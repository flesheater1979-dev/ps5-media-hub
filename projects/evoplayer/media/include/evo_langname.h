/*
 * evo_langname - a language's own name for its ISO 639 code (#128).
 *
 * "rus" -> "Русский", "ara" -> "العربية", "jpn" -> "日本語". Track pickers show
 * this instead of the bare code. UTF-8.
 */
#ifndef EVO_LANGNAME_H
#define EVO_LANGNAME_H

#ifdef __cplusplus
extern "C" {
#endif

/* The native name for a 639-1 ("ru"), 639-2/T ("rus") or 639-2/B ("fre") code,
 * any case; NULL when the code is not known (callers keep showing the code). */
const char *evo_language_native_name(const char *code);

#ifdef __cplusplus
}
#endif

#endif
