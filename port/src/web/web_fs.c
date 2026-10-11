/*
 * Browser build: the game's data read on demand over HTTP.
 *
 * The native port reads its data folder (gamedata/, about 4 GB unpacked) with fopen / fread from several places:
 * plat_fcache.c (the game's own file reads), snd_adx.c / snd_se.c (sound files), plat_mpeg2.c (movies), plat_mem.c
 * (the two programs at start). All of them are kept as they are: the program is linked with --wrap=fopen, and here
 *   - a file that exists in Emscripten's own file system (saves, settings, the two programs the page fetches before
 *     start-up) is opened there, as always;
 *   - a file opened for reading under the data root (BT3_DATA or "gamedata") that is not there is opened as a stream
 *     whose reads ask the page (web/bt3_data.js: HTTP Range requests to the server the page names, a block cache).
 *     Those reads WAIT for the network without blocking the browser: the game runs under JSPI (wasm_build.py:
 *     -sJSPI), so a read suspends the game's thread of execution until the data is there.
 * Every such read happens on the game's own thread (the sound players load a whole file when they start and the
 * audio callbacks only decode from memory). A read that cannot wait (outside the game's thread, e.g. before main)
 * fails with a message instead of returning wrong data.
 */
#define _GNU_SOURCE
#include <emscripten.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

extern const char *Port_FileRoot(void); /* plat_file.c */
FILE *__real_fopen(const char *path, const char *mode);

/* The page's loader (port/web/bt3_data.js): size of a data file (-1: there is none, -2: the server failed). */
EM_ASYNC_JS(double, web_data_size, (const char *rel), {
    return await Module.bt3Data.size(UTF8ToString(rel));
});
/* Reads `n` bytes at `off` into `dst`; returns how many (fewer at the end of the file), or -1 on an error. */
EM_ASYNC_JS(int, web_data_read, (const char *rel, double off, char *dst, int n), {
    return await Module.bt3Data.read(UTF8ToString(rel), off, dst, n);
});

typedef struct { char rel[256]; long long size, pos; } Remote;

static ssize_t remote_read(void *c, char *buf, size_t n) {
    Remote *r = c;
    long long left = r->size - r->pos;
    int got;

    if (left <= 0 || n == 0) {
        return 0;
    }
    if ((long long)n > left) {
        n = (size_t)left;
    }
    got = web_data_read(r->rel, (double)r->pos, buf, (int)n);
    if (got < 0) {
        fprintf(stderr, "bt3: reading %s at %lld failed (the data server: see the page's log)\n", r->rel, r->pos);
        errno = EIO;
        return -1;
    }
    r->pos += got;
    return got;
}

static int remote_seek(void *c, off_t *off, int whence) {
    Remote *r = c;
    long long to = whence == SEEK_SET ? *off : whence == SEEK_CUR ? r->pos + *off : r->size + *off;

    if (to < 0) {
        errno = EINVAL;
        return -1;
    }
    r->pos = to;
    *off = (off_t)to;
    return 0;
}

static int remote_close(void *c) {
    free(c);
    return 0;
}

/* The path relative to the data root, or NULL when the path is not under it. */
static const char *under_root(const char *path) {
    const char *root = Port_FileRoot();
    size_t n = strlen(root);

    while (path[0] == '.' && path[1] == '/') {
        path += 2;
    }
    if (strncmp(path, root, n) == 0 && path[n] == '/') {
        return path + n + 1;
    }
    return NULL;
}

FILE *__wrap_fopen(const char *path, const char *mode) {
    static const cookie_io_functions_t io = {remote_read, NULL, remote_seek, remote_close};
    FILE *fp = __real_fopen(path, mode);
    const char *rel;
    double size;
    Remote *r;

    if (fp != NULL || strchr(mode, 'w') != NULL || strchr(mode, 'a') != NULL || strchr(mode, '+') != NULL ||
        (rel = under_root(path)) == NULL) {
        return fp;
    }
    size = web_data_size(rel);
    if (size < 0) {
        errno = size == -1 ? ENOENT : EIO;
        return NULL;
    }
    r = calloc(1, sizeof(*r));
    if (r == NULL || strlen(rel) >= sizeof(r->rel)) {
        free(r);
        errno = ENAMETOOLONG;
        return NULL;
    }
    strcpy(r->rel, rel);
    r->size = (long long)size;
    fp = fopencookie(r, "rb", io);
    if (fp == NULL) {
        free(r);
    }
    return fp;
}
