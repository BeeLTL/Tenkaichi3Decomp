/* Optional editable menu labels rendered from a vector font. */
#ifndef GS_MENU_TEXT_H
#define GS_MENU_TEXT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Original GS RGBA pixels (128 = opaque). Returns an allocated texture with vector
   text at scale 1..8, or NULL when no row was replaced. Caller owns the result.
   Original pixels are never modified; output dimensions are set only on success. */
uint8_t *MenuText_Render(const uint8_t *rgba, int width, int height, int scale, int *outWidth, int *outHeight);
/* 0: Original game sprites (default); other choices come from the generated font catalog.
   Switching reloads the labels/font on next use; the renderer keys cached textures by this choice. */
int MenuText_GetFont(void);
void MenuText_SetFont(int choice);
int MenuText_FontCount(void);
const char *MenuText_FontName(int choice);
int MenuText_FontId(int choice);
void MenuText_SetFontId(int id);
/* External song name in the disc's 512x32 row, using the selected vector font.
   Skips missing glyphs. Returns allocated ordinary RGBA (255 opaque), or NULL for Original/no usable text. */
uint8_t *MenuText_RenderSong(const char *text, int selected, int scale, int *outWidth, int *outHeight);
/* Added stage name centered in the disc's 512x64 row, with ordinary RGBA alpha. */
uint8_t *MenuText_RenderStage(const char *text, int scale, int *outWidth, int *outHeight);
#ifdef __cplusplus
}
#endif
#endif
