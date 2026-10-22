/*
 * xtream_epg.c - program guide for the Xtream Codes provider (#149).
 *
 * A panel serves its guide as one XMLTV file (xmltv.php), keyed by the
 * epg_channel_id each channel carries. The user can bring their own XMLTV
 * address instead. This module downloads it once, scans it for the current and
 * next programme of every channel, and answers lookups by epg_channel_id.
 *
 * Same approach as the IPTV guide: a byte scan, not an XML parse, so a guide
 * costs two short strings per channel. A full panel guide is bigger than
 * evo_net's body cap (64 MB, #159), so it is not downloaded into memory: a
 * worker thread streams it through FFmpeg's HTTP client (evo_avio_stream) and
 * scans each programme as it arrives. Memory stays flat whatever the file size.
 * Where FFmpeg is not linked (host test builds) the old whole-body request runs.
 *
 * The caller passes a log label (the host, never the URL): the xmltv.php address
 * carries the account password and must not reach evo.log.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <pthread.h>

#include "addon_xtream.h"
#include "evo_net.h"
#include "evo_provider_log.h"

typedef struct {
    char   *id;          /* heap: the guide's channel id */
    char   *now;         /* heap or NULL */
    char   *next;        /* heap or NULL */
    int64_t now_stop;    /* when "now" ends */
    int64_t next_start;
} ent_t;

typedef struct {
    ent_t   *tab;               /* open addressing */
    size_t   cap, used;
} tbl_t;

static struct {
    int      state;
    int      gen;               /* bumped per load: drops stale downloads */
    tbl_t    cur;               /* the table lookups read; swapped in under E_mx */
    int      with_data;         /* guide channels that have a now or next */
    char     label[96];
    int      seen, hit;         /* lookups from the channel lists */
} E;

static pthread_mutex_t E_mx = PTHREAD_MUTEX_INITIALIZER;

static void tbl_free(tbl_t *t)
{
    for (size_t i = 0; i < t->cap; ++i) {
        free(t->tab[i].id); free(t->tab[i].now); free(t->tab[i].next);
    }
    free(t->tab);
    t->tab = NULL;
    t->cap = t->used = 0;
}

