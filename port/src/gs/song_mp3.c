/*
 * MP3 files dropped in the songs folder, converted to the ADX the game plays when the program starts.
 *
 * A song added from outside the disc is an ADX file (plat_songs.c). Here a `name.mp3` with no `name.adx` beside it,
 * or an older one, is decoded (dr_mp3, port/third_party/dr_libs) and written as `name.adx` next to it, once: the
 * next start finds the ADX and does nothing. The song list then picks the ADX up as if it had been put there by hand
 * (or by port/tools/add_song.py). A file that cannot be read or written is left out, with a notice over the picture;
 * the game goes on without it.
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
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern void Port_UiNotice(const char *text); /* ui.cpp: a line over the picture for a few seconds */

#define FRAME 18
#define HEADER 40       /* the header's bytes, "(c)CRI" last; the data follows */
#define HIGHPASS 500    /* the high-pass frequency of the disc's music and of ffmpeg's encoder */

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
static void header(uint8_t *head, unsigned channels, uint32_t rate, uint32_t frames) {
    memset(head, 0, HEADER);
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

/* `mp3` into the ADX file `adx`. Returns 0 if it worked; `why` gets the reason if not. The MP3 is decoded and
   written a piece at a time (CHUNK): the memory used is the decoder and 32 KB of samples however long the song is
   (decoded whole, an hour of music was 635 MB). The number of samples, which the header holds, is known at the end
   and written then. */
static int convert(const char *mp3, const char *adx, const char **why) {
    static int16_t pcm[CHUNK * 2];
    drmp3 *dec = malloc(sizeof(*dec));
    char tmp[600];
    uint8_t head[HEADER], buf[FRAME * 2];
    int ch, channels, c1, c2, h[2][2] = {{0, 0}, {0, 0}}, last = 0;
    uint64_t frames = 0;
    uint32_t rate;
    FILE *fp;

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
    snprintf(tmp, sizeof(tmp), "%s.part", adx);
    fp = fopen(tmp, "wb");
    if (fp == NULL) {
        *why = "the songs folder cannot be written to";
        drmp3_uninit(dec);
        free(dec);
        return -1;
    }
    rate = dec->sampleRate;
    header(head, (unsigned)channels, rate, 0);
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
    header(head, (unsigned)channels, rate, (uint32_t)frames);
    fseek(fp, 0, SEEK_SET);
    fwrite(head, 1, sizeof(head), fp);
    if (ferror(fp) | fclose(fp)) {
        *why = "the songs folder cannot be written to";
        remove(tmp);
        return -1;
    }
    remove(adx); /* (rename does not replace a file on Windows) */
    if (rename(tmp, adx) != 0) {
        *why = "the songs folder cannot be written to";
        remove(tmp);
        return -1;
    }
    return 0;
}

static int ends_with(const char *s, const char *ext) {
    size_t l = strlen(s), e = strlen(ext), k;
    if (l <= e) {
        return 0;
    }
    for (k = 0; k < e && tolower((unsigned char)s[l - e + k]) == ext[k]; k++) {
    }
    return k == e;
}

/* Converts every MP3 of `dir` that has no current ADX beside it. Called by plat_songs.c before it lists the folder. */
void PortSongs_ConvertMp3(const char *dir) {
    DIR *d = opendir(dir);
    struct dirent *de;
    char failed[160] = "";
    int bad = 0;

    if (d == NULL) {
        return;
    }
    while ((de = readdir(d)) != NULL) {
        char mp3[600], adx[600];
        struct stat sm, sa;
        const char *why = "";
        size_t l = strlen(de->d_name);

        if (!ends_with(de->d_name, ".mp3") || l >= 300) {
            continue;
        }
        snprintf(mp3, sizeof(mp3), "%s/%s", dir, de->d_name);
        snprintf(adx, sizeof(adx), "%s/%.*s.adx", dir, (int)(l - 4), de->d_name);
        if (stat(mp3, &sm) != 0 || (stat(adx, &sa) == 0 && sa.st_mtime >= sm.st_mtime)) {
            continue; /* converted already */
        }
        if (convert(mp3, adx, &why) == 0) {
            fprintf(stderr, "bt3: songs: %s converted to ADX\n", de->d_name);
            continue;
        }
        fprintf(stderr, "bt3: songs: %s left out: %s\n", de->d_name, why);
        if (bad++ == 0) {
            snprintf(failed, sizeof(failed), "%s", de->d_name);
        }
    }
    closedir(d);
    if (bad == 1) {
        char text[200];
        snprintf(text, sizeof(text), "Song %s could not be converted and is left out.", failed);
        Port_UiNotice(text);
    } else if (bad > 1) {
        char text[200];
        snprintf(text, sizeof(text), "%d songs could not be converted and are left out (%s and others).", bad, failed);
        Port_UiNotice(text);
    }
}
