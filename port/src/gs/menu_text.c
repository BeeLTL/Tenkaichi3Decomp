/* Editable labels occupy the original texture rows, so Flash animation, tint, clipping and layout keep
   working. menu_font.py installs a vector font and writes labels.json / labels.tsv.
   Files are read once on first texture use. No files: the original artwork is used without hashing it.
   Texture-pack replacements take precedence. The original game state and GS memory are never modified. */
#include "menu_text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
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
    int id, usage;
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
        char *name, *usage;
        size_t n, i;
        unsigned id = 2166136261u;
        if (line[0] == '#' || (name = strchr(line, '\t')) == NULL) { continue; }
        *name++ = '\0';
        font.usage = 3;
        usage = strchr(name, '\t');
        if (usage != NULL) { *usage++ = '\0'; font.usage = atoi(usage) & 3; }
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

int MenuText_FontAvailable(int choice, int shared) {
    return choice == 0 || (choice > 0 && choice < MenuText_FontCount() &&
                          (sFonts[choice - 1].usage & (shared ? 2 : 1)) != 0);
}

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
    choice = MenuText_FontAvailable(choice, 0) ? choice : 0;
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

/* The game's font sheets pack four glyph planes into one indexed texture. Each CLUT selects one plane.
   Only glyph cells are replaced; sprites, tags and the game's shadow/color/clip passes stay unchanged. */
typedef struct SharedGlyph { uint16_t code, u, v; } SharedGlyph;
typedef struct SharedSheet {
    int tbp, cbp, count, cap, baseline, calibrated;
    SharedGlyph *glyphs;
} SharedSheet;
typedef struct SharedRect { int x, y, w, h, top, bottom; } SharedRect;
typedef struct SharedLayout {
    int choice, cap, baseline, width, sourceHeight, height;
    SharedRect *rects;
} SharedLayout;
static SharedSheet sSharedSheets[7];
static SharedLayout sSharedLayouts[7][4];
static int sSharedChoice, sSharedLoaded;
static unsigned char *sSharedData;
static stbtt_fontinfo sSharedFont;
static int sSharedCap;
static pthread_mutex_t sSharedLock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t sSharedGeneration;

int MenuText_GetSharedFont(void) { return sSharedChoice; }

uint32_t MenuText_SharedGeneration(void) {
    uint32_t generation;
    pthread_mutex_lock(&sSharedLock); generation = sSharedGeneration; pthread_mutex_unlock(&sSharedLock);
    return generation;
}

void MenuText_SetSharedFont(int choice) {
    if (!MenuText_FontAvailable(choice, 1)) { choice = 0; }
    if (choice == sSharedChoice) { return; }
    pthread_mutex_lock(&sSharedLock);
    sSharedChoice = choice; sSharedLoaded = sSharedCap = 0;
    free(sSharedData); sSharedData = NULL;
    memset(&sSharedFont, 0, sizeof(sSharedFont));
    pthread_mutex_unlock(&sSharedLock);
}

void MenuText_SetSharedFontId(int id) {
    int i, choice = 0;
    for (i = 1; i < MenuText_FontCount(); i++) {
        if (MenuText_FontId(i) == id) { choice = i; break; }
    }
    MenuText_SetSharedFont(choice);
}

