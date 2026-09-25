/* iconv between Unicode encodings and a few single-byte charsets:
 * UTF-8, UTF-16, UTF-32 (with BOM handling; BE/LE variants), UCS-2,
 * UCS-4, WCHAR_T, ASCII, ISO-8859-1, ISO-8859-15 and CP1252.
 * "//TRANSLIT" replaces unrepresentable characters with '?', "//IGNORE"
 * drops them; either way they are counted as irreversible. */
#include <iconv.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { UTF8, ASCII, LATIN1, LATIN9, CP1252, UTF16, UTF16LE, UTF16BE, UCS2, UCS2LE, UCS2BE,
       UTF32, UTF32LE, UTF32BE, UCS4, UCS4LE, WCHAR };

static const struct { const char *name; int id; } names[] = {
	{ "utf8", UTF8 }, { "", UTF8 }, { "char", UTF8 },
	{ "ascii", ASCII }, { "usascii", ASCII }, { "ansix341968", ASCII }, { "646", ASCII },
	{ "iso88591", LATIN1 }, { "latin1", LATIN1 }, { "l1", LATIN1 }, { "iso885911987", LATIN1 },
	{ "iso885915", LATIN9 }, { "latin9", LATIN9 }, { "l9", LATIN9 },
	{ "cp1252", CP1252 }, { "windows1252", CP1252 },
	{ "utf16", UTF16 }, { "utf16le", UTF16LE }, { "utf16be", UTF16BE },
	{ "ucs2", UCS2 }, { "ucs2le", UCS2LE }, { "ucs2be", UCS2BE }, { "unicodelittle", UCS2LE }, { "unicodebig", UCS2BE },
	{ "utf32", UTF32 }, { "utf32le", UTF32LE }, { "utf32be", UTF32BE },
	{ "ucs4", UCS4 }, { "ucs4be", UCS4 }, { "ucs4le", UCS4LE }, { "wchart", WCHAR },
};

#define TRANSLIT 1
#define IGNORE 2

struct cd {
	int from, to, flags;
	int in_be;      /* UTF-16/32 input found big-endian by its BOM */
	int in_bom_done;
	int out_bom_done;
};

static int lookup(const char *s, int *flags)
{
	char n[32];
	size_t k = 0;
	for (; *s && k < sizeof n - 1; s++) {
		if (*s == '/' && s[1] == '/') {
			for (const char *f = s + 2; *f;) {
				size_t l = strcspn(f, "/,");
				if (l == 8 && !strncasecmp(f, "translit", 8))
					*flags |= TRANSLIT;
				else if (l == 6 && !strncasecmp(f, "ignore", 6))
					*flags |= IGNORE;
				f += l;
				f += strspn(f, "/,");
			}
			break;
		}
		if (*s == '-' || *s == '_' || *s == ' ')
			continue;
		n[k++] = (char)(*s >= 'A' && *s <= 'Z' ? *s + 32 : *s);
	}
	n[k] = 0;
	if (*s && !(*s == '/' && s[1] == '/'))
		return -1;
	for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
		if (!strcmp(n, names[i].name))
			return names[i].id;
	return -1;
}

iconv_t iconv_open(const char *to, const char *from)
{
	int tf = 0, ff = 0;
	int t = lookup(to, &tf), f = lookup(from, &ff);
	if (t < 0 || f < 0) {
		errno = EINVAL;
		return (iconv_t)-1;
	}
	struct cd *c = calloc(1, sizeof *c);
	if (!c)
		return (iconv_t)-1;
	c->from = f;
	c->to = t;
	c->flags = tf;
	return c;
}

int iconv_close(iconv_t cd)
{
	free(cd);
	return 0;
}

