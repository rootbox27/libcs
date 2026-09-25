#ifndef _MATH_H
#define _MATH_H
#include <features.h>
__BEGIN_DECLS
typedef float float_t;
typedef double double_t;
#define NAN __builtin_nanf("")
#define INFINITY __builtin_inff()
#define HUGE_VAL __builtin_huge_val()
#define HUGE_VALF __builtin_huge_valf()
#define HUGE_VALL __builtin_huge_vall()
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define MATH_ERRNO 1
#define MATH_ERREXCEPT 2
#define math_errhandling 2
#define FP_ILOGB0 (-2147483647 - 1)
#define FP_ILOGBNAN (-2147483647 - 1)
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf_sign(x)
#define isfinite(x) __builtin_isfinite(x)
#define isnormal(x) __builtin_isnormal(x)
#define signbit(x) __builtin_signbit(x)
#define isgreater(x,y) __builtin_isgreater(x,y)
#define isgreaterequal(x,y) __builtin_isgreaterequal(x,y)
#define isless(x,y) __builtin_isless(x,y)
#define islessequal(x,y) __builtin_islessequal(x,y)
#define islessgreater(x,y) __builtin_islessgreater(x,y)
#define isunordered(x,y) __builtin_isunordered(x,y)
#define M_E 2.7182818284590452354
#define M_LOG2E 1.4426950408889634074
#define M_LOG10E 0.43429448190325182765
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_PI_4 0.78539816339744830962
#define M_1_PI 0.31830988618379067154
#define M_2_PI 0.63661977236758134308
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2 1.41421356237309504880
#define M_SQRT1_2 0.70710678118654752440
#define __CITADEL_MATH3(name) double name(double); float name##f(float); long double name##l(long double);
#define __CITADEL_MATH3_2(name) double name(double, double); float name##f(float, float); long double name##l(long double, long double);
__CITADEL_MATH3(fabs) __CITADEL_MATH3(sqrt) __CITADEL_MATH3(cbrt)
__CITADEL_MATH3(floor) __CITADEL_MATH3(ceil) __CITADEL_MATH3(trunc) __CITADEL_MATH3(round)
__CITADEL_MATH3(rint) __CITADEL_MATH3(nearbyint)
__CITADEL_MATH3(exp) __CITADEL_MATH3(exp2) __CITADEL_MATH3(expm1)
__CITADEL_MATH3(log) __CITADEL_MATH3(log2) __CITADEL_MATH3(log10) __CITADEL_MATH3(log1p)
__CITADEL_MATH3(sin) __CITADEL_MATH3(cos) __CITADEL_MATH3(tan)
__CITADEL_MATH3(asin) __CITADEL_MATH3(acos) __CITADEL_MATH3(atan)
__CITADEL_MATH3(sinh) __CITADEL_MATH3(cosh) __CITADEL_MATH3(tanh)
__CITADEL_MATH3(asinh) __CITADEL_MATH3(acosh) __CITADEL_MATH3(atanh)
__CITADEL_MATH3(logb)
__CITADEL_MATH3_2(pow) __CITADEL_MATH3_2(atan2) __CITADEL_MATH3_2(fmod) __CITADEL_MATH3_2(hypot)
__CITADEL_MATH3_2(fmin) __CITADEL_MATH3_2(fmax) __CITADEL_MATH3_2(fdim) __CITADEL_MATH3_2(copysign)
__CITADEL_MATH3_2(remainder) __CITADEL_MATH3_2(nextafter)
__CITADEL_MATH3(erf) __CITADEL_MATH3(erfc) __CITADEL_MATH3(lgamma) __CITADEL_MATH3(tgamma)
double fma(double, double, double); float fmaf(float, float, float); long double fmal(long double, long double, long double);
double remquo(double, double, int *); float remquof(float, float, int *); long double remquol(long double, long double, int *);
double scalbln(double, long); float scalblnf(float, long); long double scalblnl(long double, long);
double nexttoward(double, long double); float nexttowardf(float, long double); long double nexttowardl(long double, long double);
double lgamma_r(double, int *); float lgammaf_r(float, int *); long double lgammal_r(long double, int *);
extern int signgam;
double frexp(double, int *); float frexpf(float, int *); long double frexpl(long double, int *);
double ldexp(double, int); float ldexpf(float, int); long double ldexpl(long double, int);
double scalbn(double, int); float scalbnf(float, int); long double scalbnl(long double, int);
double modf(double, double *); float modff(float, float *); long double modfl(long double, long double *);
int ilogb(double); int ilogbf(float); int ilogbl(long double);
long lround(double); long lroundf(float); long lroundl(long double);
long long llround(double); long long llroundf(float); long long llroundl(long double);
long lrint(double); long lrintf(float); long lrintl(long double);
long long llrint(double); long long llrintf(float); long long llrintl(long double);
double nan(const char *); float nanf(const char *); long double nanl(const char *);
#undef __CITADEL_MATH3
#undef __CITADEL_MATH3_2
__END_DECLS
#endif
