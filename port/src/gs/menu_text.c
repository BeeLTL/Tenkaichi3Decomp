/* Editable labels occupy the original texture rows, so Flash animation, tint, clipping and layout keep
   working. menu_font.py installs a vector font and writes labels.json / labels.tsv.
   Files are read once on first texture use. No files: the original artwork is used without hashing it.
   Texture-pack replacements take precedence. The original game state and GS memory are never modified. */
#include "menu_text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/imgui/imstb_truetype.h"

#define MENU_TEXT_MAX 512
#define MENU_TEXT_WIDTH 512
#define MENU_TEXT_HEIGHT 512

typedef struct MenuLabel {
    uint64_t hash;
    int width, height, y, rowHeight, cap, baseline, center, margin, widthMilli, x, rowWidth, artWidth;
    unsigned top, bottom, outline;
    char text[256];
} MenuLabel;

static MenuLabel sLabels[MENU_TEXT_MAX], sNameStyles[3];
static int sCount, sLoaded, sCapUnits, sFontChoice;
static unsigned char *sFontData;
static stbtt_fontinfo sFont;

typedef struct MenuFont {
    char file[256], name[768];
    int id;
} MenuFont;
static MenuFont *sFonts;
static int sFontCount, sFontsLoaded;

static const char *menu_text_directory(char *root, size_t size) {
    const char *dir = getenv("BT3_MENU_FONT"), *data = getenv("BT3_DATA");
    if (dir != NULL) { return dir; }
    snprintf(root, size, "%s/game-text", data != NULL ? data : "gamedata");
    return root;
}

static void menu_text_fonts(void) {
    char root[1024], path[1200], line[1200];
    const char *dir;
    FILE *fp;
    if (sFontsLoaded) { return; }
    sFontsLoaded = 1;
    dir = menu_text_directory(root, sizeof(root));
    if (!dir[0]) { return; }
    snprintf(path, sizeof(path), "%s/fonts.tsv", dir);
    fp = fopen(path, "rb");
    if (fp == NULL) { return; }
    while (fgets(line, sizeof(line), fp) != NULL) {
        MenuFont font, *next;
        char *name;
        size_t n, i;
        unsigned id = 2166136261u;
        if (line[0] == '#' || (name = strchr(line, '\t')) == NULL) { continue; }
        *name++ = '\0';
        n = strlen(line);
        if (!n || n >= sizeof(font.file) || line[0] == '/' || strchr(line, '\\') || strchr(line, ':') ||
            strcmp(line, "..") == 0 || strncmp(line, "../", 3) == 0 || strstr(line, "/../")) { continue; }
        name[strcspn(name, "\r\n")] = '\0';
        if (!name[0] || strlen(name) >= sizeof(font.name)) { continue; }
        for (i = 0; i < n; i++) { id = (id ^ (unsigned char)line[i]) * 16777619u; }
        font.id = (int)(id & 0x7FFFFFFFu);
        if (!font.id) { font.id = 1; }
        strcpy(font.file, line); strcpy(font.name, name);
        next = realloc(sFonts, (size_t)(sFontCount + 1) * sizeof(*sFonts));
        if (next == NULL) { break; }
        sFonts = next; sFonts[sFontCount++] = font;
    }
    fclose(fp);
}

int MenuText_FontCount(void) { menu_text_fonts(); return sFontCount + 1; }

const char *MenuText_FontName(int choice) {
    return choice > 0 && choice < MenuText_FontCount() ? sFonts[choice - 1].name : "Original";
}

int MenuText_FontId(int choice) {
    return choice > 0 && choice < MenuText_FontCount() ? sFonts[choice - 1].id : 0;
}

void MenuText_SetFontId(int id) {
    int i, choice = 0;
    for (i = 1; i < MenuText_FontCount(); i++) {
        if (MenuText_FontId(i) == id) { choice = i; break; }
    }
    MenuText_SetFont(choice);
}

int MenuText_GetFont(void) { return sFontChoice; }

