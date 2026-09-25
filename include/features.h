#ifndef _FEATURES_H
#define _FEATURES_H
#define __CITADEL__ 1
#define __CITADEL_VERSION__ "0.1.0"
#ifdef __cplusplus
#define __BEGIN_DECLS extern "C" {
#define __END_DECLS }
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define __restrict restrict
#elif !defined(__GNUC__)
#define __restrict
#endif
#define __noreturn __attribute__((__noreturn__))
#define __printflike(f,a) __attribute__((__format__(__printf__,f,a)))
#define __scanflike(f,a) __attribute__((__format__(__scanf__,f,a)))
#define __wur __attribute__((__warn_unused_result__))
/* Fortification is active when requested and optimizing. */
#if defined(_FORTIFY_SOURCE) && _FORTIFY_SOURCE > 0 && defined(__OPTIMIZE__) && __OPTIMIZE__ > 0 && !defined(__CITADEL_BUILDING__)
#define __CITADEL_FORTIFY 1
#if _FORTIFY_SOURCE >= 3 && defined(__has_builtin)
#if __has_builtin(__builtin_dynamic_object_size)
#define __bos(p) __builtin_dynamic_object_size(p, 1)
#define __bos0(p) __builtin_dynamic_object_size(p, 0)
#endif
#endif
#ifndef __bos
#define __bos(p) __builtin_object_size(p, _FORTIFY_SOURCE > 1)
#define __bos0(p) __builtin_object_size(p, 0)
#endif
#define __fortify_inline extern __inline__ __attribute__((__always_inline__, __gnu_inline__, __artificial__))
#endif
#endif
