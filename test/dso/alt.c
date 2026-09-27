/* Exits with libtb's value: 2 from lib/, 3 from alt/ (LD_LIBRARY_PATH). */
int tb_value(void);
void log_event(const char *s) { (void)s; }
int main(void) { return tb_value(); }
