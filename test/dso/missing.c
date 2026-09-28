/* Needs libnope.so, which is nowhere on its search path. */
int nope(void);
int main(void) { return nope(); }
