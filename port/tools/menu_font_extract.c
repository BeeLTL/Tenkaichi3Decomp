/* Host-only bridge for menu_font.py. Reuses the port's letter segmentation and spacing solver, and has the two
   loops that are slow in Python (the archives' compression and the texture hash). Never part of the game. */
#include "../src/gs/namefont.c"

int MenuFont_Extract(int style, const uint8_t *rgba, int sheets, int rowHeight, const char **names, int count) {
    int i;
    if (style < 0 || style >= NF_STYLES || sheets < 1 || sheets > MAX_SHEETS) { return 0; }
    for (i = 0; i < MAX_SHEETS; i++) {
        free(sSheet[style][i]);
        sSheet[style][i] = NULL;
    }
    for (i = 0; i < 128; i++) { free(sStyle[style].g[i].px); }
    memset(&sStyle[style], 0, sizeof(sStyle[style]));
    sSheetCount[style] = sheets;
    for (i = 0; i < sheets; i++) {
        sSheet[style][i] = malloc(SHEET_W * SHEET_H * 4);
        if (sSheet[style][i] == NULL) { return 0; }
        memcpy(sSheet[style][i], rgba + (size_t)i * SHEET_W * SHEET_H * 4, SHEET_W * SHEET_H * 4);
    }
    return style_build(style, names, count, rowHeight);
}

const uint8_t *MenuFont_Glyph(int style, int ch, int *metrics, float *spacing) {
    NfGlyph *g = &sStyle[style].g[ch];
    metrics[0] = g->w; metrics[1] = g->h; metrics[2] = g->ox; metrics[3] = g->oy;
    metrics[4] = g->sy0; metrics[5] = g->sy1; metrics[6] = g->sw;
    spacing[0] = g->lb; spacing[1] = g->rb; spacing[2] = sStyle[style].space;
    return g->px;
}

/* The game's byte-pair compression (src/sys/bpe.c), written again with every read and write checked: `data` is the
   whole member (size and packed size first), `size` the unpacked size it names. Returns the bytes written, or -1. */
long MenuFont_Unpack(const uint8_t *data, long len, uint8_t *out, long size) {
    long p = 8, n = 0, end;
    if (len < 8) { return -1; }
    end = 8 + (long)((uint32_t)data[4] | (uint32_t)data[5] << 8 | (uint32_t)data[6] << 16 | (uint32_t)data[7] << 24);
    if (end > len || end < 8) { return -1; }
    while (p < end) {
        uint8_t left[256], right[256], stack[512];
        int c = 0, i, sp;
        long block, k;
        for (i = 0; i < 256; i++) { left[i] = (uint8_t)i; right[i] = 0; }
        while (c < 256) {
            int count;
            if (p >= end) { return -1; }
            count = data[p++];
            if (count > 127) { c += count - 127; count = 0; }
            if (c == 256) { break; }
            for (i = 0; i <= count; i++) {
                if (c >= 256 || p >= end) { return -1; }
                left[c] = data[p++];
                if (c != left[c]) {
                    if (p >= end) { return -1; }
                    right[c] = data[p++];
                }
                c++;
            }
        }
        if (p + 2 > end) { return -1; }
        block = data[p] * 256 + data[p + 1];
        p += 2;
        if (p + block > end) { return -1; }
        for (k = 0; k < block; k++) {
            sp = 0;
            stack[sp++] = data[p + k];
            while (sp > 0) {
                uint8_t v = stack[--sp];
                if (left[v] == v) {
                    if (n >= size) { return -1; }
                    out[n++] = v;
                } else {
                    if (sp + 2 > 256) { return -1; }
                    stack[sp++] = right[v];
                    stack[sp++] = left[v];
                }
            }
        }
        p += block;
    }
    return p == end ? n : -1;
}

/* FNV-1a, 64 bits: the hash port/src/gs/menu_text.c looks a decoded texture up by. */
uint64_t MenuFont_Hash(const uint8_t *data, long len) {
    uint64_t h = UINT64_C(14695981039346656037);
    long i;
    for (i = 0; i < len; i++) { h = (h ^ data[i]) * UINT64_C(1099511628211); }
    return h;
}
