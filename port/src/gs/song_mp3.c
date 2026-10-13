/*
 * MP3 files dropped in the songs folder, converted to the ADX the game plays when the program starts.
 *
 * A song added from outside the disc is an ADX file (plat_songs.c). Here a `name.mp3` is decoded (dr_mp3,
 * port/third_party/dr_libs) and written as `name.adx` next to it, once. The song list then picks the ADX up as if it
 * had been put there by hand (or by port/tools/add_song.py).
 *
 * Which MP3 is converted. The ADX written here carries, in its header, a mark and the size and the time of last
 * change of the MP3 it was made from (header()). At start:
 *   - no `name.adx`: converted;
 *   - a `name.adx` with the mark and the MP3's size and time: nothing to do;
 *   - with the mark and another size or time (the MP3 was replaced, by a newer or an older file): converted again;
 *   - without the mark: somebody else's file (made by hand, by ffmpeg, by add_song.py). It is never written over;
 *     the MP3 is left alone and the log says so.
 * (The pull request compared the two files' dates: an MP3 replaced by one with an earlier date was not converted
 * again, an ADX with an earlier date than its MP3 was converted at every start, and an ADX made by hand was written
 * over whenever the MP3 beside it was the newer file.)
 *
 * An MP3 that cannot be decoded is left out, with a line in the log and a notice over the picture, and written down
 * in `mp3-failed.txt` in the folder with its size and time: it is not tried again, nor reported, until the file
 * changes or the line is deleted. A failure to WRITE (a folder that cannot be written to, a full disk) is not the
 * MP3's fault and is not written down: it is tried, and reported, at every start.
 *
 * Names. `.mp3` in any case; only the last four characters are taken off (`a.b.mp3` gives `a.b.adx`). Names of 128
 * bytes and more are left out, as the song list leaves them out. Two MP3 files that would give the same ADX
 * (`a.mp3` and `a.MP3`; on Windows and macOS also `A.mp3` and `a.mp3` where the disk tells them apart): the first by
 * name is converted, the other left out with a line in the log, at every start the same one. On Windows the files
 * are found and opened by the names the system gives in its code page: a name with characters outside it cannot be
 * opened by this program at all (nor could its ADX be played, plat_file.c opens the same way) and is left out with a
 * line in the log saying to rename it.
 *
 * The ADX written is the plain kind snd_adx.c reads: type 3, 18-byte frames, 4 bits, version 3 (no loop points: an
 * added song is played round from its start), the MP3's own rate and channels (one or two). Encoding mirrors the
 * decoder: each 32-sample frame's prediction is made from the samples the decoder will have, so the error does not
 * build up through the filter.
 */
#define DR_MP3_IMPLEMENTATION
#include "../../third_party/dr_libs/dr_mp3.h"

#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#endif

extern void Port_UiNotice(const char *text); /* ui.cpp: a line over the picture for a few seconds */

#define FRAME 18
#define HEADER 0x60     /* the header's bytes, "(c)CRI" last; the data follows */
#define MARK_AT 0x30    /* the mark, then the MP3's size and time, 8 bytes each (0x14..0x2F stay 0: a reader that
                           looks for loop points in a version 3 header looks there) */
#define HIGHPASS 500    /* the high-pass frequency of the disc's music and of ffmpeg's encoder */
#define NAME_MAX_ 128   /* a file name's bytes with its end, as in plat_extras.c and plat_songs.c */
#define MP3_MAX 256     /* MP3 files looked at (the song list holds 40) */

static const char kMark[8] = {'B', 'T', '3', ':', 'M', 'P', '3', 1};

static void put16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v) {
    put16(p, v >> 16);
    put16(p + 2, v & 0xFFFF);
}

/* The prediction coefficients, as snd_adx.c's adx_coefs computes them (the same truncation). */
static void coefs(uint32_t rate, int *c1, int *c2) {
    double z = cos(2.0 * 3.14159265358979323846 * (double)HIGHPASS / (double)rate);
    double a = 1.41421356237309504880 - z, b = 1.41421356237309504880 - 1.0;
    double c = (a - sqrt((a + b) * (a - b))) / b;

    *c1 = (int)(c * 8192.0);
    *c2 = (int)(c * c * -4096.0);
}

/* One frame of one channel: 32 samples (`in`, `step` apart) into 18 bytes. h[0], h[1] are the decoder's last two
   samples, updated. */
