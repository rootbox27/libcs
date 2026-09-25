/* crypt: SHA-256/SHA-512 crypt ($5$, $6$; Drepper's specification) and
 * bcrypt ($2a$, $2b$, $2y$). Legacy DES and MD5 hashes are not
 * supported. On failure the result is "*0" (or "*1" when the setting
 * itself starts with "*0"), a string that can never match a real hash,
 * following libxcrypt, and errno is EINVAL. Passphrases of 512 bytes or
 * more are refused: SHA-crypt's cost grows with their length. */
#include <crypt.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sha2.h"
#include "blowfish_init.h"

#define MAX_PASSPHRASE 512

static const char b64[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

/* ---- SHA-crypt ---- */

struct hashfn {
	size_t len;
	void (*init)(void *);
	void (*update)(void *, const void *, size_t);
	void (*final)(void *, unsigned char *);
};

static void i256(void *c) { __sha256_init(c); }
static void u256(void *c, const void *p, size_t n) { __sha256_update(c, p, n); }
static void f256(void *c, unsigned char *o) { __sha256_final(c, o); }
static void i512(void *c) { __sha512_init(c); }
static void u512(void *c, const void *p, size_t n) { __sha512_update(c, p, n); }
static void f512(void *c, unsigned char *o) { __sha512_final(c, o); }

static const struct hashfn sha256fn = { 32, i256, u256, f256 };
static const struct hashfn sha512fn = { 64, i512, u512, f512 };

/* encode 3 bytes (b2 most significant) as n characters, low bits first */
static char *b64_24(char *o, unsigned b2, unsigned b1, unsigned b0, int n)
{
	unsigned w = b2 << 16 | b1 << 8 | b0;
	while (n--) {
		*o++ = b64[w & 63];
		w >>= 6;
	}
	return o;
}

static char *sha_crypt(const char *key, const char *setting, char *out, int is512)
{
	const struct hashfn *H = is512 ? &sha512fn : &sha256fn;
	const char *prefix = is512 ? "$6$" : "$5$";
	size_t hl = H->len;
	union { struct sha256 a; struct sha512 b; } ctx, alt;
	unsigned char A[64], B[64], DP[64], DS[64], C[64];
	unsigned long rounds = 5000;
	int custom = 0;

	const char *s = setting + 3;
	if (!strncmp(s, "rounds=", 7)) {
		char *e;
		const char *num = s + 7;
		if (*num < '0' || *num > '9')
			return 0;
		unsigned long r = strtoul(num, &e, 10);
		if (*e != '$')
			return 0;
		if (r < 1000 || r > 999999999)
			return 0; /* out of range: refused, as libxcrypt does */
		rounds = r;
		custom = 1;
		s = e + 1;
	}
	size_t sl = strcspn(s, "$");
	if (sl > 16)
		sl = 16;
	for (size_t i = 0; i < sl; i++)
		if (s[i] == ':' || s[i] == '\n' || s[i] == '*' || s[i] == '!')
			return 0;
	size_t kl = strlen(key);
	if (kl >= MAX_PASSPHRASE) /* 512 with the NUL, as libxcrypt */
		return 0;

	/* B = H(P S P) */
	H->init(&alt);
	H->update(&alt, key, kl);
	H->update(&alt, s, sl);
	H->update(&alt, key, kl);
	H->final(&alt, B);
	/* A = H(P S B-repeated bits-of-len) */
	H->init(&ctx);
	H->update(&ctx, key, kl);
	H->update(&ctx, s, sl);
	size_t n;
	for (n = kl; n > hl; n -= hl)
		H->update(&ctx, B, hl);
	H->update(&ctx, B, n);
	for (n = kl; n; n >>= 1)
		H->update(&ctx, (n & 1) ? (const void *)B : (const void *)key, (n & 1) ? hl : kl);
	H->final(&ctx, A);
	/* DP = H(P repeated |P| times), DS = H(S repeated 16 + A[0] times) */
	H->init(&alt);
	for (size_t i = 0; i < kl; i++)
		H->update(&alt, key, kl);
	H->final(&alt, DP);
	H->init(&alt);
	for (unsigned i = 0; i < 16u + A[0]; i++)
		H->update(&alt, s, sl);
	H->final(&alt, DS);
	unsigned char pseq[MAX_PASSPHRASE], sseq[16];
	for (size_t i = 0; i < kl; i++)
		pseq[i] = DP[i % hl];
	for (size_t i = 0; i < sl; i++)
		sseq[i] = DS[i % hl];

	memcpy(C, A, hl);
	for (unsigned long r = 0; r < rounds; r++) {
		H->init(&ctx);
		if (r & 1)
			H->update(&ctx, pseq, kl);
		else
			H->update(&ctx, C, hl);
		if (r % 3)
			H->update(&ctx, sseq, sl);
		if (r % 7)
			H->update(&ctx, pseq, kl);
		if (r & 1)
			H->update(&ctx, C, hl);
		else
			H->update(&ctx, pseq, kl);
		H->final(&ctx, C);
	}

	char *o = out;
	o += sprintf(o, "%s", prefix);
	if (custom)
		o += sprintf(o, "rounds=%lu$", rounds);
	memcpy(o, s, sl);
	o += sl;
	*o++ = '$';
	if (is512) {
		static const unsigned char perm[21][3] = {
			{ 0, 21, 42 }, { 22, 43, 1 }, { 44, 2, 23 }, { 3, 24, 45 }, { 25, 46, 4 }, { 47, 5, 26 },
			{ 6, 27, 48 }, { 28, 49, 7 }, { 50, 8, 29 }, { 9, 30, 51 }, { 31, 52, 10 }, { 53, 11, 32 },
			{ 12, 33, 54 }, { 34, 55, 13 }, { 56, 14, 35 }, { 15, 36, 57 }, { 37, 58, 16 }, { 59, 17, 38 },
			{ 18, 39, 60 }, { 40, 61, 19 }, { 62, 20, 41 },
		};
		for (int i = 0; i < 21; i++)
			o = b64_24(o, C[perm[i][0]], C[perm[i][1]], C[perm[i][2]], 4);
		o = b64_24(o, 0, 0, C[63], 2);
	} else {
		static const unsigned char perm[10][3] = {
			{ 0, 10, 20 }, { 21, 1, 11 }, { 12, 22, 2 }, { 3, 13, 23 }, { 24, 4, 14 },
			{ 15, 25, 5 }, { 6, 16, 26 }, { 27, 7, 17 }, { 18, 28, 8 }, { 9, 19, 29 },
		};
		for (int i = 0; i < 10; i++)
			o = b64_24(o, C[perm[i][0]], C[perm[i][1]], C[perm[i][2]], 4);
		o = b64_24(o, 0, C[31], C[30], 3);
	}
	*o = 0;
	/* scrub the intermediate state */
	explicit_bzero(&ctx, sizeof ctx);
	explicit_bzero(&alt, sizeof alt);
	explicit_bzero(A, sizeof A);
	explicit_bzero(B, sizeof B);
	explicit_bzero(C, sizeof C);
	explicit_bzero(DP, sizeof DP);
	explicit_bzero(DS, sizeof DS);
	explicit_bzero(pseq, sizeof pseq);
	return out;
}

/* ---- bcrypt ---- */

static const char bfb64[] = "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

struct bf {
	uint32_t P[18];
	uint32_t S[4][256];
};

static uint32_t bf_f(const struct bf *c, uint32_t x)
{
	return ((c->S[0][x >> 24] + c->S[1][(x >> 16) & 0xff]) ^ c->S[2][(x >> 8) & 0xff]) + c->S[3][x & 0xff];
}

static void bf_encrypt(const struct bf *c, uint32_t *l, uint32_t *r)
{
	uint32_t L = *l, R = *r;
	for (int i = 0; i < 16; i += 2) {
		L ^= c->P[i];
		R ^= bf_f(c, L);
		R ^= c->P[i + 1];
		L ^= bf_f(c, R);
	}
	L ^= c->P[16];
	R ^= c->P[17];
	*l = R;
	*r = L;
}

static uint32_t stream_word(const unsigned char *d, size_t n, size_t *j)
{
	uint32_t w = 0;
	for (int i = 0; i < 4; i++) {
		w = w << 8 | d[*j];
		*j = (*j + 1) % n;
	}
	return w;
}

/* expand with key, xoring salt into the blocks (salt may be 0) */
static void bf_expand(struct bf *c, const unsigned char *salt, const unsigned char *key, size_t kl)
{
	size_t j = 0;
	for (int i = 0; i < 18; i++)
		c->P[i] ^= stream_word(key, kl, &j);
	uint32_t L = 0, R = 0;
	j = 0;
	for (int i = 0; i < 18; i += 2) {
		if (salt) {
			L ^= stream_word(salt, 16, &j);
			R ^= stream_word(salt, 16, &j);
		}
		bf_encrypt(c, &L, &R);
		c->P[i] = L;
		c->P[i + 1] = R;
	}
	for (int b = 0; b < 4; b++) {
		for (int i = 0; i < 256; i += 2) {
			if (salt) {
				L ^= stream_word(salt, 16, &j);
				R ^= stream_word(salt, 16, &j);
			}
			bf_encrypt(c, &L, &R);
			c->S[b][i] = L;
			c->S[b][i + 1] = R;
		}
	}
}

/* decode radix-64 (bcrypt alphabet): 4 characters per 3 bytes, a final
 * partial group of 2 or 3 characters for 1 or 2 bytes */
static int bf_decode(unsigned char *out, const char *in, size_t outlen)
{
	size_t o = 0;
	while (o < outlen) {
		size_t left = outlen - o;
		int need = left >= 3 ? 4 : (int)left + 1, c[4] = { 0 };
		for (int i = 0; i < need; i++) {
			const char *p = in[i] ? strchr(bfb64, in[i]) : 0;
			if (!p)
				return -1;
			c[i] = (int)(p - bfb64);
		}
		in += need;
		out[o++] = (unsigned char)(c[0] << 2 | c[1] >> 4);
		if (need >= 3)
			out[o++] = (unsigned char)(c[1] << 4 | c[2] >> 2);
		if (need == 4)
			out[o++] = (unsigned char)(c[2] << 6 | c[3]);
	}
	return 0;
}

static char *bf_encode(char *o, const unsigned char *in, size_t n)
{
	for (size_t i = 0; i < n; i += 3) {
		unsigned c1 = in[i];
		*o++ = bfb64[c1 >> 2];
		c1 = (c1 & 3) << 4;
		if (i + 1 >= n) {
			*o++ = bfb64[c1];
			break;
		}
		unsigned c2 = in[i + 1];
		*o++ = bfb64[c1 | c2 >> 4];
		c1 = (c2 & 15) << 2;
		if (i + 2 >= n) {
			*o++ = bfb64[c1];
			break;
		}
		c2 = in[i + 2];
		*o++ = bfb64[c1 | c2 >> 6];
		*o++ = bfb64[c2 & 63];
	}
	return o;
}

static char *bcrypt(const char *key, const char *setting, char *out)
{
	char minor = setting[2];
	if ((minor != 'a' && minor != 'b' && minor != 'y') || setting[3] != '$')
		return 0;
	if (setting[4] < '0' || setting[4] > '3' || setting[5] < '0' || setting[5] > '9' || setting[6] != '$')
		return 0;
	int cost = (setting[4] - '0') * 10 + (setting[5] - '0');
	if (cost < 4 || cost > 31)
		return 0;
	unsigned char salt[16];
	if (bf_decode(salt, setting + 7, 16) < 0)
		return 0;
	size_t kl = strlen(key);
	if (kl >= MAX_PASSPHRASE) /* 512 with the NUL, as libxcrypt */
		return 0;
	/* at most 72 bytes, with the terminating NUL when shorter; $2a$ is
	 * treated as $2b$, as libxcrypt does (no historical length wrap) */
	kl = (kl > 72 ? 72 : kl) + 1;

	struct bf *c = malloc(sizeof *c);
	if (!c)
		return 0;
	memcpy(c->P, bf_init_p, sizeof c->P);
	memcpy(c->S, bf_init_s, sizeof c->S);
	const unsigned char *k = (const unsigned char *)key;
	bf_expand(c, salt, k, kl);
	for (uint64_t r = (uint64_t)1 << cost; r; r--) {
		bf_expand(c, 0, k, kl);
		bf_expand(c, 0, salt, 16);
	}
	static const char magic[] = "OrpheanBeholderScryDoubt";
	uint32_t cd[6];
	size_t j = 0;
	for (int i = 0; i < 6; i++)
		cd[i] = stream_word((const unsigned char *)magic, 24, &j);
	for (int i = 0; i < 64; i++)
		for (int b = 0; b < 6; b += 2)
			bf_encrypt(c, &cd[b], &cd[b + 1]);
	unsigned char hash[24];
	for (int i = 0; i < 6; i++)
		for (int b = 0; b < 4; b++)
			hash[4 * i + b] = (unsigned char)(cd[i] >> (24 - 8 * b));
	explicit_bzero(c, sizeof *c);
	free(c);

	char *o = out;
	memcpy(o, setting, 7);
	o += 7;
	o = bf_encode(o, salt, 16);
	o = bf_encode(o, hash, 23);
	*o = 0;
	explicit_bzero(hash, sizeof hash);
	explicit_bzero(cd, sizeof cd);
	return out;
}

/* ---- entry points ---- */

char *crypt_r(const char *key, const char *setting, struct crypt_data *data)
{
	char *out = data->output;
	char *r = 0;
	if (!strncmp(setting, "$6$", 3))
		r = sha_crypt(key, setting, out, 1);
	else if (!strncmp(setting, "$5$", 3))
		r = sha_crypt(key, setting, out, 0);
	else if (setting[0] == '$' && setting[1] == '2')
		r = bcrypt(key, setting, out);
	if (!r) {
		strcpy(out, setting[0] == '*' && setting[1] == '0' ? "*1" : "*0");
		errno = EINVAL;
	}
	return out;
}

char *crypt(const char *key, const char *setting)
{
	static struct crypt_data data;
	return crypt_r(key, setting, &data);
}
