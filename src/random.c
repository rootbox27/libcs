/* Random numbers: rand/srand/rand_r, random/srandom, and arc4random.
 *
 * rand and random are the classic deterministic generators (a 64-bit LCG,
 * reporting the high bits); they must not be used for anything secret.
 *
 * arc4random is a ChaCha20-based CSPRNG seeded from getrandom(2), with
 * fast key erasure: every refill overwrites the key with fresh output, so
 * a later state compromise cannot reveal earlier results. Its state is in
 * a page marked MADV_WIPEONFORK (a forked child sees zeros and reseeds)
 * and MADV_DONTDUMP (kept out of core dumps). */
#include "internal.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* ---- rand, random ---- */

static uint64_t rand_seed = 1;
static volatile int rand_lock;

void srand(unsigned s)
{
	LOCK(rand_lock);
	rand_seed = s - 1;
	UNLOCK(rand_lock);
}

int rand(void)
{
	LOCK(rand_lock);
	rand_seed = 6364136223846793005ULL * rand_seed + 1;
	int r = (int)(rand_seed >> 33);
	UNLOCK(rand_lock);
	return r;
}

int rand_r(unsigned *seed)
{
	/* 32-bit state: LCG plus an output permutation */
	unsigned x = *seed = *seed * 1103515245 + 12345;
	x ^= x >> 11;
	x ^= x << 7 & 0x9d2c5680;
	x ^= x << 15 & 0xefc60000;
	x ^= x >> 18;
	return (int)(x >> 1);
}

static uint64_t random_seed = 1;

void srandom(unsigned s)
{
	LOCK(rand_lock);
	random_seed = s;
	UNLOCK(rand_lock);
}

long random(void)
{
	LOCK(rand_lock);
	random_seed = 6364136223846793005ULL * random_seed + 1442695040888963407ULL;
	long r = (long)(random_seed >> 33);
	UNLOCK(rand_lock);
	return r;
}

/* ---- arc4random ---- */

#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define QR(a, b, c, d) \
	(a += b, d ^= a, d = ROTL(d, 16), c += d, b ^= c, b = ROTL(b, 12), \
	 a += b, d ^= a, d = ROTL(d, 8), c += d, b ^= c, b = ROTL(b, 7))

static void chacha_block(const uint32_t in[16], uint32_t out[16])
{
	uint32_t x[16];
	memcpy(x, in, sizeof x);
	for (int i = 0; i < 10; i++) {
		QR(x[0], x[4], x[8], x[12]);
		QR(x[1], x[5], x[9], x[13]);
		QR(x[2], x[6], x[10], x[14]);
		QR(x[3], x[7], x[11], x[15]);
		QR(x[0], x[5], x[10], x[15]);
		QR(x[1], x[6], x[11], x[12]);
		QR(x[2], x[7], x[8], x[13]);
		QR(x[3], x[4], x[9], x[14]);
	}
	for (int i = 0; i < 16; i++)
		out[i] = x[i] + in[i];
}

#define KEYSZ 32
#define IVSZ 8
#define BLOCKS 16
#define BUFSZ (64 * BLOCKS)
#define RESEED_BYTES (1600000)

struct rs {
	int ready;                 /* zero after fork (WIPEONFORK) */
	pid_t pid;                 /* fallback fork check if madvise failed */
	size_t have;               /* unused bytes at the end of buf */
	size_t until_reseed;
	uint32_t in[16];
	unsigned char buf[BUFSZ];
};

static struct rs *rs;
static struct rs rs_static;
static int rs_wipe;                /* outside the page: survives the wipe */
static volatile int rs_lock;

static void rs_setkey(const unsigned char *kiv)
{
	static const char sigma[16] = "expand 32-byte k";
	memcpy(rs->in, sigma, 16);
	memcpy(rs->in + 4, kiv, KEYSZ);
	rs->in[12] = rs->in[13] = 0; /* block counter */
	memcpy(rs->in + 14, kiv + KEYSZ, IVSZ);
}

/* Generate a fresh buffer and immediately re-key from its start. */
static void rs_refill(void)
{
	for (int i = 0; i < BLOCKS; i++) {
		chacha_block(rs->in, (uint32_t *)(rs->buf + 64 * i));
		if (!++rs->in[12])
			rs->in[13]++;
	}
	rs_setkey(rs->buf);
	explicit_bzero(rs->buf, KEYSZ + IVSZ);
	rs->have = BUFSZ - KEYSZ - IVSZ;
}

static void rs_stir(void)
{
	unsigned char seed[KEYSZ + IVSZ];
	__secure_random(seed, sizeof seed);
	if (!rs->ready) {
		rs_setkey(seed);
	} else {
		/* mix new entropy into the existing key */
		for (size_t i = 0; i < KEYSZ / 4; i++) {
			uint32_t v;
			memcpy(&v, seed + 4 * i, 4);
			rs->in[4 + i] ^= v;
		}
	}
	explicit_bzero(seed, sizeof seed);
	rs->have = 0;
	rs->until_reseed = RESEED_BYTES;
	rs->ready = 1;
	rs->pid = (pid_t)__sys(SYS_getpid);
	rs_refill();
}

static void rs_prepare(size_t n)
{
	if (!rs) {
		void *p = mmap(0, sizeof *rs, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (p != MAP_FAILED) {
			rs = p;
			rs_wipe = !madvise(p, sizeof *rs, MADV_WIPEONFORK);
			madvise(p, sizeof *rs, MADV_DONTDUMP);
		} else {
			rs = &rs_static;
		}
	}
	if (!rs->ready || (!rs_wipe && rs->pid != (pid_t)__sys(SYS_getpid)))
		rs_stir();
	if (rs->until_reseed <= n)
		rs_stir();
	else
		rs->until_reseed -= n;
}

void arc4random_buf(void *out, size_t n)
{
	unsigned char *o = out;
	LOCK(rs_lock);
	rs_prepare(n);
	while (n) {
		if (!rs->have)
			rs_refill();
		size_t k = n < rs->have ? n : rs->have;
		unsigned char *src = rs->buf + BUFSZ - rs->have;
		memcpy(o, src, k);
		explicit_bzero(src, k); /* never hand out the same bytes twice */
		o += k;
		n -= k;
		rs->have -= k;
	}
	UNLOCK(rs_lock);
}

uint32_t arc4random(void)
{
	uint32_t v;
	arc4random_buf(&v, sizeof v);
	return v;
}

/* Uniform in [0, bound) without modulo bias. */
uint32_t arc4random_uniform(uint32_t bound)
{
	if (bound < 2)
		return 0;
	uint32_t min = -bound % bound; /* 2^32 mod bound */
	for (;;) {
		uint32_t r = arc4random();
		if (r >= min)
			return r % bound;
	}
}
