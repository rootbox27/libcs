#ifndef _COMPLEX_H
#define _COMPLEX_H
#include <features.h>
__BEGIN_DECLS

#define complex _Complex
#ifdef __GNUC__
#define _Complex_I (__extension__ 1.0iF)
#else
#define _Complex_I 1.0iF
#endif
#define I _Complex_I

#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 5)
#define CMPLX(x, y) __builtin_complex((double)(x), (double)(y))
#define CMPLXF(x, y) __builtin_complex((float)(x), (float)(y))
#define CMPLXL(x, y) __builtin_complex((long double)(x), (long double)(y))
#endif

#define __CITADEL_C1(name) \
	double _Complex name(double _Complex); \
	float _Complex name##f(float _Complex); \
	long double _Complex name##l(long double _Complex);
#define __CITADEL_CR(name) \
	double name(double _Complex); \
	float name##f(float _Complex); \
	long double name##l(long double _Complex);

__CITADEL_C1(cacos)
__CITADEL_C1(casin)
__CITADEL_C1(catan)
__CITADEL_C1(ccos)
__CITADEL_C1(csin)
__CITADEL_C1(ctan)
__CITADEL_C1(cacosh)
__CITADEL_C1(casinh)
__CITADEL_C1(catanh)
__CITADEL_C1(ccosh)
__CITADEL_C1(csinh)
__CITADEL_C1(ctanh)
__CITADEL_C1(cexp)
__CITADEL_C1(clog)
__CITADEL_C1(csqrt)
__CITADEL_C1(conj)
__CITADEL_C1(cproj)
__CITADEL_CR(cabs)
__CITADEL_CR(carg)
__CITADEL_CR(cimag)
__CITADEL_CR(creal)
double _Complex cpow(double _Complex, double _Complex);
float _Complex cpowf(float _Complex, float _Complex);
long double _Complex cpowl(long double _Complex, long double _Complex);

#undef __CITADEL_C1
#undef __CITADEL_CR

#ifdef __GNUC__
#define creal(z) ((double)__real__(z))
#define crealf(z) ((float)__real__(z))
#define creall(z) ((long double)__real__(z))
#define cimag(z) ((double)__imag__(z))
#define cimagf(z) ((float)__imag__(z))
#define cimagl(z) ((long double)__imag__(z))
#endif

__END_DECLS
#endif
