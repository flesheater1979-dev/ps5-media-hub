/*
 * evo_rmlui_provider_art.cpp — remote artwork for provider screens (#90).
 *
 * Poster walls are the point of "the service's own look", so this is not an
 * optional extra: a channel grid with no logos and a library with no posters
 * both look like EVO drew them, which is the outcome the whole seam exists to
 * avoid.
 *
 * THE PATH
 *
 *   url -> evo_net GET -> bytes on disk under the provider's cache
 *       -> FFmpeg decode by path -> BGRA -> SetMemoryTexture("evo:mem/...")
 *
 * Two deliberate choices in that chain:
 *
 * Bytes land on DISK before they are decoded, rather than being decoded from
 * memory. It costs a small write per poster and buys three things: the decode
 * is the same avformat_open_input + image2 path CoverArtService already uses,
 * so no new FFmpeg demuxer has to be enabled and no in-memory AVIOContext has
 * to be maintained; the poster cache survives a relaunch, so the second visit
 * to a library paints immediately; and a malformed image is contained by
 * FFmpeg's own probing rather than by code written here.
 *
 * The cost of going through image2 is that the decoder is chosen from the
 * FILENAME, so the cache file has to be named for its sniffed type - see
 * sniff_ext(). A poster whose format has no decoder in this build (GIF)
 * is refused on arrival rather than written and failed later.
 *
 * The decoded texture lives in the render interface's "evo:mem/" registry,
 * which both backends already resolve before touching the filesystem
 * (evo_rmlui_render.cpp LoadTexture, evo_rmlui_render_agc.cpp). Nothing in the
 * render path changes for this feature at all - that is why it is safe to add
 * to a backend whose failure mode is a silent mis-render.
 *
 * THE BUDGET
 *
 * An LRU over decoded bytes. A 500-channel playlist asks for 500 logos and
 * would otherwise hold half a gigabyte of premultiplied BGRA; the cap keeps it
 * to EVO_BUNDLE_MAX_TEXTURE_BYTES and evicting something still on screen just
 * makes that element draw empty again for a frame, which is why the cap is
 * generous rather than tight.
 */
#include "evo_rmlui_app.h"
#include "evo_rmlui_render_bridge.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include "evo_provider.h"
#include "evo_provider_bundle.h"
#include "evo_net.h"
#include "evo_data_path.h"
#include "evo_provider_log.h"
int evo_plex_art_auth_header(const char *url, char *out, size_t cap);

/*
 * EVO_PROVIDER_ART_NO_DECODE: host renderer only.
 *
 * tools/uiview_playback_rml.sh links RmlUi and freetype but no FFmpeg - there
 * are no host FFmpeg development libraries in the dev image, and building them
 * to decode a poster in a layout preview is not a trade worth making. With
 * this defined the download, the disk cache, the key registry, the LRU and
 * every path check all still run and are all still exercised; only the decode
 * itself is absent, so the harness renders the no-artwork branch that a
 * provider bundle has to handle anyway (and which is the first frame of every
 * poster wall on hardware).
 *
 * It is never defined for a device build - the Makefile does not pass it.
 */
#ifndef EVO_PROVIDER_ART_NO_DECODE
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#endif
}