static void encode_frame(const int16_t *in, int step, int c1, int c2, int h[2], uint8_t *out) {
    int i, peak = 0, scale, h1 = h[0], h2 = h[1];

    /* the scale: the largest residual of an estimate that follows the input (the decoder's history to start) */
    for (i = 0; i < 32; i++) {
        int s = in[i * step], d = s - ((c1 * h1 + c2 * h2) >> 12);
        if (d < 0) { d = -d; }
        if (d > peak) { peak = d; }
        h2 = h1;
        h1 = s;
    }
    scale = (peak + 6) / 7;
    if (scale < 1) { scale = 1; }
    if (scale > 0x10000) { scale = 0x10000; }
    put16(out, (unsigned)(scale - 1));
    memset(out + 2, 0, 16);

    /* the nibbles, from the samples the decoder makes */
    h1 = h[0];
    h2 = h[1];
    for (i = 0; i < 32; i++) {
        int pred = (c1 * h1 + c2 * h2) >> 12, d = in[i * step] - pred, n, s;
        n = d >= 0 ? (d + scale / 2) / scale : -((-d + scale / 2) / scale);
        if (n > 7) { n = 7; }
        if (n < -8) { n = -8; }
        s = n * scale + pred;
        if (s > 32767) { s = 32767; }
        if (s < -32768) { s = -32768; }
        out[2 + i / 2] |= (uint8_t)((n & 15) << ((i & 1) ? 0 : 4));
        h2 = h1;
        h1 = s;
    }
    h[0] = h1;
    h[1] = h2;
}

/* `mp3` into the ADX file `adx`. Returns 0 if it worked; `why` gets the reason if not. */
/* What tells one state of a file from another: its size and its time of last change (seconds; on Windows the
   file system's own count, which does not move with daylight saving time as the C library's can). Returns 0 if
   there is no such file. */
static int file_id(const char *path, uint64_t *size, uint64_t *mtime) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return 0;
    }
    *size = (uint64_t)a.nFileSizeHigh << 32 | a.nFileSizeLow;
    *mtime = (uint64_t)a.ftLastWriteTime.dwHighDateTime << 32 | a.ftLastWriteTime.dwLowDateTime;
#else
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    *size = (uint64_t)st.st_size;
    *mtime = (uint64_t)(int64_t)st.st_mtime;
#endif
    return 1;
}

enum { ADX_NONE, ADX_CURRENT, ADX_STALE, ADX_FOREIGN };

/* What `adx` is to the MP3 of that size and time (see the top). */
static int adx_state(const char *adx, uint64_t size, uint64_t mtime) {
    struct stat st;
    uint8_t h[HEADER];
    FILE *fp;
    size_t got;
    int k;

    if (stat(adx, &st) != 0) {
        return ADX_NONE;
    }
    fp = fopen(adx, "rb");
    got = fp != NULL ? fread(h, 1, sizeof(h), fp) : 0;
    if (fp != NULL) {
        fclose(fp);
    }
    if (got < sizeof(h) || h[0] != 0x80 || h[1] != 0 || ((unsigned)h[2] << 8 | h[3]) != HEADER - 4 || memcmp(h + MARK_AT, kMark, 8) != 0) {
        return ADX_FOREIGN; /* (also a folder of that name, a file that cannot be read) */
    }
    for (k = 0; k < 8; k++) {
        if (h[MARK_AT + 8 + k] != (uint8_t)(size >> (56 - 8 * k)) || h[MARK_AT + 16 + k] != (uint8_t)(mtime >> (56 - 8 * k))) {
            return ADX_STALE;
        }
    }
    return ADX_CURRENT;
}

static void header(uint8_t *head, unsigned channels, uint32_t rate, uint32_t frames, uint64_t size, uint64_t mtime) {
    memset(head, 0, HEADER);
    memcpy(head + MARK_AT, kMark, 8);
    put32(head + MARK_AT + 8, (uint32_t)(size >> 32));
    put32(head + MARK_AT + 12, (uint32_t)size);
    put32(head + MARK_AT + 16, (uint32_t)(mtime >> 32));
    put32(head + MARK_AT + 20, (uint32_t)mtime);
    put16(head, 0x8000);
    put16(head + 2, HEADER - 4);
    head[4] = 3;
    head[5] = FRAME;
    head[6] = 4;
    head[7] = (uint8_t)channels;
    put32(head + 8, rate);
    put32(head + 12, frames);
    put16(head + 16, HIGHPASS);
    head[18] = 3; /* version 3: no loop points */
    memcpy(head + HEADER - 6, "(c)CRI", 6);
}

