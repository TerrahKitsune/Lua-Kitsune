#pragma once
#include "lua_main_incl.h"

static const char* LUAIMAGE = "LuaImage";

// A single PNG text tag (tEXt/zTXt/iTXt keyword+value pair), key/value are
// kitsune_malloc'd NUL-terminated strings.
typedef struct ImageTag {
	char* key;
	char* value;
} ImageTag;

// Plain-old-data userdata payload: safe to lua_newuserdata + memset(0), no
// C++ constructors involved (mirrors LuaProcess's own POD + memset pattern).
typedef struct LuaImage {
	int width;
	int height;
	unsigned char* pixels;   // RGBA8, straight alpha, width*height*4 bytes, kitsune_malloc'd
	ImageTag* tags;          // kitsune_malloc'd array, may be NULL if tagCount == 0
	int tagCount;
	int tagCapacity;
} LuaImage;

LuaImage* lua_toimage(lua_State* L, int index);
LuaImage* lua_pushimage(lua_State* L);

// -- module-level (Image.X) --
int Image_Open(lua_State* L);
int Image_New(lua_State* L);
int Image_FromBytes(lua_State* L);

// -- methods (img:X) --
int Image_GetMetadata(lua_State* L);
int Image_SetMetadata(lua_State* L);
int Image_GetWidth(lua_State* L);
int Image_GetHeight(lua_State* L);
int Image_GetPixel(lua_State* L);
int Image_SetPixel(lua_State* L);
int Image_Crop(lua_State* L);
int Image_Resize(lua_State* L);
int Image_Clone(lua_State* L);
int Image_FillRect(lua_State* L);
int Image_DrawLine(lua_State* L);
int Image_DrawCircle(lua_State* L);
int Image_Composite(lua_State* L);
int Image_Diff(lua_State* L);
int Image_Save(lua_State* L);
int Image_ToBytes(lua_State* L);

int image_gc(lua_State* L);
int image_tostring(lua_State* L);

// -- color conversion utilities (Image.X, pure, no Image object involved) --
int Image_RGBtoHSV(lua_State* L);
int Image_HSVtoRGB(lua_State* L);
int Image_RGBtoHSL(lua_State* L);
int Image_HSLtoRGB(lua_State* L);

// -- recolor / tone (img:X, mutating) --
int Image_Tint(lua_State* L);
int Image_RecolorPalette(lua_State* L);
int Image_AdjustHSV(lua_State* L);
int Image_Dither(lua_State* L);
int Image_Outline(lua_State* L);

// -- brush painting (img:X, mutating) --
int Image_Stamp(lua_State* L);
int Image_StrokePath(lua_State* L);

// -- geometric transforms (img:X, non-mutating, return a new Image) --
int Image_FlipHorizontal(lua_State* L);
int Image_FlipVertical(lua_State* L);
int Image_Rotate90(lua_State* L);
int Image_PadCanvas(lua_State* L);
int Image_DropShadow(lua_State* L);

// -- filters / tone (img:X, mutating) --
int Image_Blur(lua_State* L);
int Image_Invert(lua_State* L);
int Image_Grayscale(lua_State* L);
int Image_AdjustBrightnessContrast(lua_State* L);
int Image_Threshold(lua_State* L);
int Image_ApplyMask(lua_State* L);
int Image_Noise(lua_State* L);

// -- fill primitives (img:X, mutating) --
int Image_FillGradientLinear(lua_State* L);
int Image_FillGradientRadial(lua_State* L);
int Image_FillPolygon(lua_State* L);
int Image_DrawText(lua_State* L);

// -- analysis (img:X, read-only) --
int Image_ExtractPalette(lua_State* L);
