#include "LuaImageMain.h"
#include "LuaImage.h"

static const struct luaL_Reg imagefunctions[] = {
	{ "Open",        Image_Open },
	{ "New",         Image_New },
	{ "FromBytes",   Image_FromBytes },
	{ "GetMetadata", Image_GetMetadata },
	{ "SetMetadata", Image_SetMetadata },
	{ "GetWidth",    Image_GetWidth },
	{ "GetHeight",   Image_GetHeight },
	{ "GetPixel",    Image_GetPixel },
	{ "SetPixel",    Image_SetPixel },
	{ "Crop",        Image_Crop },
	{ "Resize",      Image_Resize },
	{ "Clone",       Image_Clone },
	{ "FillRect",    Image_FillRect },
	{ "DrawLine",    Image_DrawLine },
	{ "DrawCircle",  Image_DrawCircle },
	{ "Composite",   Image_Composite },
	{ "Diff",        Image_Diff },
	{ "Save",        Image_Save },
	{ "ToBytes",     Image_ToBytes },

	{ "RGBtoHSV",       Image_RGBtoHSV },
	{ "HSVtoRGB",       Image_HSVtoRGB },
	{ "RGBtoHSL",       Image_RGBtoHSL },
	{ "HSLtoRGB",       Image_HSLtoRGB },

	{ "Tint",           Image_Tint },
	{ "RecolorPalette", Image_RecolorPalette },
	{ "AdjustHSV",      Image_AdjustHSV },
	{ "Dither",         Image_Dither },
	{ "Outline",        Image_Outline },

	{ "Stamp",          Image_Stamp },
	{ "StrokePath",     Image_StrokePath },

	{ "FlipHorizontal", Image_FlipHorizontal },
	{ "FlipVertical",   Image_FlipVertical },
	{ "Rotate90",       Image_Rotate90 },
	{ "PadCanvas",      Image_PadCanvas },
	{ "DropShadow",     Image_DropShadow },

	{ "Blur",                     Image_Blur },
	{ "Invert",                   Image_Invert },
	{ "Grayscale",                Image_Grayscale },
	{ "AdjustBrightnessContrast", Image_AdjustBrightnessContrast },
	{ "Threshold",                Image_Threshold },
	{ "ApplyMask",                Image_ApplyMask },
	{ "Noise",                    Image_Noise },

	{ "FillGradientLinear", Image_FillGradientLinear },
	{ "FillGradientRadial", Image_FillGradientRadial },
	{ "FillPolygon",        Image_FillPolygon },
	{ "DrawText",           Image_DrawText },

	{ "ExtractPalette", Image_ExtractPalette },
	{ NULL, NULL }
};

static const luaL_Reg imagemeta[] = {
	{ "__gc", image_gc },
	{ "__tostring", image_tostring },
	{ NULL, NULL }
};

int luaopen_image(lua_State *L) {

	luaL_newlibtable(L, imagefunctions);
	luaL_setfuncs(L, imagefunctions, 0);

	luaL_newmetatable(L, LUAIMAGE);
	luaL_setfuncs(L, imagemeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}
