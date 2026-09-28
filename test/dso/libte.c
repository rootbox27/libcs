/* dlopen test: uses td_value without needing libtd.so, so it loads only
 * once libtd.so is in the global scope, and then keeps it loaded. */
int td_value(void);
int te_get(void) { return td_value(); }