void MenuText_SetFont(int choice) {
    choice = choice < 0 || choice >= MenuText_FontCount() ? 0 : choice;
    if (choice == sFontChoice) { return; }
    sFontChoice = choice;
    free(sFontData);
    sFontData = NULL;
    memset(&sFont, 0, sizeof(sFont));
    sCount = sLoaded = sCapUnits = 0;
}

static void menu_text_name_styles(const char *dir) {
    char path[1200], line[256];
    FILE *fp;
    int selected;
    for (selected = 0; selected < 3; selected++) {
        MenuLabel *label = &sNameStyles[selected];
        memset(label, 0, sizeof(*label));
        label->width = label->rowWidth = 512; label->height = label->rowHeight = selected == 2 ? 64 : 32;
        label->cap = selected == 2 ? 21 : 16; label->baseline = selected == 2 ? 44 : 22;
        label->center = selected == 2; label->margin = selected == 2 ? 8 : 4; label->widthMilli = 1000;
        label->top = selected == 2 ? 0xEDD57E : selected ? 0xF1E484 : 0xCBCBCB;
        label->bottom = selected ? 0xD2684B : 0xCBCBCB;
        label->outline = selected ? 0x2C151B : 0x460059;
    }
    snprintf(path, sizeof(path), "%s/name-styles.tsv", dir);
    fp = fopen(path, "rb");
    if (fp == NULL) { /* catalogs exported before dynamic stage names */
        snprintf(path, sizeof(path), "%s/song-styles.tsv", dir);
        fp = fopen(path, "rb");
    }
    if (fp == NULL) { return; }
    while (fgets(line, sizeof(line), fp) != NULL) {
        MenuLabel label;
        memset(&label, 0, sizeof(label));
        if (sscanf(line, "%d %d %d %d %d %x %x %x", &selected, &label.cap, &label.baseline, &label.margin,
                   &label.widthMilli, &label.top, &label.bottom, &label.outline) != 8 || selected < 0 || selected > 2 ||
            label.cap < 1 || label.cap > (selected == 2 ? 64 : 32) || label.baseline < 0 || label.baseline > (selected == 2 ? 64 : 32) || label.margin < 0 || label.margin >= 256 ||
            label.widthMilli < 500 || label.widthMilli > 2000 || label.top > 0xFFFFFF || label.bottom > 0xFFFFFF || label.outline > 0xFFFFFF) { continue; }
        label.width = label.rowWidth = 512; label.height = label.rowHeight = selected == 2 ? 64 : 32;
        label.center = selected == 2;
        sNameStyles[selected] = label;
    }
    fclose(fp);
}

