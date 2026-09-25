/* <complex.h>. The long double functions come from complex_impl.h. The
 * double ones evaluate in long double and round once, which makes them
 * nearly correctly rounded (the long double errors of a few units in the
 * last place shrink by 2^11); the float ones do the same through double. */
#include <complex.h>
#include <float.h>
#include <math.h>

/* the functions themselves, not the header's macros */
#undef creal
#undef crealf
#undef creall
#undef cimag
#undef cimagf
#undef cimagl

#define T long double
#define CT long double _Complex
#define F(n) n##l
#define C(n) c##n##l
#define T_EPS LDBL_EPSILON
#define T_MAX LDBL_MAX
#define T_MANT LDBL_MANT_DIG
#define T_MIN_EXP LDBL_MIN_EXP
#define T_MAX_EXP LDBL_MAX_EXP
#define LOG_MAX 11356.523406294143949L
#define SPLIT (0x1p32L + 1)
#define SCALE_BIG 0x1p8000L
#define TANH_BIG 25
#define LN2_HI 0xb17217f7d1000000p-64L
#define LN2_LO 0xcf79abc9e3b39804p-104L
#define PIO2_HI 0xc90fdaa22168c235p-63L
#define PIO2_LO (-0xece675d1fc8f8cbbp-129L)
#define M_E_ 0xadf85458a2bb4a9bp-62L
#include "complex_impl.h"

#define DOUBLE1(name) \
	double _Complex name(double _Complex z) { return (double _Complex)name##l((long double _Complex)z); }
DOUBLE1(cexp)
DOUBLE1(clog)
DOUBLE1(csqrt)
DOUBLE1(csin)
DOUBLE1(ccos)
DOUBLE1(ctan)
DOUBLE1(csinh)
DOUBLE1(ccosh)
DOUBLE1(ctanh)
DOUBLE1(casin)
DOUBLE1(cacos)
DOUBLE1(catan)
DOUBLE1(casinh)
DOUBLE1(cacosh)
DOUBLE1(catanh)

double _Complex cpow(double _Complex z, double _Complex w)
{
	return (double _Complex)cpowl((long double _Complex)z, (long double _Complex)w);
}

double _Complex cproj(double _Complex z)
{
	if (__builtin_isinf(__real__ z) || __builtin_isinf(__imag__ z))
		return __builtin_complex(__builtin_inf(), __builtin_copysign(0.0, __imag__ z));
	return z;
}

double cabs(double _Complex z)
{
	return hypot(__real__ z, __imag__ z);
}

double carg(double _Complex z)
{
	return atan2(__imag__ z, __real__ z);
}

double creal(double _Complex z)
{
	return __real__ z;
}

double cimag(double _Complex z)
{
	return __imag__ z;
}

double _Complex conj(double _Complex z)
{
	return __builtin_complex(__real__ z, -__imag__ z);
}

#define FLOAT1(name) \
	float _Complex name##f(float _Complex z) { return (float _Complex)name((double _Complex)z); }
FLOAT1(cexp)
FLOAT1(clog)
FLOAT1(csqrt)
FLOAT1(csin)
FLOAT1(ccos)
FLOAT1(ctan)
FLOAT1(csinh)
FLOAT1(ccosh)
FLOAT1(ctanh)
FLOAT1(casin)
FLOAT1(cacos)
FLOAT1(catan)
FLOAT1(casinh)
FLOAT1(cacosh)
FLOAT1(catanh)
FLOAT1(cproj)

float _Complex cpowf(float _Complex z, float _Complex w)
{
	return (float _Complex)cpow((double _Complex)z, (double _Complex)w);
}

float cabsf(float _Complex z)
{
	return hypotf(__real__ z, __imag__ z);
}

float cargf(float _Complex z)
{
	return atan2f(__imag__ z, __real__ z);
}

float crealf(float _Complex z)
{
	return __real__ z;
}

float cimagf(float _Complex z)
{
	return __imag__ z;
}

float _Complex conjf(float _Complex z)
{
	return __builtin_complex(__real__ z, -__imag__ z);
}
