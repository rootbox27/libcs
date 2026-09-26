#ifndef _UCHAR_H
#define _UCHAR_H
#include <features.h>
#include <wchar.h>
__BEGIN_DECLS

#ifndef __cplusplus
typedef unsigned char char8_t;
typedef unsigned short char16_t;
typedef unsigned char32_t;
#endif

size_t mbrtoc16(char16_t *__restrict, const char *__restrict, size_t, mbstate_t *__restrict);
size_t c16rtomb(char *__restrict, char16_t, mbstate_t *__restrict);
size_t mbrtoc32(char32_t *__restrict, const char *__restrict, size_t, mbstate_t *__restrict);
size_t c32rtomb(char *__restrict, char32_t, mbstate_t *__restrict);
/* char8_t is a C23 typedef, and a keyword only from C++20 */
#if !defined(__cplusplus) || defined(__cpp_char8_t)
size_t mbrtoc8(char8_t *__restrict, const char *__restrict, size_t, mbstate_t *__restrict);
size_t c8rtomb(char *__restrict, char8_t, mbstate_t *__restrict);
#endif

__END_DECLS
#endif