static void menu_text_load(void) {
    const char *dir;
    char root[1024], path[1200], line[1024];
    FILE *fp;
    long size;
    int offset, version = 1;

    sLoaded = 1;
    dir = menu_text_directory(root, sizeof(root));
    if (!dir[0]) { return; } /* explicitly disabled */
    snprintf(path, sizeof(path), "%s/labels.tsv", dir);
    fp = fopen(path, "rb");
    if (fp == NULL) { return; }
    while (sCount < MENU_TEXT_MAX && fgets(line, sizeof(line), fp) != NULL) {
        MenuLabel label;
        unsigned long long hash;
        int pos = 0, i;
        size_t n;
        if (line[0] == '#') { if (strstr(line, "BT3 menu text v4:")) { version = 4; } else if (strstr(line, "BT3 menu text v3:")) { version = 3; } else if (strstr(line, "BT3 menu text v2:")) { version = 2; } continue; }
        memset(&label, 0, sizeof(label));
        label.widthMilli = 1000;
        if (version == 4) {
            if (sscanf(line, "%llx\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%x\t%x\t%x\t%n",
                       &hash, &label.width, &label.height, &label.y, &label.rowHeight, &label.cap,
                       &label.baseline, &label.center, &label.margin, &label.widthMilli, &label.x, &label.rowWidth,
                       &label.artWidth, &label.top, &label.bottom, &label.outline, &pos) != 16 || pos == 0) { continue; }
        } else if (version == 3) {
            if (sscanf(line, "%llx\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%x\t%x\t%x\t%n",
                       &hash, &label.width, &label.height, &label.y, &label.rowHeight, &label.cap,
                       &label.baseline, &label.center, &label.margin, &label.widthMilli, &label.x, &label.rowWidth,
                       &label.top, &label.bottom, &label.outline, &pos) != 15 || pos == 0) { continue; }
        } else if (version == 2) {
            if (sscanf(line, "%llx\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%x\t%x\t%x\t%n",
                       &hash, &label.width, &label.height, &label.y, &label.rowHeight, &label.cap,
                       &label.baseline, &label.center, &label.margin, &label.widthMilli,
                       &label.top, &label.bottom, &label.outline, &pos) != 13 || pos == 0) { continue; }
        } else {
            if (sscanf(line, "%llx\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%x\t%x\t%x\t%n",
                       &hash, &label.width, &label.height, &label.y, &label.rowHeight, &label.cap,
                       &label.baseline, &label.center, &label.margin, &label.top, &label.bottom,
                       &label.outline, &pos) != 12 || pos == 0) { continue; }
        }
        if (version < 3) { label.rowWidth = label.width; }
        n = strcspn(line + pos, "\r\n");
        if (n == 0 || n >= sizeof(label.text) || (label.width < 8 || label.width > MENU_TEXT_WIDTH || (label.width & (label.width - 1))) || label.height < 1 || label.height > MENU_TEXT_HEIGHT ||
            label.y < 0 || label.rowHeight < 1 || label.rowHeight > 64 || label.y > label.height - label.rowHeight ||
            label.cap < 1 || label.cap > 64 || label.baseline < 0 || label.baseline > label.rowHeight ||
            label.widthMilli < 500 || label.widthMilli > 2000 || label.x < 0 || label.rowWidth < 1 || label.x > label.width - label.rowWidth ||
            label.artWidth < 0 || label.artWidth > label.rowWidth || label.margin < 0 || label.margin * 2 >= label.rowWidth || label.top > 0xFFFFFF || label.bottom > 0xFFFFFF || label.outline > 0xFFFFFF) {
            continue;
        }
        for (i = 0; i < (int)n; i++) {
            if ((unsigned char)line[pos + i] < 32) { break; }
        }
        if (i != (int)n) { continue; } /* no control characters silently dropped */
        memcpy(label.text, line + pos, n);
        label.hash = (uint64_t)hash;
        sLabels[sCount++] = label;
    }
    fclose(fp);
    if (sCount == 0) { return; }
    if (sFontChoice <= 0 || sFontChoice >= MenuText_FontCount()) { sCount = 0; return; }
    snprintf(path, sizeof(path), "%s/fonts/%s", dir, sFonts[sFontChoice - 1].file);
    fp = fopen(path, "rb");
    if (fp == NULL) { sCount = 0; return; }
    if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 256 || size > 4 * 1024 * 1024 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp); sCount = 0; return;
    }
    sFontData = malloc((size_t)size);
    if (sFontData == NULL || fread(sFontData, 1, (size_t)size, fp) != (size_t)size) {
        fclose(fp); free(sFontData); sFontData = NULL; sCount = 0; return;
    }
    fclose(fp);
    offset = stbtt_GetFontOffsetForIndex(sFontData, 0);
    if (offset < 0 || !stbtt_InitFont(&sFont, sFontData, offset)) {
        free(sFontData); sFontData = NULL; sCount = 0; return;
    }
    {
        int x0, y0, x1, y1;
        if (!stbtt_GetCodepointBox(&sFont, 'H', &x0, &y0, &x1, &y1) || y1 <= y0) { sCount = 0; return; }
        sCapUnits = y1 - y0;
    }
    menu_text_name_styles(dir);
    fprintf(stderr, "bt3: editable menu text: %d labels from %s\n", sCount, dir);
}