namespace {

/* Posters are scaled to fit this box. Bigger than any tile the UI draws at
 * 1080p, small enough that 200 of them fit the budget. */
constexpr int kMaxDim = 480;

struct Entry {
    std::string key;          /* "evo:mem/prov-<hash>" */
    std::string file;         /* where the raw bytes were written */
    size_t      bytes = 0;    /* decoded size, 0 until registered */
    bool        ready = false;
    bool        failed = false;
    uint64_t    last_req = 0;  /* ms of the last request for this key (#155) */
};

struct ArtState {
    std::unordered_map<std::string, Entry> by_key;
    /* Most-recently-used at the front. Holds keys, not iterators, because the
     * map rehashes. */
    std::list<std::string> lru;
    size_t used = 0;
    size_t budget = EVO_BUNDLE_MAX_TEXTURE_BYTES;
    /* Keys whose bytes have arrived and are waiting to be decoded. Decoding is
     * deferred to poll() so it happens on the render thread's frame, where the
     * cost is visible in the frame time rather than hidden in a callback. */
    std::vector<std::string> arrived;
    int in_flight = 0;
    unsigned evictions = 0, redecodes = 0;   /* #155: counted, logged in batches */
};

ArtState& S()
{
    static ArtState s;
    return s;
}

/* At most this many downloads outstanding. evo_net's queue is 32 slots and is
 * shared with catalog requests, so art must not be allowed to fill it - a
 * poster wall that starves the catalog request behind it would show an empty
 * screen full of pictures. */
constexpr int kMaxInFlight = 8;

/* FNV-1a over the URL. Only needs to avoid collisions between the posters of
 * one session, and a 64-bit hash does that with room to spare; it is not a
 * security boundary - the file it names is written by us, into a directory
 * built by evo_bundle_path(). */
std::string url_hash(const char* url)
{
    uint64_t h = 1469598103934665603ull;
    for (const unsigned char* p = (const unsigned char*)url; *p; ++p) {
        h ^= *p;
        h *= 1099511628211ull;
    }
    char buf[32];
    snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
    return std::string(buf);
}

/*
 * The cache file's extension is load-bearing, not cosmetic. FFmpeg's image2
 * demuxer picks the decoder from the FILENAME (ff_guess_image2_codec), never
 * from the content. This cache used to name every download "<hash>.img", and
 * ".img" is in that table - it maps to AV_CODEC_ID_GEM, a format the FFmpeg
 * profile does not build a decoder for. So every logo opened fine and then
 * died at avcodec_find_decoder, which reads as "no logos" with nothing in the
 * log. Naming the file for what the bytes actually are keeps the decode on the
 * png/mjpeg decoders scripts/build-ffmpeg.sh does enable.
 */
const char* sniff_ext(const char* bytes, size_t len)
{
    if (len >= 8 && memcmp(bytes, "\x89PNG\r\n\x1a\n", 8) == 0) return ".png";
    if (len >= 3 && memcmp(bytes, "\xff\xd8\xff", 3) == 0)      return ".jpg";
    /* WebP: Stremio's poster host (images.metahub.space) sends it. The build
     * has the webp decoder, and image2 picks it by the extension. */
    if (len >= 12 && memcmp(bytes, "RIFF", 4) == 0 && memcmp(bytes + 8, "WEBP", 4) == 0) return ".webp";
    return nullptr;   /* GIF lands here: no decoder in this build. */
}

/* Every extension sniff_ext() can produce, for locating a file a previous
 * session cached when the bytes are not in hand to sniff again. */
const char* const kArtExts[] = { ".png", ".jpg", ".webp" };

/*
 * Why this module logs at all, and why it is capped.
 *
 * It used to log nothing whatsoever, so "no logos" was indistinguishable from
 * "no logos in the playlist", a 403, a write that failed, and a decode that
 * failed - none of which leave any other trace. But art is driven from Tick(),
 * i.e. every frame, and a per-frame PROV_LOG in this path is what produced a
 * 16.9 MB log and cost frame rate once already.
 *
 * So: every line below is on a ONE-SHOT path (a per-key state transition that
 * happens at most once, because `failed`/`ready` latch), and the total is capped
 * anyway in case a level has hundreds of broken posters.
 */
int art_log_budget = 24;
#define ART_LOG(...)                                                          \
    do {                                                                      \
        if (art_log_budget > 0) { art_log_budget--; PROV_LOG(__VA_ARGS__); }  \
    } while (0)

void touch(const std::string& key)
{
    ArtState& s = S();
    for (auto it = s.lru.begin(); it != s.lru.end(); ++it) {
        if (*it == key) { s.lru.splice(s.lru.begin(), s.lru, it); return; }
    }
    s.lru.push_front(key);
}

uint64_t now_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000);
}

