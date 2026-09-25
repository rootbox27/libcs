/* Single-precision transcendental functions: evaluated with the double
 * versions (error at most about 0.52 ulp of a double) and rounded once to
 * float, which is correctly rounded except within ~2^-29 of a midpoint.
 * Overflow, underflow and inexact come from the final conversion or from
 * the double computation, as appropriate. */
#include "libm.h"
float expf(float x) { return (float)exp((double)x); }
float exp2f(float x) { return (float)exp2((double)x); }
float expm1f(float x) { return (float)expm1((double)x); }
float logf(float x) { return (float)log((double)x); }
float log2f(float x) { return (float)log2((double)x); }
float log10f(float x) { return (float)log10((double)x); }
float log1pf(float x) { return (float)log1p((double)x); }
float sinf(float x) { return (float)sin((double)x); }
float cosf(float x) { return (float)cos((double)x); }
float tanf(float x) { return (float)tan((double)x); }
float asinf(float x) { return (float)asin((double)x); }
float acosf(float x) { return (float)acos((double)x); }
float atanf(float x) { return (float)atan((double)x); }
float sinhf(float x) { return (float)sinh((double)x); }
float coshf(float x) { return (float)cosh((double)x); }
float tanhf(float x) { return (float)tanh((double)x); }
float asinhf(float x) { return (float)asinh((double)x); }
float acoshf(float x) { return (float)acosh((double)x); }
float atanhf(float x) { return (float)atanh((double)x); }
float cbrtf(float x) { return (float)cbrt((double)x); }
float powf(float x, float y) { return (float)pow((double)x, (double)y); }
float atan2f(float x, float y) { return (float)atan2((double)x, (double)y); }
float hypotf(float x, float y) { return (float)hypot((double)x, (double)y); }
