#include <features.h>
#undef assert
#ifdef NDEBUG
#define assert(x) ((void)0)
#else
#define assert(x) ((void)((x) || (__assert_fail(#x, __FILE__, __LINE__, __func__), 0)))
#endif
#ifndef _ASSERT_H
#define _ASSERT_H
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__cplusplus) && __STDC_VERSION__ < 202311L
#define static_assert _Static_assert
#endif
__BEGIN_DECLS
__noreturn void __assert_fail(const char *, const char *, int, const char *);
__END_DECLS
#endif
