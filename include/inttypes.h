#ifndef _INTTYPES_H
#define _INTTYPES_H
#include <features.h>
#include <stdint.h>
#define __need_wchar_t
#include <stddef.h>
__BEGIN_DECLS
typedef struct { intmax_t quot, rem; } imaxdiv_t;
intmax_t imaxabs(intmax_t);
imaxdiv_t imaxdiv(intmax_t, intmax_t);
intmax_t strtoimax(const char *__restrict, char **__restrict, int);
uintmax_t strtoumax(const char *__restrict, char **__restrict, int);
__END_DECLS
#define __P64 "l"
#define PRId8 "d"
#define PRId16 "d"
#define PRId32 "d"
#define PRId64 __P64 "d"
#define PRIi8 "i"
#define PRIi16 "i"
#define PRIi32 "i"
#define PRIi64 __P64 "i"
#define PRIu8 "u"
#define PRIu16 "u"
#define PRIu32 "u"
#define PRIu64 __P64 "u"
#define PRIx8 "x"
#define PRIx16 "x"
#define PRIx32 "x"
#define PRIx64 __P64 "x"
#define PRIX8 "X"
#define PRIX16 "X"
#define PRIX32 "X"
#define PRIX64 __P64 "X"
#define PRIo8 "o"
#define PRIo16 "o"
#define PRIo32 "o"
#define PRIo64 __P64 "o"
#define PRIdMAX __P64 "d"
#define PRIiMAX __P64 "i"
#define PRIuMAX __P64 "u"
#define PRIxMAX __P64 "x"
#define PRIXMAX __P64 "X"
#define PRIoMAX __P64 "o"
#define PRIdPTR __P64 "d"
#define PRIiPTR __P64 "i"
#define PRIuPTR __P64 "u"
#define PRIxPTR __P64 "x"
#define PRIXPTR __P64 "X"
#define PRIoPTR __P64 "o"
#define PRIdLEAST64 PRId64
#define PRIuLEAST64 PRIu64
#define PRIdFAST64 PRId64
#define PRIuFAST64 PRIu64
#define SCNd8 "hhd"
#define SCNd16 "hd"
#define SCNd32 "d"
#define SCNd64 __P64 "d"
#define SCNu8 "hhu"
#define SCNu16 "hu"
#define SCNu32 "u"
#define SCNu64 __P64 "u"
#define SCNx32 "x"
#define SCNx64 __P64 "x"
#define SCNi32 "i"
#define SCNi64 __P64 "i"
#define SCNdMAX __P64 "d"
#define SCNuMAX __P64 "u"
#endif