/*
 * #155: a tile that already holds its key never asks for the art again, so
 * evicting an image that is on screen blanks it for good. The screen cannot be
 * asked which keys it shows, so anything requested in the last kPinMs counts as
 * on screen and is skipped. Only past twice the budget is that overridden, so
 * memory stays bounded.
 */
constexpr uint64_t kPinMs = 120000;

void evict_to_budget()
{
    ArtState& s = S();
    EvoRenderBridge* r = EvoRmlApp::Instance().RenderBridge();
    const uint64_t now = now_ms();
    const size_t hard = s.budget * 2;
    for (auto lit = s.lru.end(); s.used > s.budget && lit != s.lru.begin();) {
        --lit;
        auto it = s.by_key.find(*lit);
        if (it == s.by_key.end()) { lit = s.lru.erase(lit); continue; }
        if (s.used <= hard && now - it->second.last_req < kPinMs) continue;   /* on screen */
        lit = s.lru.erase(lit);
        if (it->second.ready && r) r->DropMemoryTexture(it->second.key);
        s.used -= it->second.bytes;
        /* The raw file stays: it is the disk cache, and re-decoding it is far
         * cheaper than downloading it again. */
        it->second.ready = false;
        it->second.bytes = 0;
        if ((++s.evictions % 20) == 1)
            PROV_LOG("art EVICT n=%u redecode=%u used=%zuK budget=%zuK",
                     s.evictions, s.redecodes, s.used >> 10, s.budget >> 10);
    }
}

/* ------------------------------------------------------------------------- */
/* Decode                                                                    */
/* ------------------------------------------------------------------------- */
/*
 * One image file to premultiplied BGRA at or under kMaxDim.
 *
 * Premultiplied because that is what the CPU rasteriser and the AGC backend
 * both expect from the evo:mem registry - handing them straight alpha makes
 * every poster's edges fringe, which is a silent wrong-looking result rather
 * than an error.
 */
#ifdef EVO_PROVIDER_ART_NO_DECODE

bool decode_image(const char* path, std::vector<uint32_t>& out, int& ow, int& oh)
{
    (void)path; (void)out; (void)ow; (void)oh;
    return false;   /* see EVO_PROVIDER_ART_NO_DECODE above */
}

#else

