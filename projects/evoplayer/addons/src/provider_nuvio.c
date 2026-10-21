/*
 * provider_nuvio.c — Nuvio, browsed natively, with its web app as an extra.
 *
 * Nuvio's TV web app (NuvioTVSmart, the Tizen/webOS build) is a Stremio-addon
 * client: catalogs from addons such as Cinemeta, streams from addons such as
 * Torrentio or AIOStreams, resolved through TorBox / Real-Debrid. So debrid
 * services arrive through Nuvio's addons, with no debrid code in EVO.
 *
 * Like Jellyfin, it is browsed in EVO's own screens (rml/mediaserver.rml): the
 * catalog, search and streams run on the Stremio addon engine in
 * provider_stremio.c, over the addons installed in the Nuvio web app, with the
 * requests on FFmpeg's HTTP client. The web app itself stays as the "web
 * version" (Triangle in the chooser): that is where addons are added and
 * removed, and evo_webui.c's "nuvio" hook profile hands its player's stream to
 * EVO's player.
 *
 * The source string is the site's address: a self-hosted build
 * (http://<host>:<port>, e.g. the nuvio-test container) or the hosted app
 * (https://web.nuvioapp.space). Persisted as nuvio.conf.
 */
#include <stdio.h>
#include <string.h>

#include "evo_provider.h"
#include "provider_stremio.h"
#include "evo_data_path.h"

#define NUVIO_CONF         "nuvio.conf"
#define NUVIO_DEFAULT_PORT 80

static char g_host[128];
static int  g_port = NUVIO_DEFAULT_PORT;
static int  g_https = 0;
static char g_source[176];

static int nv_init(void)
{
    g_host[0] = '\0';
    g_port = NUVIO_DEFAULT_PORT;
    g_https = 0;

    FILE *f = fopen(evo_data_path(NUVIO_CONF), "r");
    if (!f) return 0;                   /* not set up yet - not an error */
    char line[160];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "host=", 5) == 0) {
            snprintf(g_host, sizeof g_host, "%s", line + 5);
        } else if (strncmp(line, "port=", 5) == 0) {
            int p = 0;
            if (sscanf(line + 5, "%d", &p) == 1 && p > 0 && p <= 65535) g_port = p;
        } else if (strncmp(line, "https=", 6) == 0) {
            g_https = line[6] == '1';
        }
    }
    fclose(f);
    return 0;
}

static void nv_shutdown(void) { /* holds no resources */ }

/* The Nuvio TV web app can ship inside EVO (assets/nuvio, built by
 * scripts/build-nuvio-web.sh). When it is there Nuvio needs no address: EVO
 * serves it from its own local server, so it works with no internet host to
 * depend on. An address typed in the options overrides it. */
#define NUVIO_BUNDLE_INDEX "/app0/assets/nuvio/index.html"
#define NUVIO_BUNDLE_URL   "local:nuvio"

static int nv_bundled(void)
{
    FILE *f = fopen(NUVIO_BUNDLE_INDEX, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int nv_is_configured(void) { return (g_host[0] || nv_bundled()) ? 1 : 0; }

static const char *nv_get_source(void)
{
    if (!g_host[0]) return "";
    snprintf(g_source, sizeof g_source, "%s://%s:%d", g_https ? "https" : "http",
             g_host, g_port);
    return g_source;
}

static int nv_set_source(const char *value)
{
    if (!value || !value[0]) {                   /* empty: back to the built-in Nuvio */
        FILE *c = fopen(evo_data_path(NUVIO_CONF), "w");
        if (c) fclose(c);
        g_host[0] = '\0';
        g_port = NUVIO_DEFAULT_PORT;
        g_https = 0;
        return c ? 0 : -1;
    }
    char host[128];
    int port = NUVIO_DEFAULT_PORT, tls = 0;
    if (evo_provider_parse_web_source(value, host, sizeof host, &port, &tls,
                                      NUVIO_DEFAULT_PORT) != 0)
        return -1;

    FILE *f = fopen(evo_data_path(NUVIO_CONF), "w");
    if (!f) return -1;
    fprintf(f, "host=%s\nport=%d\nhttps=%d\n", host, port, tls);
    fclose(f);

    snprintf(g_host, sizeof g_host, "%s", host);
    g_port = port;
    g_https = tls;
    return 0;
}

static const char *nv_web_ui_url(void)
{
    if (g_host[0]) return nv_get_source();
    return nv_bundled() ? NUVIO_BUNDLE_URL : "";
}

/* evo_webui.c saves each site's page storage as webui/<host>_<port>.json, and
 * the built-in page's as webui/local_<loopback port>.json. */
#define NUVIO_LOCAL_PORT 8686

const char *evo_nuvio_store_file(void)
{
    static char leaf[200];
    if (g_host[0]) snprintf(leaf, sizeof leaf, "webui/%s_%d.json", g_host, g_port);
    else           snprintf(leaf, sizeof leaf, "webui/local_%d.json", NUVIO_LOCAL_PORT);
    return leaf;
}

const evo_provider_t evo_provider_nuvio = {
    .id            = "nuvio",
    .name          = "Nuvio",
    .icon          = "icon_emby.png",
    .caps          = EVO_PROVIDER_CAP_CATALOG | EVO_PROVIDER_CAP_SEARCH |
                     EVO_PROVIDER_CAP_RESOLVE | EVO_PROVIDER_CAP_CONFIG |
                     EVO_PROVIDER_CAP_WEBUI | EVO_PROVIDER_CAP_PICK,
    .api_version   = EVO_PROVIDER_API_VERSION,
    .init          = nv_init,
    .shutdown      = nv_shutdown,
    .is_configured = nv_is_configured,
    .list_catalog  = evo_nuvio_list_catalog,
    .search        = evo_nuvio_search,
    .resolve       = evo_nuvio_resolve,
    .get_source    = nv_get_source,
    .set_source    = nv_set_source,
    .web_ui_url    = nv_web_ui_url,
    .web_ui_path   = "/",
    .web_ui_hook   = "nuvio",
    .ui_embedded   = "mediaserver",
};
