/*
 * Browser build: the check that the game data is the original disc's (plat_verify.c) is not run, and that is said.
 *
 * Natively the check runs on a thread of its own: a quick pass over every data file's size and time (stat), then,
 * unless a result kept in <root>/.verified still matches, a CRC-32 of all 68,605 files (about 3 GB). In the browser
 *   - there is no thread to run it on (pthread_create fails: the build has no threads), and before this file the
 *     failure left the check off without a word;
 *   - the data folder is on the HTTP server (web_fs.c), not in the program's own file system: stat finds none of
 *     its files, so the quick pass, run anyway, would call every file missing and the original data "modified";
 *   - the full pass would fetch the whole 3 GB at every start (nothing to keep a result against: no file times).
 * So Port_VerifyStart is linked wrapped (wasm_build.py) and does not start it. The log says the data is not
 * checked and why; the crash report and the page (web_ui.c: data_check) say the same. BT3_VERIFY=0 still means
 * "turned off", as natively.
 */
#include <stdio.h>
#include <stdlib.h>

const char *__real_Port_DataSummary(void);

/* The page's view of it (web_ui.c "data_check"): plat_verify.c's "off" (3), and states of its own. */
enum { CHECK_NOT_STARTED = -1, CHECK_OFF = 3, CHECK_NOT_IN_BROWSER = 4 };
static int sCheck = CHECK_NOT_STARTED;

static const char kNotRun[] = "game data: not checked against the original disc (the browser version cannot run the "
                              "check: it reads every file of the data folder, about 3 GB, and there are no threads to "
                              "run it on); the native build checks it";

void __wrap_Port_VerifyStart(void) {
    const char *v = getenv("BT3_VERIFY");
    static int once;

    if (once) { /* (the caller runs again when the game restarts itself) */
        return;
    }
    once = 1;
    if (v != NULL && atoi(v) == 0) {
        sCheck = CHECK_OFF;
        fprintf(stderr, "bt3: game data: check turned off (BT3_VERIFY=0)\n");
        return;
    }
    sCheck = CHECK_NOT_IN_BROWSER;
    fprintf(stderr, "bt3: %s\n", kNotRun);
}

/* What a crash report adds about the data: natively "" for the original disc's; here that is not known. */
const char *__wrap_Port_DataSummary(void) {
    const char *s = __real_Port_DataSummary();
    return s[0] == '\0' && sCheck == CHECK_NOT_IN_BROWSER ? "game data: not checked (browser version)" : s;
}

int WebVerify_State(void) {
    return sCheck;
}

/* ---- the gameplay fingerprint (plat_fprint.c), for online play's rule: two unmodded games ----------------------
   Natively it is worked out on the data check's thread, which the browser does not have (and it reads about 1.2 GB
   of files). The data server works it out instead (port/web/devserver.py, port/tools/make_fingerprint.py: the
   definition's second implementation) and the page hands it over as BT3_WEB_FPRINT=<main>,<7 parts> (16 hex digits
   each). What the original disc's is, is decided here, against the table the native code uses (plat_fprint_tab.h),
   not taken from the server. Without it the fingerprint is "not computed" (-1), as natively without a data check. */
#include <string.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"
#include "../plat_fprint_tab.h"
#pragma clang diagnostic pop

static const char *const kFpParts[7] = {"tables", "characters", "animations", "stages", "effects", "scripts", "menus"};
static int sFpState = -2; /* -2 not read yet, -1 none, 1 known */
static unsigned long long sFpMain, sFpPart[7];
static char sFpText[512];

static void fp_read(void) {
    const char *v = getenv("BT3_WEB_FPRINT"), *p = v;
    unsigned long long x[8];
    size_t used;
    int k, differs = 0;

    sFpState = -1;
    for (k = 0; v != NULL && k < 8; k++) {
        char *end;
        x[k] = strtoull(p, &end, 16);
        if (end - p != 16 || (k < 7 ? *end != ',' : *end != '\0')) {
            fprintf(stderr, "bt3: gameplay fingerprint: BT3_WEB_FPRINT is not of the form <main>,<7 parts>: ignored\n");
            return;
        }
        p = end + 1;
    }
    if (v == NULL) {
        return;
    }
    sFpMain = x[0];
    memcpy(sFpPart, x + 1, sizeof(sFpPart));
    sFpState = 1;
    used = (size_t)snprintf(sFpText, sizeof(sFpText), "gameplay data: %08x (", (unsigned)(sFpMain >> 32));
    for (k = 0; k < 7; k++) {
        used += (size_t)snprintf(sFpText + used, sizeof(sFpText) - used, "%s%s %08x", k ? " " : "", kFpParts[k], (unsigned)(sFpPart[k] >> 32));
    }
    used += (size_t)snprintf(sFpText + used, sizeof(sFpText) - used, "): %s", sFpMain == kFpMain ? "the original disc's" : "modified:");
    for (k = 0; sFpMain != kFpMain && k < 7 && used < sizeof(sFpText); k++) {
        if (sFpPart[k] != kFpComp[k]) {
            used += (size_t)snprintf(sFpText + used, sizeof(sFpText) - used, "%s %s", differs++ ? "," : "", kFpParts[k]);
        }
    }
    if (used < sizeof(sFpText)) {
        snprintf(sFpText + used, sizeof(sFpText) - used, " (worked out by the data server)");
    }
    fprintf(stderr, "bt3: %s\n", sFpText);
}

int __wrap_Port_DataFingerprint(unsigned long long *out) {
    if (sFpState == -2) {
        fp_read();
    }
    if (sFpState == 1 && out != NULL) {
        *out = sFpMain;
    }
    return sFpState;
}
int __wrap_Port_DataFingerprintPart(int part, unsigned long long *out) {
    if (__wrap_Port_DataFingerprint(NULL) != 1 || part < 0 || part >= 7) {
        return sFpState == 1 ? -1 : sFpState;
    }
    *out = sFpPart[part];
    return 1;
}
int __wrap_Port_DataFingerprintOriginal(void) {
    return __wrap_Port_DataFingerprint(NULL) == 1 ? sFpMain == kFpMain : -1;
}
const char *__wrap_Port_DataFingerprintText(void) {
    return __wrap_Port_DataFingerprint(NULL) == 1 ? sFpText : "";
}