bool decode_image(const char* path, std::vector<uint32_t>& out, int& ow, int& oh)
{
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path, nullptr, nullptr) < 0) return false;

    /* Everything the cleanup path touches is declared before the first goto -
     * a jump past an initialisation does not compile in C++. */
    bool ok = false;
    AVCodecContext* dec = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws = nullptr;
    int vs = -1;

    if (avformat_find_stream_info(fmt, nullptr) < 0) goto done;

    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vs = (int)i;
            break;
        }
    }
    if (vs < 0) goto done;

    {
        AVCodecParameters* par = fmt->streams[vs]->codecpar;
        /* A poster with absurd dimensions is a decode that allocates hundreds
         * of megabytes before anything here gets a say. Refuse it up front. */
        if (par->width <= 0 || par->height <= 0 ||
            par->width > 8192 || par->height > 8192) goto done;

        const AVCodec* c = avcodec_find_decoder(par->codec_id);
        if (!c) goto done;
        dec = avcodec_alloc_context3(c);
        if (!dec) goto done;
        if (avcodec_parameters_to_context(dec, par) < 0) goto done;
        if (avcodec_open2(dec, c, nullptr) < 0) goto done;
    }

    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    if (!pkt || !frame) goto done;

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index != vs) { av_packet_unref(pkt); continue; }
        int rc = avcodec_send_packet(dec, pkt);
        av_packet_unref(pkt);
        if (rc < 0) break;
        rc = avcodec_receive_frame(dec, frame);
        if (rc == 0) break;
        if (rc != AVERROR(EAGAIN)) break;
    }
    if (frame->width <= 0 || frame->height <= 0) goto done;

    {
        int tw = frame->width, th = frame->height;
        if (tw > kMaxDim || th > kMaxDim) {
            double sc = (double)kMaxDim / (double)(tw > th ? tw : th);
            tw = (int)(tw * sc);
            th = (int)(th * sc);
            if (tw < 1) tw = 1;
            if (th < 1) th = 1;
        }

        sws = sws_getContext(frame->width, frame->height,
                             (AVPixelFormat)frame->format,
                             tw, th, AV_PIX_FMT_RGBA,
                             SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws) goto done;

        out.assign((size_t)tw * (size_t)th, 0);
        uint8_t* dst[4] = { (uint8_t*)out.data(), nullptr, nullptr, nullptr };
        int dst_ls[4] = { tw * 4, 0, 0, 0 };
        sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dst_ls);

        /*
         * Premultiply. swscale with AV_PIX_FMT_RGBA produces RGBA byte order
         * (byte 0=R, 1=G, 2=B, 3=A). On little-endian uint32_t, byte 0 is
         * bits 0..7 (R) and byte 2 is bits 16..23 (B), matching 0xAABBGGRR
         * expected by SetMemoryTexture on both CPU and AGC GPU backends.
         */
        for (uint32_t& px : out) {
            uint32_t a = (px >> 24) & 0xFF;
            if (a == 255) continue;
            uint32_t r = (px & 0xFF) * a / 255;
            uint32_t g = ((px >> 8) & 0xFF) * a / 255;
            uint32_t b = ((px >> 16) & 0xFF) * a / 255;
            px = (a << 24) | (b << 16) | (g << 8) | r;
        }

        ow = tw;
        oh = th;
        ok = true;
    }

done:
    if (sws) sws_freeContext(sws);
    if (frame) av_frame_free(&frame);
    if (pkt) av_packet_free(&pkt);
    if (dec) avcodec_free_context(&dec);
    if (fmt) avformat_close_input(&fmt);
    return ok;
}

#endif /* EVO_PROVIDER_ART_NO_DECODE */

/* ------------------------------------------------------------------------- */
/* Download                                                                  */
/* ------------------------------------------------------------------------- */

struct FetchCtx {
    std::string key;
    /* The cache path without an extension. on_art() appends the one it sniffs
     * from the bytes, because the type is not known before they arrive. */
    std::string base;
    std::string provider, url;
    bool alt_tried = false;
};

/* url -> the picture to use when it fails (see evo_provider_art_set_fallback). */
std::unordered_map<std::string, std::string>& Fallbacks()
{
    static std::unordered_map<std::string, std::string> m;
    return m;
}

/* The URL for the log: no query string, so a token never reaches evo.log. */
std::string log_url(const std::string& u)
{
    return u.substr(0, u.find_first_of("?#"));
}

void on_art(int success, int status, const char* body, size_t len, void* ud);

/* Start the download of ctx->url. False when the net queue refused it. */
bool start_fetch(FetchCtx* ctx)
{
    char plex_auth[1200] = {0};
    const char* headers[1];
    int header_count = 0;
    if (ctx->provider == "plex" &&
        evo_plex_art_auth_header(ctx->url.c_str(), plex_auth, sizeof plex_auth)) {
        headers[0] = plex_auth;
        header_count = 1;
    }
    ART_LOG("art REQUEST provider=%s plex_auth=%d key=%s", ctx->provider.c_str(), header_count, ctx->key.c_str());
    return evo_net_request_async_image("GET", ctx->url.c_str(), nullptr, header_count ? headers : nullptr,
                                       header_count, on_art, ctx) == 0;
}