void MenuText_RegisterGlyphs(int tbp, int cbp, const void *glyphs, int count, int lineHeight) {
    SharedSheet *sheet;
    int slot = cbp >= 0x2A00 ? (cbp - 0x2A00) / 0x208 : -1, i;
    if (slot < 0 || slot >= 7 || cbp != 0x2A00 + slot * 0x208 || tbp != cbp + 8 ||
        glyphs == NULL || count < 1 || count > 65536 || lineHeight < 1 || lineHeight > 64) { return; }
    pthread_mutex_lock(&sSharedLock);
    sheet = &sSharedSheets[slot];
    if (sheet->count == count && sheet->tbp == tbp && sheet->cbp == cbp &&
        memcmp(sheet->glyphs, glyphs, (size_t)count * sizeof(*sheet->glyphs)) == 0) {
        pthread_mutex_unlock(&sSharedLock); return;
    }
    if (sheet->count != count) {
        SharedGlyph *copy = realloc(sheet->glyphs, (size_t)count * sizeof(*copy));
        if (copy == NULL) { pthread_mutex_unlock(&sSharedLock); return; }
        sheet->glyphs = copy;
    }
    memcpy(sheet->glyphs, glyphs, (size_t)count * sizeof(*sheet->glyphs));
    sheet->tbp = tbp; sheet->cbp = cbp; sheet->count = count;
    sheet->cap = lineHeight;
    for (i = 0; i < count; i++) {
        if (sheet->glyphs[i].code == 'H') {
            int h = (sheet->glyphs[i].v >> 10) & 31;
            if (h) { sheet->cap = h; }
            break;
        }
    }
    sheet->baseline = sheet->cap; sheet->calibrated = 0;
    sSharedGeneration++;
    for (i = 0; i < 4; i++) {
        SharedLayout *layout = &sSharedLayouts[slot][i];
        free(layout->rects); memset(layout, 0, sizeof(*layout));
    }
    pthread_mutex_unlock(&sSharedLock);
}

static int menu_text_shared_load(void) {
    char root[1024], path[1400];
    const char *dir;
    FILE *fp;
    long size;
    int offset, x0, y0, x1, y1;
    if (sSharedLoaded) { return sSharedCap > 0; }
    sSharedLoaded = 1;
    if (!sSharedChoice || sSharedChoice >= MenuText_FontCount()) { return 0; }
    dir = menu_text_directory(root, sizeof(root));
    if (!dir[0]) { return 0; }
    snprintf(path, sizeof(path), "%s/fonts/%s", dir, sFonts[sSharedChoice - 1].file);
    fp = fopen(path, "rb");
    if (fp == NULL) { return 0; }
    if (fseek(fp, 0, SEEK_END) || (size = ftell(fp)) < 256 || size > 4 * 1024 * 1024 || fseek(fp, 0, SEEK_SET)) {
        fclose(fp); return 0;
    }
    sSharedData = malloc((size_t)size);
    if (!sSharedData || fread(sSharedData, 1, (size_t)size, fp) != (size_t)size) {
        fclose(fp); free(sSharedData); sSharedData = NULL; return 0;
    }
    fclose(fp);
    offset = stbtt_GetFontOffsetForIndex(sSharedData, 0);
    if (offset < 0 || !stbtt_InitFont(&sSharedFont, sSharedData, offset) ||
        !stbtt_GetCodepointBox(&sSharedFont, 'H', &x0, &y0, &x1, &y1) || y1 <= y0) { return 0; }
    sSharedCap = y1 - y0;
    fprintf(stderr, "bt3: shared text font: %s\n", MenuText_FontName(sSharedChoice));
    return 1;
}

/* Each plane gets its own packed atlas. Padding follows the selected font's actual bounds, so
   descenders and accents fit without changing the cap height or borrowing pixels from the next glyph. */
static SharedLayout *menu_text_shared_layout(SharedSheet *sheet, int plane, int width, int height) {
    SharedLayout *layout = &sSharedLayouts[sheet - sSharedSheets][plane];
    int i, x = 1, y = 1, rowHeight = 0, bottom = 1;
    float sy = (float)sheet->cap / sSharedCap;
    if (layout->rects && layout->choice == sSharedChoice && layout->cap == sheet->cap &&
        layout->baseline == sheet->baseline && layout->width == width && layout->sourceHeight == height) { return layout; }
    free(layout->rects); memset(layout, 0, sizeof(*layout));
    layout->rects = calloc((size_t)sheet->count, sizeof(*layout->rects));
    if (!layout->rects) { return NULL; }
    for (i = 0; i < sheet->count; i++) {
        SharedGlyph *g = &sheet->glyphs[i];
        SharedRect *r = &layout->rects[i];
        int gx = g->u & 1023, gy = g->v & 1023, w = (g->u >> 10) & 31, h = (g->v >> 10) & 31;
        int x0, y0, x1, y1;
        if (((g->u >> 15) | ((g->v >> 15) << 1)) != plane || !w || !h || gx+w > width || gy+h > height) { continue; }
        if (stbtt_FindGlyphIndex(&sSharedFont, g->code) &&
            stbtt_GetCodepointBox(&sSharedFont, g->code, &x0, &y0, &x1, &y1) && x1 > x0 && y1 > y0) {
            int top = (int)floorf(sheet->baseline - y1 * sy);
            int end = (int)ceilf(sheet->baseline - y0 * sy);
            r->top = top < 0 ? -top : 0; r->bottom = end > h ? end-h : 0;
        }
        r->w = w; r->h = h + r->top + r->bottom;
        if (x + w + 1 > width) { x = 1; y += rowHeight + 2; rowHeight = 0; }
        r->x = x; r->y = y; x += w + 2;
        if (r->h > rowHeight) { rowHeight = r->h; }
        if (y + r->h + 1 > bottom) { bottom = y + r->h + 1; }
    }
    layout->height = 1;
    while (layout->height < bottom) { layout->height *= 2; }
    layout->choice = sSharedChoice; layout->cap = sheet->cap; layout->baseline = sheet->baseline;
    layout->width = width; layout->sourceHeight = height;
    return layout;
}

