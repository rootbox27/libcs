/* nl_langinfo for the only locales we have (C and C.UTF-8, both UTF-8
 * here). Item numbers follow glibc. */
#include <langinfo.h>

static const char *const tm_items[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
	"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
	"January", "February", "March", "April", "May", "June", "July", "August", "September",
	"October", "November", "December",
	"AM", "PM", "%a %b %e %H:%M:%S %Y", "%m/%d/%y", "%H:%M:%S", "%I:%M:%S %p",
	"", "", "", "", "", "",
};

char *nl_langinfo(nl_item item)
{
	int cat = item >> 16, idx = item & 0xffff;
	switch (cat) {
	case 0:
		return item == CODESET ? (char *)"UTF-8" : (char *)"";
	case 1:
		return (char *)(idx == 0 ? "." : "");
	case 2:
		if (idx < (int)(sizeof tm_items / sizeof tm_items[0]))
			return (char *)tm_items[idx];
		return (char *)"";
	case 4:
		return (char *)(idx == 15 ? "-" : "");
	case 5:
		return (char *)(idx == 0 ? "^[yY]" : idx == 1 ? "^[nN]" : "");
	}
	return (char *)"";
}

char *nl_langinfo_l(nl_item item, locale_t loc)
{
	(void)loc;
	return nl_langinfo(item);
}