static char *dup_n(const char *s, size_t n)
{
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static uint32_t hash_str(const char *s)
{
    uint32_t h = 5381;
    while (*s) h = ((h << 5) + h) + (unsigned char)*s++;
    return h;
}

static ent_t *tab_find(tbl_t *t, const char *id, int create);

static int tab_grow(tbl_t *t)
{
    size_t ncap = t->cap ? t->cap * 2 : 1024;
    ent_t *nt = (ent_t *)calloc(ncap, sizeof *nt);
    if (!nt) return -1;
    ent_t *old = t->tab;
    size_t ocap = t->cap;
    t->tab = nt;
    t->cap = ncap;
    for (size_t i = 0; i < ocap; ++i) {
        if (!old[i].id) continue;
        size_t j = hash_str(old[i].id) % ncap;
        while (nt[j].id) j = (j + 1) % ncap;
        nt[j] = old[i];
    }
    free(old);
    return 0;
}

static ent_t *tab_find(tbl_t *t, const char *id, int create)
{
    if (!t->cap) {
        if (!create || tab_grow(t) != 0) return NULL;
    }
    size_t j = hash_str(id) % t->cap;
    while (t->tab[j].id) {
        if (strcmp(t->tab[j].id, id) == 0) return &t->tab[j];
        j = (j + 1) % t->cap;
    }
    if (!create) return NULL;
    if ((t->used + 1) * 10 > t->cap * 7) {
        if (tab_grow(t) != 0) return NULL;
        return tab_find(t, id, create);
    }
    t->tab[j].id = dup_n(id, strlen(id));
    if (!t->tab[j].id) return NULL;
    t->used++;
    return &t->tab[j];
}

/* "20260924123000 +0000" -> unix seconds, or -1. */
static int64_t xmltv_time(const char *s)
{
    int y, mo, d, h, mi, se;
    if (!s || sscanf(s, "%4d%2d%2d%2d%2d%2d", &y, &mo, &d, &h, &mi, &se) != 6)
        return -1;
    int yy = y - (mo <= 2);
    int era = (yy >= 0 ? yy : yy - 399) / 400;
    unsigned yoe = (unsigned)(yy - era * 400);
    unsigned doy = (unsigned)((153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t t = ((int64_t)era * 146097 + (int64_t)doe - 719468) * 86400 +
                h * 3600 + mi * 60 + se;
    const char *sp = strchr(s, ' ');
    if (sp) {
        while (*sp == ' ') sp++;
        if ((*sp == '+' || *sp == '-') && strlen(sp) >= 5) {
            int sign = (*sp == '-') ? -1 : 1;
            t -= sign * (((sp[1] - '0') * 10 + (sp[2] - '0')) * 3600 +
                         ((sp[3] - '0') * 10 + (sp[4] - '0')) * 60);
        }
    }
    return t;
}

static int tag_attr(const char *el, const char *el_end, const char *attr,
                    char *out, size_t out_sz)
{
    size_t al = strlen(attr);
    for (const char *p = el; p + al < el_end; ++p) {
        if (strncmp(p, attr, al) != 0) continue;
        if (p != el && p[-1] != ' ') continue;
        const char *q = p + al;
        if (q >= el_end || *q != '=') continue;
        q++;
        if (q >= el_end || *q != '"') continue;
        q++;
        const char *e = memchr(q, '"', (size_t)(el_end - q));
        if (!e) return -1;
        size_t n = (size_t)(e - q);
        if (n >= out_sz) n = out_sz - 1;
        memcpy(out, q, n);
        out[n] = '\0';
        return 0;
    }
    return -1;
}

static void xml_decode(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (*r == '&') {
            if      (strncmp(r, "&amp;", 5) == 0)  { *w++ = '&';  r += 5; }
            else if (strncmp(r, "&quot;", 6) == 0) { *w++ = '"';  r += 6; }
            else if (strncmp(r, "&apos;", 6) == 0) { *w++ = '\''; r += 6; }
            else if (strncmp(r, "&#39;", 5) == 0)  { *w++ = '\''; r += 5; }
            else if (strncmp(r, "&lt;", 4) == 0)   { *w++ = '<';  r += 4; }
            else if (strncmp(r, "&gt;", 4) == 0)   { *w++ = '>';  r += 4; }
            else *w++ = *r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == '\t'))
        s[--n] = '\0';
    size_t lead = 0;
    while (s[lead] == ' ' || s[lead] == '\t' || s[lead] == '\r' || s[lead] == '\n') lead++;
    if (lead) memmove(s, s + lead, n - lead + 1);
}

static void set_str(char **dst, const char *s)
{
    free(*dst);
    *dst = dup_n(s, strlen(s));
}

/* One scan of one guide: the table it fills, the unparsed tail, and the count. */
typedef struct {
    tbl_t   tbl;
    char   *buf;
    size_t  used, cap;
    int64_t now;
    int     progs;
} scan_t;

/* One "<programme ...>...</programme>" block, already NUL-ended at its close. */
static void scan_block(scan_t *sc, const char *op, const char *pe)
{
    const char *hdr_end = memchr(op, '>', (size_t)(pe - op));
    if (!hdr_end) return;

    char start[32] = {0}, stop[32] = {0}, chan[96] = {0};
    tag_attr(op, hdr_end, "start",   start, sizeof start);
    tag_attr(op, hdr_end, "stop",    stop,  sizeof stop);
    tag_attr(op, hdr_end, "channel", chan,  sizeof chan);
    xml_decode(chan);
    trim(chan);

    const int64_t now = sc->now;
    int64_t ts = start[0] ? xmltv_time(start) : -1;
    if (!chan[0] || ts <= 0) return;
    int64_t te = stop[0] ? xmltv_time(stop) : ts + 1800;
    const char *tb = strstr(hdr_end, "<title");
    const char *tg = (tb && tb < pe) ? memchr(tb, '>', (size_t)(pe - tb)) : NULL;
    const char *tc = tg ? strstr(tg, "</title>") : NULL;
    if (!(tg && tc && tc > tg + 1 && tc <= pe && te > now)) return;

    char title[128];
    size_t n = (size_t)(tc - tg - 1);
    if (n >= sizeof title) n = sizeof title - 1;
    memcpy(title, tg + 1, n);
    title[n] = '\0';
    xml_decode(title);
    trim(title);
    ent_t *e = title[0] ? tab_find(&sc->tbl, chan, 1) : NULL;
    if (!e) return;
    if (ts <= now) {
        set_str(&e->now, title);
        e->now_stop = te;
        sc->progs++;
    } else if (!e->next || ts < e->next_start) {
        set_str(&e->next, title);
        e->next_start = ts;
        sc->progs++;
    }
}

/* Takes the next piece of the file; scans every programme that is now complete and
 * keeps only the unfinished tail. Returns 0, or -1 when out of memory. */
static int scan_feed(scan_t *sc, const char *chunk, size_t n)
{
    if (sc->used + n + 1 > sc->cap) {
        size_t ncap = sc->cap ? sc->cap : 65536;
        while (sc->used + n + 1 > ncap) ncap *= 2;
        char *nb = (char *)realloc(sc->buf, ncap);
        if (!nb) return -1;
        sc->buf = nb;
        sc->cap = ncap;
    }
    memcpy(sc->buf + sc->used, chunk, n);
    sc->used += n;
    sc->buf[sc->used] = '\0';

    char *p = sc->buf;
    char *end = sc->buf + sc->used;
    const char *keep = NULL;
    for (;;) {
        char *op = strstr(p, "<programme");
        if (!op) break;
        char *cl = strstr(op, "</programme>");
        if (!cl) { keep = op; break; }
        *cl = '\0';
        scan_block(sc, op, cl);
        p = cl + 12;
        if (p >= end) { p = end; break; }
    }
    if (!keep) {                        /* keep a possible half "<programme" at the end */
        size_t tail = (size_t)(end - p);
        keep = tail > 10 ? end - 10 : p;
    }
    size_t rest = (size_t)(end - keep);
    if (rest > (1u << 20)) { keep = end - 10; rest = 10; }   /* a single absurd block */
    memmove(sc->buf, keep, rest);
    sc->used = rest;
    sc->buf[rest] = '\0';
    return 0;
}

/* The scan is done: publish its table, or drop it if a newer load replaced it. */
static void scan_finish(scan_t *sc, int gen, int ok)
{
    int with = 0;
    for (size_t i = 0; i < sc->tbl.cap; ++i)
        if (sc->tbl.tab[i].id && (sc->tbl.tab[i].now || sc->tbl.tab[i].next)) with++;

    pthread_mutex_lock(&E_mx);
    if (gen == E.gen) {
        tbl_t old = E.cur;
        E.cur = sc->tbl;
        sc->tbl = old;                  /* freed below */
        E.with_data = with;
        E.state = !ok ? XT_EPG_FAILED : with ? XT_EPG_OK : XT_EPG_EMPTY;
        PROV_LOG("xtream: guide from %s: %d programmes, %d channels have now/next",
                 E.label, sc->progs, with);
    }
    pthread_mutex_unlock(&E_mx);
    tbl_free(&sc->tbl);
    free(sc->buf);
}

/* Whole-body fallback (host builds without FFmpeg). */
static void on_guide(int success, int status, const char *body, size_t len, void *ud)
{
    const int gen = (int)(intptr_t)ud;
    if (gen != E.gen) return;           /* a newer load replaced this one */
    if (!success || status < 200 || status >= 300 || !body || !len) {
        PROV_LOG("xtream: guide from %s failed (success=%d status=%d len=%zu)",
                 E.label, success, status, len);
        E.state = XT_EPG_FAILED;
        return;
    }
    scan_t sc;
    memset(&sc, 0, sizeof sc);
    sc.now = (int64_t)time(NULL);
    int ok = 1;
    for (size_t off = 0; off < len && ok; off += 65536) {
        size_t n = len - off < 65536 ? len - off : 65536;
        ok = scan_feed(&sc, body + off, n) == 0;
    }
    scan_finish(&sc, gen, ok);
}

/* FFmpeg's HTTP client, streaming (evo_http_avio.c). Weak: absent in host builds. */
extern int evo_avio_stream(const char *url, int (*sink)(const char *, size_t, void *),
                           void *ud, int *out_status) __attribute__((weak));

typedef struct {
    char   url[EVO_NET_MAX_URL];
    int    gen;
    scan_t sc;
} job_t;

static int job_sink(const char *chunk, size_t n, void *ud)
{
    job_t *j = (job_t *)ud;
    if (j->gen != E.gen) return -1;     /* a newer load replaced this one: stop reading */
    return scan_feed(&j->sc, chunk, n);
}

static void *guide_thread(void *arg)
{
    job_t *j = (job_t *)arg;
    j->sc.now = (int64_t)time(NULL);
    int status = 0;
    const int rc = evo_avio_stream(j->url, job_sink, j, &status);
    if (j->gen == E.gen) {
        if (rc != EVO_NET_OK)
            PROV_LOG("xtream: guide from %s failed (rc=%d status=%d)", E.label, rc, status);
        scan_finish(&j->sc, j->gen, rc == EVO_NET_OK);
    } else {
        tbl_free(&j->sc.tbl);
        free(j->sc.buf);
    }
    free(j);
    return NULL;
}

void xtream_epg_load(const char *url, const char *label)
{
    pthread_mutex_lock(&E_mx);
    tbl_free(&E.cur);
    E.with_data = 0;
    E.gen++;
    E.seen = E.hit = 0;
    snprintf(E.label, sizeof E.label, "%s", label ? label : "the panel");
    const int gen = E.gen;
    if (!url || !url[0]) { E.state = XT_EPG_NONE; pthread_mutex_unlock(&E_mx); return; }
    E.state = XT_EPG_LOADING;
    pthread_mutex_unlock(&E_mx);

    PROV_LOG("xtream: loading guide from %s", E.label);

    if (evo_avio_stream && strlen(url) < EVO_NET_MAX_URL) {
        job_t *j = (job_t *)calloc(1, sizeof *j);
        if (j) {
            snprintf(j->url, sizeof j->url, "%s", url);
            j->gen = gen;
            pthread_t th;
            pthread_attr_t at;
            pthread_attr_init(&at);
            pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
            pthread_attr_setstacksize(&at, 512 * 1024);
            const int prc = pthread_create(&th, &at, guide_thread, j);
            pthread_attr_destroy(&at);
            if (prc == 0) return;
            free(j);
        }
        PROV_LOG("xtream: guide thread could not start, using the plain request");
    }

    if (evo_net_request_async("GET", url, NULL, NULL, 0, on_guide,
                              (void *)(intptr_t)gen) != 0) {
        PROV_LOG("xtream: guide request from %s could not start", E.label);
        E.state = XT_EPG_FAILED;
    }
}

int xtream_epg_state(void) { return E.state; }

int xtream_epg_lookup(const char *epg_id, char *now, size_t now_sz,
                      char *next, size_t next_sz)
{
    if (now_sz) now[0] = '\0';
    if (next_sz) next[0] = '\0';
    if (!epg_id || !epg_id[0]) return 0;
    E.seen++;
    if (E.state != XT_EPG_OK) return 0;
    pthread_mutex_lock(&E_mx);
    ent_t *e = tab_find(&E.cur, epg_id, 0);
    if (!e) { pthread_mutex_unlock(&E_mx); return 0; }

    const char *n = e->now, *x = e->next;
    if (n && e->now_stop <= (int64_t)time(NULL)) n = NULL;   /* ended since the scan */
    if (!n && !x) { pthread_mutex_unlock(&E_mx); return 0; }
    if (n) snprintf(now, now_sz, "%s", n);
    if (x) snprintf(next, next_sz, "%s", x);
    pthread_mutex_unlock(&E_mx);
    E.hit++;
    return 1;
}

const char *xtream_epg_status(void)
{
    static char buf[96];
    switch (E.state) {
    case XT_EPG_LOADING: return "Loading the guide...";
    case XT_EPG_FAILED:  return "The guide could not be downloaded";
    case XT_EPG_EMPTY:   return "Panel has no guide";
    case XT_EPG_OK:
        if (E.seen > 0 && E.hit == 0) {
            snprintf(buf, sizeof buf, "Guide covers 0 of %d channels", E.seen);
            return buf;
        }
        return "";
    default:             return "";
    }
}