#define CHUNK (32 * 256) /* sample frames decoded and encoded at a time: whole ADX frames */

/* `mp3` (of that size and time) into the ADX file `adx`. Returns 0 if it worked; `why` gets the reason if not,
   and `source` whether the MP3 is the reason (and not the folder or the disk). The MP3 is decoded and
   written a piece at a time (CHUNK): the memory used is the decoder and 32 KB of samples however long the song is
   (decoded whole, an hour of music was 635 MB). The number of samples, which the header holds, is known at the end
   and written then. */
static int convert(const char *mp3, const char *adx, uint64_t size, uint64_t mtime, const char **why, int *source) {
    static int16_t pcm[CHUNK * 2];
    drmp3 *dec = malloc(sizeof(*dec));
    char tmp[600];
    uint8_t head[HEADER], buf[FRAME * 2];
    int ch, channels, c1, c2, h[2][2] = {{0, 0}, {0, 0}}, last = 0;
    uint64_t frames = 0;
    uint32_t rate;
    FILE *fp;

    *source = 1;
    if (dec == NULL || !drmp3_init_file(dec, mp3, NULL)) {
        *why = "not an MP3 that can be read";
        free(dec);
        return -1;
    }
    channels = (int)dec->channels;
    if (channels < 1 || channels > 2 || dec->sampleRate == 0) {
        *why = channels > 2 ? "more than two channels" : "not an MP3 that can be read";
        drmp3_uninit(dec);
        free(dec);
        return -1;
    }
    /* written under another name and given its own when whole: the song list never sees half a file. (With the
       process number: two copies of the game started together, as for a test of online play, each write their own.) */
    snprintf(tmp, sizeof(tmp), "%s.%ld.part", adx, (long)getpid());
    fp = fopen(tmp, "wb");
    if (fp == NULL) {
        *source = 0;
        *why = "the songs folder cannot be written to";
        drmp3_uninit(dec);
        free(dec);
        return -1;
    }
    rate = dec->sampleRate;
    header(head, (unsigned)channels, rate, 0, size, mtime);
    fwrite(head, 1, sizeof(head), fp);

    coefs(rate, &c1, &c2);
    *why = NULL;
    while (!last && *why == NULL) {
        uint64_t got = 0, f;
        while (got < CHUNK) { /* (a read may give less than asked for before the end) */
            uint64_t n = drmp3_read_pcm_frames_s16(dec, CHUNK - got, pcm + got * channels);
            if (n == 0) {
                break;
            }
            got += n;
        }
        if (got < CHUNK) { /* the end: the rest of the last frame silent */
            last = 1;
            memset(pcm + got * channels, 0, (size_t)(CHUNK - got) * channels * sizeof(int16_t));
        }
        for (f = 0; f * 32 < got; f++) {
            for (ch = 0; ch < channels; ch++) {
                encode_frame(pcm + f * 32 * channels + ch, channels, c1, c2, h[ch], buf + ch * FRAME);
            }
            fwrite(buf, 1, (size_t)FRAME * channels, fp);
        }
        frames += got;
        if (frames > 0xFFFFFFF0u) {
            *why = "too long";
        } else if (ferror(fp)) {
            *why = "the songs folder cannot be written to";
            *source = 0;
        }
    }
    drmp3_uninit(dec);
    free(dec);
    if (*why == NULL && frames == 0) {
        *why = "not an MP3 that can be read";
    }
    if (*why != NULL) {
        fclose(fp);
        remove(tmp);
        return -1;
    }
    header(head, (unsigned)channels, rate, (uint32_t)frames, size, mtime);
    fseek(fp, 0, SEEK_SET);
    fwrite(head, 1, sizeof(head), fp);
    *source = 0;
    if (ferror(fp) | fclose(fp)) {
        *why = "the songs folder cannot be written to";
        remove(tmp);
        return -1;
    }
    if (adx_state(adx, size, mtime) == ADX_FOREIGN) { /* (put there while this one was being made) */
        *why = "an ADX of that name, not made from it, is there";
        remove(tmp);
        return -1;
    }
#ifdef _WIN32
    if (!MoveFileExA(tmp, adx, MOVEFILE_REPLACE_EXISTING)) { /* (rename does not replace a file on Windows) */
#else
    if (rename(tmp, adx) != 0) {
#endif
        *why = "the songs folder cannot be written to";
        remove(tmp);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------------------- the MP3 files that failed */

typedef struct Failed {
    uint64_t size, mtime;
    char name[NAME_MAX_], why[64];
    int seen; /* still true of a file in the folder */
} Failed;

static Failed sFailed[MP3_MAX];
static int sFailedCount;

static void failed_path(const char *dir, char *out, size_t n) {
    snprintf(out, n, "%s/mp3-failed.txt", dir);
}

/* A line: "<size> <time> <file name> | <reason>". */
static void failed_load(const char *dir) {
    char path[600], line[400];
    FILE *fp;

    failed_path(dir, path, sizeof(path));
    sFailedCount = 0;
    fp = fopen(path, "rb");
    while (fp != NULL && sFailedCount < MP3_MAX && fgets(line, sizeof(line), fp) != NULL) {
        Failed *f = &sFailed[sFailedCount];
        char *e, *name, *bar = NULL, *q;
        size_t l = strlen(line);

        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) {
            line[--l] = '\0';
        }
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }
        f->size = strtoull(line, &e, 10);
        if (e == line || *e != ' ') {
            continue;
        }
        name = e + 1;
        f->mtime = strtoull(name, &e, 10);
        if (e == name || *e != ' ') {
            continue;
        }
        name = e + 1;
        for (q = name; (q = strstr(q, " | ")) != NULL; q += 3) {
            bar = q; /* (the last one: a file name may hold the same three characters) */
        }
        if (bar == NULL || bar == name || (size_t)(bar - name) >= sizeof(f->name)) {
            continue;
        }
        snprintf(f->name, sizeof(f->name), "%.*s", (int)(bar - name), name);
        snprintf(f->why, sizeof(f->why), "%s", bar + 3);
        f->seen = 0;
        sFailedCount++;
    }
    if (fp != NULL) {
        fclose(fp);
    }
}

/* Writes the entries still true of a file; with none left the file is taken away. */
static void failed_save(const char *dir) {
    char path[600];
    FILE *fp = NULL;
    int i;

    failed_path(dir, path, sizeof(path));
    for (i = 0; i < sFailedCount; i++) {
        const Failed *f = &sFailed[i];
        if (!f->seen) {
            continue;
        }
        if (fp == NULL) {
            fp = fopen(path, "wb");
            if (fp == NULL) {
                return; /* (cannot be written down: tried again at the next start) */
            }
            fputs("# MP3 files the game could not convert (written by the game). A file is tried again when it changes,\n"
                  "# or when its line here is deleted. <size> <time of last change> <file> | <why>\n", fp);
        }
        fprintf(fp, "%" PRIu64 " %" PRIu64 " %s | %s\n", f->size, f->mtime, f->name, f->why);
    }
    if (fp != NULL) {
        fclose(fp);
    } else {
        remove(path);
    }
}

static Failed *failed_find(const char *name) {
    int i;
    for (i = 0; i < sFailedCount; i++) {
        if (strcmp(sFailed[i].name, name) == 0) {
            return &sFailed[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------------------- the folder */

static int ends_with(const char *s, const char *ext) {
    size_t l = strlen(s), e = strlen(ext), k;
    if (l <= e) {
        return 0;
    }
    for (k = 0; k < e && tolower((unsigned char)s[l - e + k]) == ext[k]; k++) {
    }
    return k == e;
}

/* By name without regard to case, then by the bytes: the same order on every machine and at every start. */
static int name_cmp(const void *a, const void *b) {
    const unsigned char *x = a, *y = b;
    for (; *x != '\0' && tolower(*x) == tolower(*y); x++, y++) {
    }
    return tolower(*x) != tolower(*y) ? tolower(*x) - tolower(*y) : strcmp(a, b);
}

/* Whether the two MP3 names give the same ADX file. */
static int same_target(const char *a, const char *b) {
    size_t la = strlen(a) - 4, lb = strlen(b) - 4, k;
    if (la != lb) {
        return 0;
    }
#if defined(_WIN32) || defined(__APPLE__)
    for (k = 0; k < la && tolower((unsigned char)a[k]) == tolower((unsigned char)b[k]); k++) {
    }
    return k == la;
#else
    (void)k;
    return memcmp(a, b, la) == 0;
#endif
}

/* Converts every MP3 of `dir` that needs it (see the top). Called by plat_songs.c before it lists the folder. */
void PortSongs_ConvertMp3(const char *dir) {
    static char names[MP3_MAX][NAME_MAX_];
    static unsigned char taken[MP3_MAX]; /* this MP3 is the one its ADX name belongs to */
    DIR *d = opendir(dir);
    struct dirent *de;
    char first[NAME_MAX_] = "";
    int n = 0, k, j, bad = 0, dirty = 0;

    if (d == NULL) {
        return;
    }
    while ((de = readdir(d)) != NULL) {
        if (!ends_with(de->d_name, ".mp3")) {
            continue;
        }
        if (strlen(de->d_name) >= NAME_MAX_) {
            fprintf(stderr, "bt3: songs: %s left out: the name is too long (%d bytes at most)\n", de->d_name, NAME_MAX_ - 1);
        } else if (n >= MP3_MAX) {
            fprintf(stderr, "bt3: songs: more than %d MP3 files, %s is left out\n", MP3_MAX, de->d_name);
        } else {
            snprintf(names[n++], NAME_MAX_, "%s", de->d_name);
        }
    }
    closedir(d);
    if (n == 0) {
        return; /* (a list of failures with no MP3 left in the folder stays as it is: nothing is written without one) */
    }
    qsort(names, (size_t)n, NAME_MAX_, name_cmp);
    failed_load(dir);
    for (k = 0; k < n; k++) {
        const char *name = names[k], *why = "";
        char mp3[600], adx[600];
        uint64_t size, mtime;
        int source = 0, state;
        Failed *f;

        taken[k] = 0;
        snprintf(mp3, sizeof(mp3), "%s/%s", dir, name);
        snprintf(adx, sizeof(adx), "%s/%.*s.adx", dir, (int)(strlen(name) - 4), name);
#ifdef _WIN32
        if (strchr(name, '?') != NULL) { /* the system puts a question mark where the code page has no character */
            fprintf(stderr, "bt3: songs: %s left out: its name has characters this program cannot open a file by; rename it\n", name);
            continue;
        }
#endif
        if (!file_id(mp3, &size, &mtime)) {
            continue; /* (a folder of that name; a file gone since the folder was read) */
        }
        for (j = 0; j < k && !(taken[j] && same_target(names[j], name)); j++) {
        }
        if (j < k) {
            fprintf(stderr, "bt3: songs: %s left out: %s is converted to the same file name\n", name, names[j]);
            continue;
        }
        taken[k] = 1;
        state = adx_state(adx, size, mtime);
        if (state == ADX_CURRENT) {
            continue;
        }
        if (state == ADX_FOREIGN) {
            fprintf(stderr, "bt3: songs: %s is not converted: the %.*s.adx beside it was not made from it, and is kept\n", name,
                    (int)(strlen(name) - 4), name);
            continue;
        }
        f = failed_find(name);
        if (f != NULL && f->size == size && f->mtime == mtime) {
            f->seen = 1; /* failed as it is now: not tried again, not reported again */
            continue;
        }
        if (f != NULL) {
            dirty = 1; /* (the file changed: its line goes, or is written anew below) */
        }
        if (convert(mp3, adx, size, mtime, &why, &source) == 0) {
            fprintf(stderr, "bt3: songs: %s converted to ADX\n", name);
            continue;
        }
        fprintf(stderr, "bt3: songs: %s left out: %s%s\n", name, why, source ? " (not tried again until the file changes: mp3-failed.txt)" : "");
        if (source) {
            if (f == NULL && sFailedCount < MP3_MAX) {
                f = &sFailed[sFailedCount++];
                snprintf(f->name, sizeof(f->name), "%s", name);
            }
            if (f != NULL) {
                f->size = size;
                f->mtime = mtime;
                snprintf(f->why, sizeof(f->why), "%s", why);
                f->seen = 1;
                dirty = 1;
            }
        }
        if (bad++ == 0) {
            snprintf(first, sizeof(first), "%s", name);
        }
    }
    for (k = 0; k < sFailedCount; k++) {
        dirty |= !sFailed[k].seen; /* (its MP3 is gone, or was converted in another way) */
    }
    if (dirty) {
        failed_save(dir);
    }
    if (bad == 1) {
        char text[200];
        snprintf(text, sizeof(text), "Song %s could not be converted and is left out.", first);
        Port_UiNotice(text);
    } else if (bad > 1) {
        char text[200];
        snprintf(text, sizeof(text), "%d songs could not be converted and are left out (%s and others).", bad, first);
        Port_UiNotice(text);
    }
}
