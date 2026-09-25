/* strfmon for the C locale, the only one this library has: no currency
 * symbol, no grouping, '.' as the decimal point and "-" as the sign. */
#include <monetary.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static ssize_t vstrfmon(char *restrict s, size_t max, const char *restrict fmt, va_list ap)
{
	size_t pos = 0;
	if (!max)
		goto toobig;
	for (; *fmt; fmt++) {
		if (*fmt != '%' || fmt[1] == '%') {
			if (pos + 1 >= max)
				goto toobig;
			s[pos++] = *fmt;
			fmt += *fmt == '%';
			continue;
		}
		fmt++;
		char fill = ' ';
		int paren = 0, left = 0;
		for (;; fmt++) {
			if (*fmt == '=') {
				if (!*++fmt)
					goto inval;
				fill = *fmt;
			} else if (*fmt == '(') {
				paren = 1;
			} else if (*fmt == '-') {
				left = 1;
			} else if (*fmt != '^' && *fmt != '+' && *fmt != '!') {
				break;
			}
		}
		size_t width = 0, lprec = 0, rprec = 2;
		int has_lprec = 0;
		for (; *fmt >= '0' && *fmt <= '9'; fmt++)
			if ((width = width * 10 + (size_t)(*fmt - '0')) > max)
				goto toobig;
		if (*fmt == '#') {
			has_lprec = 1;
			for (fmt++; *fmt >= '0' && *fmt <= '9'; fmt++)
				if ((lprec = lprec * 10 + (size_t)(*fmt - '0')) > max)
					goto toobig;
		}
		if (*fmt == '.') {
			rprec = 0;
			for (fmt++; *fmt >= '0' && *fmt <= '9'; fmt++)
				if ((rprec = rprec * 10 + (size_t)(*fmt - '0')) > max)
					goto toobig;
		}
		if (*fmt != 'i' && *fmt != 'n')
			goto inval;
		double v = va_arg(ap, double);
		int neg = v < 0;
		if (neg)
			v = -v;
		int len = snprintf(0, 0, "%.*f", (int)rprec, v);
		if (len < 0)
			goto inval;
		size_t idigits = (size_t)len;
		if (rprec)
			idigits -= rprec + 1;
		size_t pad = lprec > idigits ? lprec - idigits : 0;
		/* a sign slot is kept for positive values when aligning columns */
		size_t pre = neg || has_lprec ? 1 : 0;
		size_t post = neg && paren ? 1 : 0;
		size_t body = pre + pad + (size_t)len + post;
		size_t field = width > body ? width : body;
		if (pos + field >= max)
			goto toobig;
		size_t spaces = field - body;
		if (!left) {
			memset(s + pos, ' ', spaces);
			pos += spaces;
		}
		if (pre)
			s[pos++] = neg ? (paren ? '(' : '-') : ' ';
		memset(s + pos, fill, pad);
		pos += pad;
		snprintf(s + pos, (size_t)len + 1, "%.*f", (int)rprec, v);
		pos += (size_t)len;
		if (post)
			s[pos++] = ')';
		if (left) {
			memset(s + pos, ' ', spaces);
			pos += spaces;
		}
	}
	s[pos] = 0;
	return (ssize_t)pos;
toobig:
	errno = E2BIG;
	return -1;
inval:
	errno = EINVAL;
	return -1;
}

ssize_t strfmon(char *restrict s, size_t max, const char *restrict fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ssize_t r = vstrfmon(s, max, fmt, ap);
	va_end(ap);
	return r;
}

ssize_t strfmon_l(char *restrict s, size_t max, locale_t loc, const char *restrict fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ssize_t r = vstrfmon(s, max, fmt, ap);
	va_end(ap);
	return r;
}