void on_art(int success, int status, const char* body, size_t len, void* ud)
{
    FetchCtx* ctx = (FetchCtx*)ud;
    ArtState& s = S();
    if (s.in_flight > 0) s.in_flight--;

    auto it = s.by_key.find(ctx->key);
    if (it == s.by_key.end()) { delete ctx; return; }

    if (!success || status != 200 || !body || len == 0) {
        /* Remember the failure so the same broken URL is not retried on every
         * page turn - a poster wall of 404s would otherwise be 500 requests
         * per navigation. */
        ART_LOG("art FETCH fail status=%d ok=%d len=%zu provider=%s url=%s key=%s",
                status, success, len, ctx->provider.c_str(), log_url(ctx->url).c_str(), ctx->key.c_str());
        /* The server may list an image it cannot send: try the one other picture the item has. */
        if (!ctx->alt_tried) {
            auto fb = Fallbacks().find(ctx->url);
            if (fb != Fallbacks().end() && !fb->second.empty()) {
                ctx->alt_tried = true;
                ctx->url = fb->second;
                if (start_fetch(ctx)) { s.in_flight++; return; }
            }
        }
        it->second.failed = true;
        delete ctx;
        return;
    }

    /* Name the file for what the bytes actually are (see sniff_ext), then make
     * sure the directory exists: the provider's "art/" level - and, when the UI
     * is embedded rather than fetched, the provider directory above it - is not
     * created by anything else, so this fopen used to fail every time. */
    const char* ext = sniff_ext(body, len);
    if (!ext) {
        ART_LOG("art FORMAT unsupported magic=%02x%02x%02x%02x len=%zu provider=%s url=%s key=%s",
                (unsigned char)body[0], (unsigned char)(len > 1 ? body[1] : 0),
                (unsigned char)(len > 2 ? body[2] : 0),
                (unsigned char)(len > 3 ? body[3] : 0), len, ctx->provider.c_str(), log_url(ctx->url).c_str(),
                ctx->key.c_str());
        it->second.failed = true;
        delete ctx;
        return;
    }
    it->second.file = ctx->base + ext;

    if (evo_bundle_ensure_parent_dirs(it->second.file.c_str()) != 0) {
        ART_LOG("art MKDIR fail path=%s", it->second.file.c_str());
        it->second.failed = true;
        delete ctx;
        return;
    }

    FILE* f = fopen(it->second.file.c_str(), "wb");
    if (!f) {
        ART_LOG("art OPEN fail path=%s", it->second.file.c_str());
        it->second.failed = true;
        delete ctx;
        return;
    }
    size_t put = fwrite(body, 1, len, f);
    bool wrote = (put == len) && (fclose(f) == 0);
    if (!wrote) {
        remove(it->second.file.c_str());
        it->second.failed = true;
        delete ctx;
        return;
    }

    s.arrived.push_back(ctx->key);
    delete ctx;
}

} /* namespace */

/* ------------------------------------------------------------------------- */
/* Public API - see evo_provider_bundle.h for the contract                   */
/* ------------------------------------------------------------------------- */