static uint8_t *menu_text_glyphs(const uint8_t *rgba, int width, int height, int tbp, int cbp,
                               int scale, int *outWidth, int *outHeight) {
    SharedSheet *sheet = NULL;
    uint8_t *result = NULL;
    SharedLayout *layout;
    int i, plane, rw, rh, changed = 0;
    if (!sSharedChoice || !rgba || width < 1 || width > 1024 || height < 1 || height > 1024 ||
        scale < 1 || scale > 8 || !outWidth || !outHeight) { return NULL; }
    for (i = 0; i < 7; i++) {
        if (sSharedSheets[i].count && sSharedSheets[i].tbp == tbp && cbp >= sSharedSheets[i].cbp &&
            cbp < sSharedSheets[i].cbp + 4) { sheet = &sSharedSheets[i]; break; }
    }
    if (!sheet || !menu_text_shared_load()) { return NULL; }
    plane = cbp - sheet->cbp; rw = width * scale; rh = height * scale;
    if (!sheet->calibrated) {
        for (i = 0; i < sheet->count; i++) {
            SharedGlyph *g = &sheet->glyphs[i];
            int x, y, top = 64, bottom = -1;
            int gx = g->u & 1023, gy = g->v & 1023, w = (g->u >> 10) & 31, h = (g->v >> 10) & 31;
            if (g->code != 'H' || ((g->u >> 15) | ((g->v >> 15) << 1)) != plane ||
                gx + w > width || gy + h > height) { continue; }
            for (y = 0; y < h; y++) {
                for (x = 0; x < w; x++) {
                    if (rgba[((size_t)(gy+y) * width + gx+x)*4+3] >= 64) {
                        if (y < top) { top = y; }
                        if (y > bottom) { bottom = y; }
                    }
                }
            }
            if (bottom >= top) {
                sheet->cap = bottom - top + 1; sheet->baseline = bottom + 1; sheet->calibrated = 1;
                sSharedGeneration++;
            }
            break;
        }
    }
    layout = menu_text_shared_layout(sheet, plane, width, height);
    if (!layout) { return NULL; }
    rw = width * scale; rh = layout->height * scale;
    result = calloc((size_t)rw * rh, 4);
    if (!result) { return NULL; }
    for (i = 0; i < sheet->count; i++) {
        SharedGlyph *g = &sheet->glyphs[i];
        SharedRect *r = &layout->rects[i];
        int gx = g->u & 1023, gy = g->v & 1023, w = (g->u >> 10) & 31, h = (g->v >> 10) & 31;
        int bx0, by0, bx1, by1, bw, bh, ox, oy, x, y;
        int left = w, maxAlpha = 0;
        uint8_t ink[3] = {128,128,128};
        float sy, sx;
        unsigned char *bitmap;
        if (!r->w) { continue; }
        /* Keep the original pixels for missing glyphs, including spaces and symbols. */
        for (y = 0; y < h * scale; y++) {
            for (x = 0; x < w * scale; x++) {
                memcpy(result + ((size_t)((r->y+r->top)*scale+y) * rw + r->x*scale+x)*4,
                       rgba + ((size_t)(gy+y/scale) * width + gx+x/scale)*4, 4);
            }
        }
        if (!stbtt_FindGlyphIndex(&sSharedFont, g->code) ||
            !stbtt_GetCodepointBox(&sSharedFont, g->code, &bx0, &by0, &bx1, &by1) || bx1 <= bx0 || by1 <= by0) { continue; }
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                const uint8_t *src = rgba + ((size_t)(gy+y) * width + gx+x)*4;
                if (src[3] >= 32 && x < left) { left = x; }
                if (src[3] > maxAlpha) { maxAlpha = src[3]; memcpy(ink, src, 3); }
            }
        }
        if (!maxAlpha || left == w) { continue; }
        sy = (float)sheet->cap * scale / sSharedCap;
        /* Keep the game's advance width. Only compress a glyph when its ink exceeds its original cell. */
        sx = fminf(sy, (float)((w-left) * scale) / (bx1 - bx0));
        bitmap = stbtt_GetCodepointBitmap(&sSharedFont, sx, sy, g->code, &bw, &bh, &ox, &oy);
        if (!bitmap) { continue; }
        for (y = 0; y < r->h * scale; y++) {
            memset(result + ((size_t)(r->y * scale + y) * rw + r->x * scale) * 4, 0, (size_t)w * scale * 4);
        }
        for (y = 0; y < bh; y++) {
            int py = (sheet->baseline + r->top) * scale + oy + y;
            for (x = 0; x < bw; x++) {
                int px = left * scale + x;
                if (px < w * scale && py >= 0 && py < r->h * scale) {
                    uint8_t *dst = result + ((size_t)(r->y * scale + py) * rw + r->x * scale + px) * 4;
                    memcpy(dst, ink, 3); /* 0x80 is neutral under GS color modulation */
                    dst[3] = (uint8_t)((bitmap[y*bw+x] * (unsigned)maxAlpha + 127) / 255);
                }
            }
        }
        stbtt_FreeBitmap(bitmap, NULL); changed = 1;
    }
    if (!changed) { free(result); return NULL; }
    *outWidth = rw; *outHeight = rh;
    return result;
}