/* Decode strictly, skipping characters absent from the selected font. Malformed UTF-8 keeps the original row. */
static int menu_text_codes(const char *text, int *codes) {
    const unsigned char *p = (const unsigned char *)text;
    int count = 0;
    while (*p) {
        unsigned c = *p++, minimum = 0;
        int n = 0, i;
        if (c >= 0xC2 && c <= 0xDF) { n = 1; c &= 31; minimum = 0x80; }
        else if (c >= 0xE0 && c <= 0xEF) { n = 2; c &= 15; minimum = 0x800; }
        else if (c >= 0xF0 && c <= 0xF4) { n = 3; c &= 7; minimum = 0x10000; }
        else if (c >= 128) { return 0; }
        for (i = 0; i < n; i++) {
            if ((*p & 0xC0) != 0x80) { return 0; }
            c = (c << 6) | (*p++ & 63);
        }
        if (c < minimum || c < 32 || (c >= 0x7F && c <= 0x9F) || c > 0x10FFFF ||
            (c >= 0xD800 && c <= 0xDFFF)) { return 0; }
        if (stbtt_FindGlyphIndex(&sFont, (int)c) == 0) { continue; }
        codes[count++] = (int)c;
    }
    return count;
}

static int menu_text_row(uint8_t *dst, const MenuLabel *label, int resolution) {
    unsigned char *fill, *body, *blur;
    float *distance;
    int codes[256], len = menu_text_codes(label->text, codes);
    int i, x, y, c, advance, bearing, previous = 0, width = 0;
    int w = label->rowWidth * resolution, h = label->rowHeight * resolution, count = w * h;
    int radius = resolution, window = radius * 2 + 1;
    int padding = label->rowWidth < 32 ? label->rowWidth / 8 : 4;
    int border = label->rowWidth < 48 ? label->rowWidth / 16 : 3;
    if (border < 1) { border = 1; }
    float scaleY = label->cap * resolution / (float)sCapUnits;
    float scaleX = scaleY * label->widthMilli / 1000.0f;
    int inkLeft = 0, inkRight = 0, hasInk = 0;
    float pen, start, baseline = (float)label->baseline * resolution;
    float cap = label->cap * resolution;

    if (!len) { return 0; }
    for (i = 0; i < len; i++) {
        stbtt_GetCodepointHMetrics(&sFont, codes[i], &advance, &bearing);
        if (label->artWidth) {
            int x0, y0, x1, y1;
            int at = width + (previous ? stbtt_GetCodepointKernAdvance(&sFont, previous, codes[i]) : 0);
            if (stbtt_GetCodepointBox(&sFont, codes[i], &x0, &y0, &x1, &y1)) {
                if (!hasInk || at + x0 < inkLeft) { inkLeft = at + x0; }
                if (!hasInk || at + x1 > inkRight) { inkRight = at + x1; }
                hasInk = 1;
            }
        }
        width += advance + (previous ? stbtt_GetCodepointKernAdvance(&sFont, previous, codes[i]) : 0);
        previous = codes[i];
    }
    if (label->artWidth && hasInk && inkRight > inkLeft) {
        width = inkRight - inkLeft;
        scaleX = label->artWidth * resolution / (float)width * label->widthMilli / 1000.0f;
    }
    if (width <= 0) { return 0; }
    if (width * scaleX > w - (2 * label->margin + 2 * padding) * resolution) {
        float ratio = (w - (2 * label->margin + 2 * padding) * resolution) / (width * scaleX);
        scaleX *= ratio; scaleY *= ratio; cap *= ratio;
        baseline = h * 0.5f + (baseline - h * 0.5f) * ratio;
    }
    start = label->center ? (w - width * scaleX) * 0.5f : (float)label->margin * resolution;
    if (label->artWidth && hasInk) {
        start -= inkLeft * scaleX;
    } else if (!label->center) {
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&sFont, codes[0], scaleX, scaleY, &x0, &y0, &x1, &y1);
        start -= x0; /* align the glyph's visible edge, rather than its font-specific bearing */
    }
    fill = calloc((size_t)count, 1);
    body = malloc((size_t)count);
    blur = malloc((size_t)count);
    distance = malloc((size_t)count * sizeof(float));
    if (fill == NULL || body == NULL || blur == NULL || distance == NULL) {
        free(fill); free(body); free(blur); free(distance); return 0;
    }
    pen = start; previous = 0;
    for (i = 0; i < len; i++) {
        int bw, gh, ox, oy, px0, py0;
        unsigned char *bitmap;
        if (previous) { pen += stbtt_GetCodepointKernAdvance(&sFont, previous, codes[i]) * scaleX; }
        px0 = (int)floorf(pen); py0 = (int)floorf(baseline);
        bitmap = stbtt_GetCodepointBitmapSubpixel(&sFont, scaleX, scaleY, pen - px0, baseline - py0,
                                                codes[i], &bw, &gh, &ox, &oy);
        if (bitmap != NULL) {
            for (y = 0; y < gh; y++) {
                for (x = 0; x < bw; x++) {
                    int px = px0 + ox + x, py = py0 + oy + y;
                    if (px >= 0 && px < w && py >= 0 && py < h && bitmap[y*bw+x] > fill[py*w+px]) {
                        fill[py*w+px] = bitmap[y*bw+x];
                    }
                }
            }
            stbtt_FreeBitmap(bitmap, NULL);
        }
        stbtt_GetCodepointHMetrics(&sFont, codes[i], &advance, &bearing);
        pen += advance * scaleX; previous = codes[i];
    }
    /* Two distance sweeps keep rounded outlines inexpensive at high texture resolutions. */
    for (i = 0; i < count; i++) { distance[i] = fill[i] ? 1.0f - fill[i]/255.0f : 1e6f; }
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int at = y*w+x;
            float d = distance[at];
            if (x) { d = fminf(d, distance[at-1]+1); }
            if (y) {
                d = fminf(d, distance[at-w]+1);
                if (x) { d = fminf(d, distance[at-w-1]+1.414214f); }
                if (x+1 < w) { d = fminf(d, distance[at-w+1]+1.414214f); }
            }
            distance[at] = d;
        }
    }
    for (y = h-1; y >= 0; y--) {
        for (x = w-1; x >= 0; x--) {
            int at = y*w+x;
            float d = distance[at], coverage;
            if (x+1 < w) { d = fminf(d, distance[at+1]+1); }
            if (y+1 < h) {
                d = fminf(d, distance[at+w]+1);
                if (x) { d = fminf(d, distance[at+w-1]+1.414214f); }
                if (x+1 < w) { d = fminf(d, distance[at+w+1]+1.414214f); }
            }
            distance[at] = d;
            coverage = fminf(1, fmaxf(0, border*resolution + 0.5f - d));
            body[at] = (unsigned char)fmaxf(fill[at], coverage*255);
        }
    }
    /* Separable box blur for the offset shadow. */
    for (y = 0; y < h; y++) {
        int sum = 0;
        for (x = 0; x <= radius && x < w; x++) { sum += body[y*w+x]; }
        for (x = 0; x < w; x++) {
            blur[y*w+x] = (unsigned char)(sum/window);
            if (x-radius >= 0) { sum -= body[y*w+x-radius]; }
            if (x+radius+1 < w) { sum += body[y*w+x+radius+1]; }
        }
    }
    for (x = 0; x < w; x++) {
        int sum = 0;
        for (y = 0; y <= radius && y < h; y++) { sum += blur[y*w+x]; }
        for (y = 0; y < h; y++) {
            distance[y*w+x] = sum/(window*255.0f)*0.65f;
            if (y-radius >= 0) { sum -= blur[(y-radius)*w+x]; }
            if (y+radius+1 < h) { sum += blur[(y+radius+1)*w+x]; }
        }
    }
    for (y = 0; y < h; y++) {
        float ramp = (y - (baseline - cap)) / fmaxf(1, cap - 1);
        ramp = fminf(1, fmaxf(0, ramp));
        for (x = 0; x < w; x++) {
            int at = y*w+x, sx = x-4*resolution, sy = y-3*resolution;
            float f = fill[at]/255.0f, b = body[at]/255.0f;
            float shadow = sx >= 0 && sy >= 0 ? distance[sy*w+sx] : 0;
            float alpha = b + shadow*(1-b);
            for (c = 0; c < 3; c++) {
                int shift = 16-c*8;
                float color = ((label->top >> shift)&255)*(1-ramp) + ((label->bottom >> shift)&255)*ramp;
                float premult = color*f + ((label->outline >> shift)&255)*(b-f);
                dst[at*4+c] = alpha > 0 ? (unsigned char)(premult/alpha + 0.5f) : (unsigned char)(label->outline >> shift);
            }
            dst[at*4+3] = (unsigned char)(alpha*127.0f + 0.5f);
        }
    }
    free(fill); free(body); free(blur); free(distance);
    return 1;
}