extern "C" int evo_provider_art_request(const char* provider_id, const char* url,
                                        char* out_key, size_t out_sz)
{
    if (!url || !*url || !out_key || out_sz == 0) return -1;
    /* Only http(s). A provider item's art_url is data from a remote service,
     * so a "file:///..." in it must not turn into a local read. */
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
        return -1;

    ArtState& s = S();
    std::string h = url_hash(url);
    std::string key = "evo:mem/prov-" + h;
    if (key.size() + 1 > out_sz) return -1;
    memcpy(out_key, key.c_str(), key.size() + 1);

    auto it = s.by_key.find(key);
    if (it != s.by_key.end()) {
        it->second.last_req = now_ms();
        if (it->second.failed) return -1;
        if (it->second.ready) { touch(key); return 1; }
        /* Bytes are on disk from a previous session or an earlier eviction:
         * queue a decode rather than a download. An entry still in flight has
         * no filename yet, so there is nothing to look for. */
        if (!it->second.file.empty()) {
            FILE* f = fopen(it->second.file.c_str(), "rb");
            if (f) { fclose(f); s.arrived.push_back(key); s.redecodes++; }
        }
        return 0;
    }

    /* Where the raw bytes go. Through evo_bundle_path() because that is the
     * only sanctioned way to build a path inside a provider's directory, even
     * for a name this file generated itself. The extension is deliberately
     * absent here - it is whatever on_art() sniffs from the bytes. */
    char base[512];
    std::string rel = "art/" + h;
    if (evo_bundle_path(provider_id, rel.c_str(), base, sizeof base) != 0)
        return -1;

    Entry e;
    e.key = key;
    e.last_req = now_ms();
    s.by_key.emplace(key, e);

    /* Already cached on disk from a previous run: no network at all. Which
     * extension it was written under is not recorded anywhere, so try each. */
    for (const char* ext : kArtExts) {
        std::string cached = std::string(base) + ext;
        FILE* f = fopen(cached.c_str(), "rb");
        if (f) {
            fclose(f);
            s.by_key[key].file = cached;
            s.arrived.push_back(key);
            return 0;
        }
    }

    if (s.in_flight >= kMaxInFlight) {
        /* Queue full. Do not erase or drop permanently - return -2 so the host
         * keeps the URL and tries again on a future tick when in-flight slots drain. */
        s.by_key.erase(key);
        return -2;
    }

    FetchCtx* ctx = new FetchCtx{key, base, provider_id ? provider_id : "", url};
    if (!start_fetch(ctx)) {
        delete ctx;
        s.by_key.erase(key);
        return -1;
    }
    s.in_flight++;
    return 0;
}

extern "C" void evo_provider_art_set_fallback(const char* url, const char* alt)
{
    if (!url || !*url || !alt || !*alt || !strcmp(url, alt)) return;
    auto& m = Fallbacks();
    if (m.size() > 4096) m.clear();          /* a browsing session never needs the old ones */
    m[url] = alt;
}

extern "C" int evo_provider_art_ready(const char* key)
{
    if (!key || !*key) return 0;
    ArtState& s = S();
    auto it = s.by_key.find(key);
    if (it == s.by_key.end() || !it->second.ready) return 0;
    touch(it->first);
    return 1;
}

extern "C" void evo_provider_art_poll(void)
{
    ArtState& s = S();
    if (s.arrived.empty()) return;

    EvoRenderBridge* r = EvoRmlApp::Instance().RenderBridge();
    if (!r) { s.arrived.clear(); return; }

    /*
     * At most a couple of decodes per frame. A poster wall's worth of PNG
     * decodes in one frame is a visible hitch, and the rows are already on
     * screen - the art filling in over the next few frames is the better
     * trade, and is what every storefront on this console does.
     */
    int budget = 2;
    while (!s.arrived.empty() && budget-- > 0) {
        std::string key = s.arrived.back();
        s.arrived.pop_back();

        auto it = s.by_key.find(key);
        if (it == s.by_key.end() || it->second.ready) continue;

        std::vector<uint32_t> px;
        int w = 0, h = 0;
        if (!decode_image(it->second.file.c_str(), px, w, h)) {
            ART_LOG("art DECODE fail path=%s", it->second.file.c_str());
            it->second.failed = true;
            /* A file that will not decode is not going to start; drop it so
             * the disk cache does not keep handing it back. */
            remove(it->second.file.c_str());
            continue;
        }

        /* One line for the first poster that works, so a log can distinguish
         * "the path is broken" from "this level simply has no art". */
        static bool s_logged_first_ok = false;
        if (!s_logged_first_ok) {
            s_logged_first_ok = true;
            ART_LOG("art OK first poster %dx%d key=%s", w, h, it->second.key.c_str());
        }

        r->SetMemoryTexture(it->second.key, px.data(), w, h);
        it->second.bytes = px.size() * sizeof(uint32_t);
        it->second.ready = true;
        s.used += it->second.bytes;
        touch(key);
    }

    evict_to_budget();
    EvoRmlApp::Instance().MarkFrameDirty();
}

