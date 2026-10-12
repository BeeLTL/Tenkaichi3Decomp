/* Host-only bridge for menu_font.py. Reuses the port's letter segmentation and spacing solver. */
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