uint8_t *MenuText_Render(const uint8_t *rgba, int width, int height, int scale, int *outWidth, int *outHeight) {
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i, bytes;
    int n, x, y, changed = 0, rw, rh;
    uint8_t *result = NULL;
    if (sFontChoice == 0) { return NULL; } /* Original: use the untouched game sprites. */
    if (!sLoaded) { menu_text_load(); }
    if (sCount == 0 || rgba == NULL || (width < 8 || width > MENU_TEXT_WIDTH || (width & (width - 1))) || height < 1 || height > MENU_TEXT_HEIGHT ||
        scale < 1 || scale > 8 || outWidth == NULL || outHeight == NULL) { return NULL; }
    bytes = (size_t)width * height * 4;
    rw = width * scale; rh = height * scale;
    for (i = 0; i < bytes; i++) { hash = (hash ^ rgba[i]) * UINT64_C(1099511628211); }
    for (n = 0; n < sCount; n++) {
        const MenuLabel *label = &sLabels[n];
        if (label->hash != hash || label->width != width || label->height != height) { continue; }
        if (result == NULL) {
            result = malloc((size_t)rw * rh * 4);
            if (result == NULL) { return NULL; }
            for (y = 0; y < rh; y++) {
                for (x = 0; x < rw; x++) {
                    memcpy(result + ((size_t)y*rw+x)*4, rgba + ((size_t)(y/scale)*width+x/scale)*4, 4);
                }
            }
        }
        {
            int rowWidth = label->rowWidth * scale, rowHeight = label->rowHeight * scale;
            uint8_t *row = malloc((size_t)rowWidth * rowHeight * 4);
            if (row != NULL && menu_text_row(row, label, scale)) {
                for (y = 0; y < rowHeight; y++) {
                    memcpy(result + ((size_t)(label->y * scale + y) * rw + label->x * scale) * 4,
                           row + (size_t)y * rowWidth * 4, (size_t)rowWidth * 4);
                }
                changed++;
            }
            free(row);
        }
    }
    if (!changed) { free(result); return NULL; }
    *outWidth = rw; *outHeight = rh;
    return result;
}