static const unsigned short cp1252_hi[32] = {
	0x20ac, 0, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017d, 0,
	0, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0, 0x017e, 0x0178,
};
static const struct { unsigned char b; unsigned short u; } latin9[8] = {
	{ 0xa4, 0x20ac }, { 0xa6, 0x0160 }, { 0xa8, 0x0161 }, { 0xb4, 0x017d },
	{ 0xb8, 0x017e }, { 0xbc, 0x0152 }, { 0xbd, 0x0153 }, { 0xbe, 0x0178 },
};

static unsigned get16(const unsigned char *p, int le) { return le ? p[0] | p[1] << 8 : p[0] << 8 | p[1]; }
static uint32_t get32(const unsigned char *p, int le)
{
	return le ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24
	          : (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Decode one character: returns bytes used, 0 for a consumed BOM with
 * no character, -1 illegal, -2 incomplete. */
static int decode(struct cd *c, const unsigned char *s, size_t n, uint32_t *out)
{
	unsigned b = s[0];
	switch (c->from) {
	case UTF8: {
		if (b < 0x80) {
			*out = b;
			return 1;
		}
		int k = b >= 0xf0 ? 4 : b >= 0xe0 ? 3 : b >= 0xc2 ? 2 : 0;
		if (!k || b > 0xf4)
			return -1;
		uint32_t v = b & (0x7f >> k);
		for (int i = 1; i < k; i++) {
			if ((size_t)i >= n)
				return -2;
			if ((s[i] & 0xc0) != 0x80)
				return -1;
			v = v << 6 | (s[i] & 0x3f);
		}
		if ((k == 3 && v < 0x800) || (k == 4 && (v < 0x10000 || v > 0x10ffff)) || (v >= 0xd800 && v <= 0xdfff))
			return -1;
		*out = v;
		return k;
	}
	case ASCII:
		if (b > 0x7f)
			return -1;
		*out = b;
		return 1;
	case LATIN1:
		*out = b;
		return 1;
	case LATIN9:
		*out = b;
		for (int i = 0; i < 8; i++)
			if (latin9[i].b == b)
				*out = latin9[i].u;
		return 1;
	case CP1252:
		if (b >= 0x80 && b < 0xa0) {
			if (!cp1252_hi[b - 0x80])
				return -1;
			*out = cp1252_hi[b - 0x80];
		} else {
			*out = b;
		}
		return 1;
	case UTF16: case UTF16LE: case UTF16BE: case UCS2: case UCS2LE: case UCS2BE: {
		if (n < 2)
			return -2;
		/* without a BOM, UTF-16 is taken as little-endian (as glibc) */
		int le = c->from == UTF16LE || c->from == UCS2LE || c->from == UCS2 || (c->from == UTF16 && !c->in_be);
		unsigned u = get16(s, le);
		if (c->from == UTF16 && !c->in_bom_done) {
			c->in_bom_done = 1;
			if (u == 0xfeff)
				return 0;
			if (u == 0xfffe) {
				c->in_be = 1;
				return 0;
			}
		}
		if (c->from == UCS2 || c->from == UCS2LE || c->from == UCS2BE) {
			if (u >= 0xd800 && u <= 0xdfff)
				return -1;
			*out = u;
			return 2;
		}
		if (u >= 0xdc00 && u <= 0xdfff)
			return -1;
		if (u >= 0xd800 && u <= 0xdbff) {
			if (n < 4)
				return -2;
			unsigned l = get16(s + 2, le);
			if (l < 0xdc00 || l > 0xdfff)
				return -1;
			*out = 0x10000 + ((u - 0xd800) << 10) + (l - 0xdc00);
			return 4;
		}
		*out = u;
		return 2;
	}
	default: { /* 32-bit */
		if (n < 4)
			return -2;
		int le = c->from == UTF32LE || c->from == UCS4LE || c->from == WCHAR || (c->from == UTF32 && !c->in_be);
		uint32_t u = get32(s, le);
		if (c->from == UTF32 && !c->in_bom_done) {
			c->in_bom_done = 1;
			if (u == 0xfeff)
				return 0;
			if (u == 0xfffe0000u) {
				c->in_be = 1;
				return 0;
			}
		}
		if (u > 0x10ffff || (u >= 0xd800 && u <= 0xdfff))
			return -1;
		*out = u;
		return 4;
	}
	}
}

static void put16(unsigned char *p, unsigned v, int le)
{
	p[le ? 0 : 1] = (unsigned char)v;
	p[le ? 1 : 0] = (unsigned char)(v >> 8);
}

static void put32(unsigned char *p, uint32_t v, int le)
{
	for (int i = 0; i < 4; i++)
		p[le ? i : 3 - i] = (unsigned char)(v >> (8 * i));
}

/* Encode: returns bytes written, -1 unrepresentable, -2 no room. */
static int encode(struct cd *c, uint32_t u, unsigned char *o, size_t n)
{
	switch (c->to) {
	case UTF8:
		if (u < 0x80) {
			if (n < 1)
				return -2;
			o[0] = (unsigned char)u;
			return 1;
		} else {
			int k = u < 0x800 ? 2 : u < 0x10000 ? 3 : 4;
			if (n < (size_t)k)
				return -2;
			for (int i = k - 1; i > 0; i--) {
				o[i] = (unsigned char)(0x80 | (u & 0x3f));
				u >>= 6;
			}
			o[0] = (unsigned char)((0xf00 >> k) | u);
			return k;
		}
	case ASCII: case LATIN1: case LATIN9: case CP1252: {
		int b = -1;
		if (c->to == ASCII)
			b = u < 0x80 ? (int)u : -1;
		else if (c->to == LATIN1)
			b = u < 0x100 ? (int)u : -1;
		else if (c->to == LATIN9) {
			b = u < 0x100 ? (int)u : -1;
			for (int i = 0; i < 8; i++) {
				if (latin9[i].b == u)
					b = -1; /* replaced positions */
				if (latin9[i].u == u)
					b = latin9[i].b;
			}
		} else {
			b = u < 0x80 || (u >= 0xa0 && u < 0x100) ? (int)u : -1;
			for (int i = 0; i < 32; i++)
				if (cp1252_hi[i] && cp1252_hi[i] == u)
					b = 0x80 + i;
		}
		if (b < 0)
			return -1;
		if (n < 1)
			return -2;
		o[0] = (unsigned char)b;
		return 1;
	}
	case UTF16: case UTF16LE: case UTF16BE: case UCS2: case UCS2LE: case UCS2BE: {
		int le = c->to != UTF16BE && c->to != UCS2BE;
		int bom = c->to == UTF16 && !c->out_bom_done ? 2 : 0;
		int ucs2 = c->to == UCS2 || c->to == UCS2LE || c->to == UCS2BE;
		if (ucs2 && u > 0xffff)
			return -1;
		int k = u > 0xffff ? 4 : 2;
		if (n < (size_t)(k + bom))
			return -2;
		if (bom) {
			put16(o, 0xfeff, le);
			c->out_bom_done = 1;
		}
		if (k == 2) {
			put16(o + bom, u, le);
		} else {
			u -= 0x10000;
			put16(o + bom, 0xd800 | u >> 10, le);
			put16(o + bom + 2, 0xdc00 | (u & 0x3ff), le);
		}
		return k + bom;
	}
	default: {
		int le = c->to != UTF32BE && c->to != UCS4;
		int bom = c->to == UTF32 && !c->out_bom_done ? 4 : 0;
		if (n < (size_t)(4 + bom))
			return -2;
		if (bom) {
			put32(o, 0xfeff, le);
			c->out_bom_done = 1;
		}
		put32(o + bom, u, le);
		return 4 + bom;
	}
	}
}

/* //TRANSLIT: a few common replacements, else '?' */
static const struct { unsigned short u; char s[6]; } tr[] = {
	{ 0x00a0, " " }, { 0x00a9, "(C)" }, { 0x00ab, "<<" }, { 0x00ad, "-" }, { 0x00ae, "(R)" },
	{ 0x00b5, "u" }, { 0x00b7, "." }, { 0x00bb, ">>" }, { 0x00bc, " 1/4 " }, { 0x00bd, " 1/2 " },
	{ 0x00be, " 3/4 " }, { 0x00c6, "AE" }, { 0x00d7, "x" }, { 0x00df, "ss" }, { 0x00e6, "ae" },
	{ 0x0152, "OE" }, { 0x0153, "oe" }, { 0x2002, " " }, { 0x2003, " " }, { 0x2009, " " },
	{ 0x2010, "-" }, { 0x2011, "-" }, { 0x2012, "-" }, { 0x2013, "-" }, { 0x2014, "-" },
	{ 0x2018, "'" }, { 0x2019, "'" }, { 0x201a, "," }, { 0x201c, "\"" }, { 0x201d, "\"" },
	{ 0x201e, ",," }, { 0x2022, "o" }, { 0x2026, "..." }, { 0x2039, "<" }, { 0x203a, ">" },
	{ 0x20ac, "EUR" }, { 0x2122, "(TM)" }, { 0x2190, "<-" }, { 0x2192, "->" }, { 0x2212, "-" },
};

static int translit(struct cd *c, uint32_t u, unsigned char *o, size_t n)
{
	const char *rep = "?";
	for (unsigned i = 0; i < sizeof tr / sizeof tr[0]; i++)
		if (tr[i].u == u)
			rep = tr[i].s;
	/* all or nothing */
	struct cd save = *c;
	size_t used = 0;
	for (const char *p = rep; *p; p++) {
		int w = encode(c, (unsigned char)*p, o + used, n - used);
		if (w < 0) {
			*c = save;
			return w == -2 ? -2 : encode(c, '?', o, n);
		}
		used += (size_t)w;
	}
	return (int)used;
}

size_t iconv(iconv_t cd, char **restrict in, size_t *restrict inleft, char **restrict out, size_t *restrict outleft)
{
	struct cd *c = cd;
	if (!c || cd == (iconv_t)-1) {
		errno = EBADF;
		return (size_t)-1;
	}
	if (!in || !*in) {
		/* reset to the initial state */
		c->in_bom_done = c->in_be = 0;
		c->out_bom_done = 0;
		return 0;
	}
	size_t irreversible = 0;
	int ignored = 0;
	while (*inleft) {
		uint32_t u;
		int k = decode(c, (unsigned char *)*in, *inleft, &u);
		if (k == -2) {
			errno = EINVAL;
			return (size_t)-1;
		}
		if (k == -1) {
			if (c->flags & IGNORE) {
				/* skip one byte and go on */
				++*in;
				--*inleft;
				ignored = 1;
				continue;
			}
			errno = EILSEQ;
			return (size_t)-1;
		}
		if (k == 0) {
			*in += c->from >= UTF32 ? 4 : 2;
			*inleft -= c->from >= UTF32 ? 4 : 2;
			continue;
		}
		int w = encode(c, u, (unsigned char *)*out, *outleft);
		if (w == -1) {
			if (c->flags & TRANSLIT) {
				w = translit(c, u, (unsigned char *)*out, *outleft);
				irreversible++;
			} else if (c->flags & IGNORE) {
				*in += k;
				*inleft -= (size_t)k;
				irreversible++;
				ignored = 1;
				continue;
			} else {
				errno = EILSEQ;
				return (size_t)-1;
			}
		}
		if (w == -2) {
			errno = E2BIG;
			return (size_t)-1;
		}
		*in += k;
		*inleft -= (size_t)k;
		*out += w;
		*outleft -= (size_t)w;
	}
	if (ignored) {
		/* glibc reports skipped input as EILSEQ after converting the rest */
		errno = EILSEQ;
		return (size_t)-1;
	}
	return irreversible;
}
