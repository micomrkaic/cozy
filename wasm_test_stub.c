/* wasm_test_stub.c — stands in for wasm_api.c when vmtest is compiled to
 * wasm for node (the wasm rite): the browser pause latch has no page to
 * wait on, so pause returns at once, exactly as the headless native
 * REPL does. */
#include <stdint.h>
#include <string.h>
void nu_wasm_pause(const char *msg, uint32_t mlen) { (void)msg; (void)mlen; }
char *nu_wasm_input(const char *p, uint32_t n) { (void)p; (void)n; return strdup(""); }