static uint8_t *menu_text_render_name(const char *text, int kind, int scale, int *outWidth, int *outHeight) {
    MenuLabel label;
    uint8_t *result;
    size_t bytes, i, length;
    if (sFontChoice == 0 || text == NULL || kind < 0 || kind > 2 || scale < 1 || scale > 8 ||
        outWidth == NULL || outHeight == NULL) { return NULL; }
    if (!sLoaded) { menu_text_load(); }
    if (sCount == 0) { return NULL; }
    length = strlen(text);
    if (length == 0 || length >= sizeof(label.text)) { return NULL; }
    label = sNameStyles[kind];
    memcpy(label.text, text, length + 1);
    bytes = (size_t)512 * label.rowHeight * scale * scale * 4;
    result = malloc(bytes);
    if (result == NULL) { return NULL; }
    if (!menu_text_row(result, &label, scale)) { free(result); return NULL; }
    /* Menu texture pixels use PS2 alpha; the ImGui overlay uses conventional RGBA. */
    for (i = 3; i < bytes; i += 4) { result[i] = (uint8_t)((unsigned)result[i] * 255 / 128); }
    *outWidth = 512 * scale; *outHeight = label.rowHeight * scale;
    return result;
}

uint8_t *MenuText_RenderSong(const char *text, int selected, int scale, int *outWidth, int *outHeight) {
    if (selected < 0 || selected > 1) { return NULL; }
    return menu_text_render_name(text, selected, scale, outWidth, outHeight);
}

uint8_t *MenuText_RenderStage(const char *text, int scale, int *outWidth, int *outHeight) {
    return menu_text_render_name(text, 2, scale, outWidth, outHeight);
}
