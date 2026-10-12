/*
 * Browser build: the crash report's context. Natively port/src/plat_crash.c installs the crash handlers (signals,
 * Windows exceptions); that file is not built for the browser (wasm_build.py, WASM_SKIP). Here a crash is a
 * WebAssembly trap (or an abort), caught by the page, which prints the JavaScript stack (with function names when the
 * build keeps them). This hands the page what the native report adds to the stack: how far the start got, and whether
 * the data is the original disc's (port/web/index.html calls it).
 */
#include <emscripten.h>
#include <stdio.h>
extern const char *Port_DataSummary(void); /* plat_verify.c (web_verify.c in the browser) */
extern const char *volatile gPortStage;   /* plat_mem.c */

EMSCRIPTEN_KEEPALIVE const char *Port_CrashContext(void) {
    static char buf[512];
    snprintf(buf, sizeof(buf), "stage: %s%s%s", gPortStage, Port_DataSummary()[0] != '\0' ? "; " : "", Port_DataSummary());
    return buf;
}