/* Home artwork for provider items: a separate metadata index, keyed by the history key
 * ("evo://<provider>/<item id>"); never modifies evo_recent.txt. Decoded buffers are
 * retained for the process lifetime so both Home renderers can safely use their pointers
 * after the launch-params call returns. */
namespace {
struct HomeItem {
    std::string key, provider, poster, backdrop;
    std::vector<uint32_t> poster_px, backdrop_px;
    int pw = 0, ph = 0, bw = 0, bh = 0;
    bool logged_missing = false;
};
constexpr size_t kHomeMax = 256;
std::vector<HomeItem>& home_items() {
    static std::vector<HomeItem> v;
    return v;
}
bool& home_loaded() { static bool b = false; return b; }
const char* home_index() { return evo_data_path("evo_home_art.tsv"); }
const char* plex_old_index() { return evo_data_path("evo_plex_home_art.tsv"); }
bool home_safe_url(const char* url) {
    if (!url || (strncmp(url,"http://",7) != 0 && strncmp(url,"https://",8) != 0)) return false;
    const size_t n = strlen(url);
    if (n > 1500 || n < 10 || strchr(url, '\n') || strchr(url, '\r') || strchr(url, '\t')) return false;
    /* Never write URLs containing authentication query parameters to disk. */
    std::string u(url);
    for (char& c : u) c = (char)tolower((unsigned char)c);
    return u.find("token=") == std::string::npos &&
           u.find("x-plex-token") == std::string::npos &&
           u.find("api_key=") == std::string::npos;
}
/* "evo://jellyfin/i:123" -> "jellyfin" */
std::string home_provider_of(const std::string& key) {
    if (key.compare(0, 6, "evo://") != 0) return std::string();
    const size_t e = key.find('/', 6);
    return e == std::string::npos ? std::string() : key.substr(6, e - 6);
}
/* One line: key, poster, backdrop. `plex_old`: the first file this feature wrote, whose
 * keys are bare Plex ids ("video:...") - they get the "evo://plex/" prefix. */
void home_read(const char* path, bool plex_old) {
    FILE* f = fopen(path, "rb");
    if (!f) return;
    char line[4096];
    while (home_items().size() < kHomeMax && fgets(line, sizeof line, f)) {
        char* a = strchr(line, '\t'); if (!a) continue; *a++ = 0;
        char* b = strchr(a, '\t'); if (!b) continue; *b++ = 0;
        b[strcspn(b, "\r\n")] = 0;
        std::string key = line;
        if (plex_old) {
            if (key.compare(0, 6, "video:") != 0) continue;
            key = "evo://plex/" + key;
        }
        if (key.size() > 300 || home_provider_of(key).empty()) continue;
        HomeItem item; item.key = key; item.provider = home_provider_of(key);
        if (home_safe_url(a)) item.poster = a;
        if (home_safe_url(b)) item.backdrop = b;
        if (item.poster.empty() && item.backdrop.empty()) continue;
        bool dup = false;
        for (const auto& o : home_items()) dup = dup || o.key == item.key;
        if (!dup) home_items().push_back(std::move(item));
    }
    fclose(f);
}
void home_load() {
    if (home_loaded()) return;
    home_loaded() = true;
    home_read(home_index(), false);
    home_read(plex_old_index(), true);
}
void home_save() {
    std::string tmp = std::string(home_index()) + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return;
    bool ok = true;
    for (const auto& item : home_items()) {
        if (fprintf(f, "%s\t%s\t%s\n", item.key.c_str(),
                    item.poster.c_str(), item.backdrop.c_str()) < 0) ok = false;
    }
    if (fclose(f) != 0) ok = false;
    if (ok) rename(tmp.c_str(), home_index());
    else remove(tmp.c_str());
}
const uint32_t* home_decode(const std::string& provider, const std::string& url,
                            std::vector<uint32_t>& px, int& w, int& h) {
    if (url.empty()) return nullptr;
    if (!px.empty()) return px.data();
    char key[128];
    int rc = evo_provider_art_request(provider.c_str(), url.c_str(), key, sizeof key);
    if (rc < 0) return nullptr;
    if (rc != 1) return nullptr;
    /* Use the same on-disk cache as the provider browser. */
    std::string rel = "art/" + url_hash(url.c_str());
    char base[512];
    if (evo_bundle_path(provider.c_str(), rel.c_str(), base, sizeof base) != 0) return nullptr;
    for (const char* ext : kArtExts) {
        std::string path = std::string(base) + ext;
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) continue;
        fclose(f);
        if (decode_image(path.c_str(), px, w, h) && !px.empty()) return px.data();
    }
    return nullptr;
}
} // namespace

