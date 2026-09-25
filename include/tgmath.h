#ifndef _TGMATH_H
#define _TGMATH_H
/* Type-generic math (real types only; <complex.h> is not provided).
 * Integer arguments select the double function, as C requires. */
#include <math.h>

#define __tg_t(x) _Generic((x), float: (float)0, long double: (long double)0, default: (double)0)
#define __TG1(fn, x) _Generic(__tg_t(x), float: fn##f, long double: fn##l, default: fn)
#define __TG2(fn, x, y) _Generic(__tg_t(x) + __tg_t(y), float: fn##f, long double: fn##l, default: fn)
#define __TG3(fn, x, y, z) _Generic(__tg_t(x) + __tg_t(y) + __tg_t(z), float: fn##f, long double: fn##l, default: fn)

#undef acos
#define acos(x) __TG1(acos, x)(x)
#undef asin
#define asin(x) __TG1(asin, x)(x)
#undef atan
#define atan(x) __TG1(atan, x)(x)
#undef acosh
#define acosh(x) __TG1(acosh, x)(x)
#undef asinh
#define asinh(x) __TG1(asinh, x)(x)
#undef atanh
#define atanh(x) __TG1(atanh, x)(x)
#undef cos
#define cos(x) __TG1(cos, x)(x)
#undef sin
#define sin(x) __TG1(sin, x)(x)
#undef tan
#define tan(x) __TG1(tan, x)(x)
#undef cosh
#define cosh(x) __TG1(cosh, x)(x)
#undef sinh
#define sinh(x) __TG1(sinh, x)(x)
#undef tanh
#define tanh(x) __TG1(tanh, x)(x)
#undef exp
#define exp(x) __TG1(exp, x)(x)
#undef log
#define log(x) __TG1(log, x)(x)
#undef sqrt
#define sqrt(x) __TG1(sqrt, x)(x)
#undef fabs
#define fabs(x) __TG1(fabs, x)(x)
#undef cbrt
#define cbrt(x) __TG1(cbrt, x)(x)
#undef ceil
#define ceil(x) __TG1(ceil, x)(x)
#undef erf
#define erf(x) __TG1(erf, x)(x)
#undef erfc
#define erfc(x) __TG1(erfc, x)(x)
#undef exp2
#define exp2(x) __TG1(exp2, x)(x)
#undef expm1
#define expm1(x) __TG1(expm1, x)(x)
#undef floor
#define floor(x) __TG1(floor, x)(x)
#undef lgamma
#define lgamma(x) __TG1(lgamma, x)(x)
#undef llrint
#define llrint(x) __TG1(llrint, x)(x)
#undef llround
#define llround(x) __TG1(llround, x)(x)
#undef log10
#define log10(x) __TG1(log10, x)(x)
#undef log1p
#define log1p(x) __TG1(log1p, x)(x)
#undef log2
#define log2(x) __TG1(log2, x)(x)
#undef logb
#define logb(x) __TG1(logb, x)(x)
#undef lrint
#define lrint(x) __TG1(lrint, x)(x)
#undef lround
#define lround(x) __TG1(lround, x)(x)
#undef nearbyint
#define nearbyint(x) __TG1(nearbyint, x)(x)
#undef rint
#define rint(x) __TG1(rint, x)(x)
#undef round
#define round(x) __TG1(round, x)(x)
#undef tgamma
#define tgamma(x) __TG1(tgamma, x)(x)
#undef trunc
#define trunc(x) __TG1(trunc, x)(x)
#undef ilogb
#define ilogb(x) __TG1(ilogb, x)(x)
#undef atan2
#define atan2(x, y) __TG2(atan2, x, y)(x, y)
#undef copysign
#define copysign(x, y) __TG2(copysign, x, y)(x, y)
#undef fdim
#define fdim(x, y) __TG2(fdim, x, y)(x, y)
#undef fmax
#define fmax(x, y) __TG2(fmax, x, y)(x, y)
#undef fmin
#define fmin(x, y) __TG2(fmin, x, y)(x, y)
#undef fmod
#define fmod(x, y) __TG2(fmod, x, y)(x, y)
#undef hypot
#define hypot(x, y) __TG2(hypot, x, y)(x, y)
#undef nextafter
#define nextafter(x, y) __TG2(nextafter, x, y)(x, y)
#undef remainder
#define remainder(x, y) __TG2(remainder, x, y)(x, y)
#undef pow
#define pow(x, y) __TG2(pow, x, y)(x, y)
#undef fma
#define fma(x, y, z) __TG3(fma, x, y, z)(x, y, z)
#undef remquo
#define remquo(x, y, q) __TG2(remquo, x, y)(x, y, q)
#undef frexp
#define frexp(x, e) __TG1(frexp, x)(x, e)
#undef ldexp
#define ldexp(x, n) __TG1(ldexp, x)(x, n)
#undef scalbn
#define scalbn(x, n) __TG1(scalbn, x)(x, n)
#undef scalbln
#define scalbln(x, n) __TG1(scalbln, x)(x, n)
#undef nexttoward
#define nexttoward(x, y) __TG1(nexttoward, x)(x, y)

#endif
