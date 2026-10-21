/*
 * xtream_epg.c - program guide for the Xtream Codes provider (#149).
 *
 * A panel serves its guide as one XMLTV file (xmltv.php), keyed by the
 * epg_channel_id each channel carries. The user can bring their own XMLTV
 * address instead. This module downloads it once, scans it for the current and
 * next programme of every channel, and answers lookups by epg_channel_id.
 *
 * Same approach as the IPTV guide: a byte scan, not an XML parse, so a guide of
 * tens of megabytes costs two short strings per channel. An oversized guide is
 * refused by evo_net (EVO_NET_MAX_BODY) and then simply stays empty.
 *
 * The caller passes a log label (the host, never the URL): the xmltv.php address
 * carries the account password and must not reach evo.log.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

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

static struct {
    int      state;
    int      gen;               /* bumped per load: drops stale downloads */
    ent_t   *tab;               /* open addressing */
    size_t   cap, used;
    int      with_data;         /* guide channels that have a now or next */
    char     label[96];
    int      seen, hit;         /* lookups from the channel lists */
} E;

static void free_table(void)
{
    for (size_t i = 0; i < E.cap; ++i) {
        free(E.tab[i].id); free(E.tab[i].now); free(E.tab[i].next);
    }
    free(E.tab);
    E.tab = NULL;
    E.cap = E.used = 0;
    E.with_data = 0;
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

static ent_t *tab_find(const char *id, int create);

static int tab_grow(void)
{
    size_t ncap = E.cap ? E.cap * 2 : 1024;
    ent_t *nt = (ent_t *)calloc(ncap, sizeof *nt);
    if (!nt) return -1;
    ent_t *old = E.tab;
    size_t ocap = E.cap;
    E.tab = nt;
    E.cap = ncap;
    for (size_t i = 0; i < ocap; ++i) {
        if (!old[i].id) continue;
        size_t j = hash_str(old[i].id) % ncap;
        while (nt[j].id) j = (j + 1) % ncap;
        nt[j] = old[i];
    }
    free(old);
    return 0;
}

static ent_t *tab_find(const char *id, int create)
{
    if (!E.cap) {
        if (!create || tab_grow() != 0) return NULL;
    }
    size_t j = hash_str(id) % E.cap;
    while (E.tab[j].id) {
        if (strcmp(E.tab[j].id, id) == 0) return &E.tab[j];
        j = (j + 1) % E.cap;
    }
    if (!create) return NULL;
    if ((E.used + 1) * 10 > E.cap * 7) {
        if (tab_grow() != 0) return NULL;
        return tab_find(id, create);
    }
    E.tab[j].id = dup_n(id, strlen(id));
    if (!E.tab[j].id) return NULL;
    E.used++;
    return &E.tab[j];
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

static void parse_guide(const char *body, size_t len)
{
    const int64_t now = (int64_t)time(NULL);
    const char *end = body + len;
    const char *p = body;
    int progs = 0;

    while (p < end) {
        const char *op = strstr(p, "<programme");
        if (!op || op >= end) break;
        const char *close = strstr(op, "</programme>");
        const char *pe = close ? close : end;
        const char *hdr_end = memchr(op, '>', (size_t)(pe - op));
        if (!hdr_end) break;

        char start[32] = {0}, stop[32] = {0}, chan[96] = {0};
        tag_attr(op, hdr_end, "start",   start, sizeof start);
        tag_attr(op, hdr_end, "stop",    stop,  sizeof stop);
        tag_attr(op, hdr_end, "channel", chan,  sizeof chan);
        xml_decode(chan);
        trim(chan);

        int64_t ts = start[0] ? xmltv_time(start) : -1;
        if (chan[0] && ts > 0) {
            int64_t te = stop[0] ? xmltv_time(stop) : ts + 1800;
            const char *tb = strstr(hdr_end, "<title");
            const char *tg = (tb && tb < pe) ? memchr(tb, '>', (size_t)(pe - tb)) : NULL;
            const char *tc = tg ? strstr(tg, "</title>") : NULL;
            if (tg && tc && tc > tg + 1 && tc <= pe && te > now) {
                char title[128];
                size_t n = (size_t)(tc - tg - 1);
                if (n >= sizeof title) n = sizeof title - 1;
                memcpy(title, tg + 1, n);
                title[n] = '\0';
                xml_decode(title);
                trim(title);
                ent_t *e = title[0] ? tab_find(chan, 1) : NULL;
                if (e) {
                    if (ts <= now) {
                        set_str(&e->now, title);
                        e->now_stop = te;
                        progs++;
                    } else if (!e->next || ts < e->next_start) {
                        set_str(&e->next, title);
                        e->next_start = ts;
                        progs++;
                    }
                }
            }
        }
        p = close ? close + 12 : end;
    }

    E.with_data = 0;
    for (size_t i = 0; i < E.cap; ++i)
        if (E.tab[i].id && (E.tab[i].now || E.tab[i].next)) E.with_data++;
    E.state = E.with_data ? XT_EPG_OK : XT_EPG_EMPTY;
    PROV_LOG("xtream: guide from %s: %d programmes, %d channels have now/next",
             E.label, progs, E.with_data);
}

static void on_guide(int success, int status, const char *body, size_t len, void *ud)
{
    if ((int)(intptr_t)ud != E.gen) return;     /* a newer load replaced this one */
    if (!success || status < 200 || status >= 300 || !body || !len) {
        PROV_LOG("xtream: guide from %s failed (success=%d status=%d len=%zu)",
                 E.label, success, status, len);
        E.state = XT_EPG_FAILED;
        return;
    }
    parse_guide(body, len);
}

void xtream_epg_load(const char *url, const char *label)
{
    free_table();
    E.gen++;
    E.seen = E.hit = 0;
    snprintf(E.label, sizeof E.label, "%s", label ? label : "the panel");
    if (!url || !url[0]) { E.state = XT_EPG_NONE; return; }

    E.state = XT_EPG_LOADING;
    PROV_LOG("xtream: loading guide from %s", E.label);
    if (evo_net_request_async("GET", url, NULL, NULL, 0, on_guide,
                              (void *)(intptr_t)E.gen) != 0) {
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
    ent_t *e = tab_find(epg_id, 0);
    if (!e) return 0;

    const char *n = e->now, *x = e->next;
    if (n && e->now_stop <= (int64_t)time(NULL)) n = NULL;   /* ended since the scan */
    if (!n && !x) return 0;
    if (n) snprintf(now, now_sz, "%s", n);
    if (x) snprintf(next, next_sz, "%s", x);
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
