/*
 * evo_charset - subtitle text of any encoding, as UTF-8 (#128).
 *
 * Sidecar .srt/.vtt files in the wild are often not UTF-8: UTF-16, Windows-1251
 * (Russian), -1256 (Arabic), -1253 (Greek), -1255 (Hebrew), TIS-620 (Thai),
 * GBK, Big5, Shift-JIS, EUC-KR. The caption path wants UTF-8.
 */
#ifndef EVO_CHARSET_H
#define EVO_CHARSET_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Returns a malloc'd, NUL-terminated UTF-8 copy of data[0..len). UTF-8 (with or
 * without a BOM) is passed through; UTF-16 with a BOM or clearly UTF-16 is
 * converted; anything else is read as the code page the language tokens in
 * `path` (".ru.srt", "Movie.chs.srt") point to, or the one its bytes look like.
 * `path` may be NULL. Returns NULL when out of memory. The caller frees it.
 * *out_name, when not NULL, gets a static name of what was detected.
 */
char *evo_charset_to_utf8(const unsigned char *data, size_t len, const char *path,
                          const char **out_name);

#ifdef __cplusplus
}
#endif

#endif
