/* Optional editable menu labels rendered from a vector font. */
#ifndef GS_MENU_TEXT_H
#define GS_MENU_TEXT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Original GS RGBA pixels (128 = opaque) of a texture; `indexed`: it has a palette (the labels all do). Returns
   an allocated texture `scale` times the size with the labels drawn from the font, or NULL when no row was
   replaced. Caller owns the result. Original pixels are never modified; output dimensions are set only on success. */
uint8_t *MenuText_Render(const uint8_t *rgba, int width, int height, int indexed, int scale, int *outWidth, int *outHeight);
/* The size text is drawn at for an internal resolution: that resolution, at least 2 and at most 4 times the PS2's. */
int MenuText_RasterScale(int internalScale);
/* For the renderer's texture cache: a value that changes when a font setting or `scale` changes what
   MenuText_Render / MenuText_RenderGlyphs make of this texture. 0 with both settings on Original. */
uint32_t MenuText_TexKey(int width, int height, int indexed, int tbp, int cbp, int scale);
/* 0: Original game sprites (default); other choices come from the font list (fonts.tsv next to the fonts).
   A font is read and checked when it is chosen; one that cannot be used leaves the choice on Original. */
int MenuText_GetFont(void);
void MenuText_SetFont(int choice);
int MenuText_FontCount(void);
int MenuText_FontAvailable(int choice, int shared);
const char *MenuText_FontName(int choice);
int MenuText_FontId(int choice);
void MenuText_SetFontId(int id);
/* Shared Font_Print glyphs have a separate choice; atlas replacements retain game metrics and styling. */
int MenuText_GetSharedFont(void);
void MenuText_SetSharedFont(int choice);
void MenuText_SetSharedFontId(int id);
void MenuText_RegisterGlyphs(int tbp, int cbp, const void *glyphs, int count, int lineHeight);
uint8_t *MenuText_RenderGlyphs(const uint8_t *rgba, int width, int height, int tbp, int cbp,
                             int scale, int *outWidth, int *outHeight);
/* Packed glyph x/y/width/height, top/bottom padding, atlas width/height, in logical pixels. */
int MenuText_MapGlyph(int tbp, int cbp, int width, int height,
                     float u0, float v0, float u1, float v1, float *rect);
/* External song name in the disc's 512x32 row, using the selected vector font.
   Skips missing glyphs. Returns allocated ordinary RGBA (255 opaque), or NULL for Original/no usable text. */
uint8_t *MenuText_RenderSong(const char *text, int selected, int scale, int *outWidth, int *outHeight);
/* Added stage name centered in the disc's 512x64 row, with ordinary RGBA alpha. */
uint8_t *MenuText_RenderStage(const char *text, int scale, int *outWidth, int *outHeight);
#ifdef __cplusplus
}
#endif
#endif