uint8_t *MenuText_RenderGlyphs(const uint8_t *rgba, int width, int height, int tbp, int cbp,
                             int scale, int *outWidth, int *outHeight) {
    uint8_t *result;
    if (!sSharedChoice) { return NULL; }
    pthread_mutex_lock(&sSharedLock);
    result = menu_text_glyphs(rgba, width, height, tbp, cbp, scale, outWidth, outHeight);
    pthread_mutex_unlock(&sSharedLock);
    return result;
}


int MenuText_MapGlyph(int tbp, int cbp, int width, int height,
                     float u0, float v0, float u1, float v1, float *rect) {
    int slot, i, found = 0;
    if (!sSharedChoice || !rect || width < 1 || width > 1024 || height < 1 || height > 1024) { return 0; }
    pthread_mutex_lock(&sSharedLock);
    for (slot = 0; slot < 7; slot++) {
        SharedSheet *sheet = &sSharedSheets[slot];
        SharedLayout *layout;
        if (!sheet->count || sheet->tbp != tbp || cbp < sheet->cbp || cbp >= sheet->cbp+4 || !menu_text_shared_load()) { continue; }
        layout = menu_text_shared_layout(sheet, cbp-sheet->cbp, width, height);
        if (!layout) { break; }
        for (i = 0; i < sheet->count; i++) {
            SharedGlyph *g = &sheet->glyphs[i]; SharedRect *r = &layout->rects[i];
            int gx = g->u & 1023, gy = g->v & 1023, w = (g->u >> 10) & 31, h = (g->v >> 10) & 31;
            if (!r->w || fabsf(u0-(gx+0.5f)) > 0.01f || fabsf(v0-(gy+0.5f)) > 0.01f ||
                fabsf(u1-u0-w) > 0.01f || fabsf(v1-v0-h) > 0.01f) { continue; }
            rect[0]=r->x; rect[1]=r->y; rect[2]=r->w; rect[3]=r->h;
            rect[4]=r->top; rect[5]=r->bottom; rect[6]=layout->width; rect[7]=layout->height;
            found = 1; break;
        }
        break;
    }
    pthread_mutex_unlock(&sSharedLock);
    return found;
}