extern "C" void evo_home_art_capture(const char* provider, const char* item_id, const char* poster,
                                     const char* backdrop) {
    if (!provider || !*provider || !item_id || !*item_id) return;
    const std::string key = std::string("evo://") + provider + "/" + item_id;
    if (key.size() > 300) return;
    home_load();
    const std::string p = home_safe_url(poster) ? poster : "";
    const std::string b = home_safe_url(backdrop) ? backdrop : "";
    if (p.empty() && b.empty()) return;
    auto& v = home_items();
    for (auto& item : v) {
        if (item.key != key) continue;
        if (item.poster == p && item.backdrop == b) return;
        item.poster = p; item.backdrop = b;
        item.poster_px.clear(); item.backdrop_px.clear();
        item.pw = item.ph = item.bw = item.bh = 0;
        item.logged_missing = false;
        home_save(); return;
    }
    if (v.size() >= kHomeMax) v.erase(v.begin());
    HomeItem item; item.key = key; item.provider = provider; item.poster = p; item.backdrop = b;
    v.push_back(std::move(item));
    home_save();
}

extern "C" const uint32_t* evo_home_art_get(const char* history_key, int hero, int* w, int* h) {
    if (w) *w = 0;
    if (h) *h = 0;
    if (!history_key || strncmp(history_key, "evo://", 6)) return nullptr;
    home_load();
    for (auto& item : home_items()) {
        if (item.key != history_key) continue;
        const uint32_t* pixels = nullptr;
        int aw = 0, ah = 0;
        if (hero && !item.backdrop.empty()) {
            pixels = home_decode(item.provider, item.backdrop, item.backdrop_px, item.bw, item.bh);
            aw = item.bw; ah = item.bh;
        }
        if (!pixels) {
            pixels = home_decode(item.provider, item.poster, item.poster_px, item.pw, item.ph);
            aw = item.pw; ah = item.ph;
        }
        if (pixels) { if (w) *w = aw; if (h) *h = ah; }
        return pixels;
    }
    /* Nothing was ever saved for it (played before art was kept): say so once. */
    static std::vector<std::string> told;
    if (told.size() < 64 && std::find(told.begin(), told.end(), history_key) == told.end()) {
        told.emplace_back(history_key);
        ART_LOG("home art: no poster for %s", history_key);
    }
    return nullptr;
}

extern "C" void evo_provider_art_set_budget(size_t bytes)
{
    S().budget = bytes;
    evict_to_budget();
}

extern "C" void evo_provider_art_clear(void)
{
    ArtState& s = S();
    EvoRenderBridge* r = EvoRmlApp::Instance().RenderBridge();
    for (auto& kv : s.by_key)
        if (kv.second.ready && r) r->DropMemoryTexture(kv.second.key);
    s.by_key.clear();
    s.lru.clear();
    s.arrived.clear();
    s.used = 0;
}
