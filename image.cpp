#include "image.h"
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h> 
#include <windows.h> 
#include "luawindow.h"

static int iterator;

BOOL CALLBACK MonitorEnumProcCallback(_In_  HMONITOR hMonitor, _In_  HDC DevC, _In_  LPRECT lprcMonitor, _In_  LPARAM dwData) {

	LuaImage * image = (LuaImage*)dwData;

	if (++iterator != image->screen) {
		return TRUE;
	}

	MONITORINFO  info;
	info.cbSize = sizeof(MONITORINFO);

	BOOL monitorInfo = GetMonitorInfo(hMonitor, &info);

	if (monitorInfo) {

		if (image->Height == 0) {
			image->Height = info.rcMonitor.bottom - info.rcMonitor.top;
		}

		if (image->Width == 0) {
			image->Width = info.rcMonitor.right - info.rcMonitor.left;
		}

		DWORD FileSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + (sizeof(RGBTRIPLE) + 1 * (image->Width*image->Height * 4));

		if (image->Data) {
			gff_free(image->Data);
		}

		image->Data = (BYTE*)gff_calloc(FileSize, sizeof(1));

		if (image->Data)
		{
			image->DataSize = FileSize;
		}
		else
		{
			return FALSE;
		}

		PBITMAPFILEHEADER BFileHeader = (PBITMAPFILEHEADER)image->Data;
		PBITMAPINFOHEADER  BInfoHeader = (PBITMAPINFOHEADER)&image->Data[sizeof(BITMAPFILEHEADER)];

		BFileHeader->bfType = 0x4D42; // BM
		BFileHeader->bfSize = sizeof(BITMAPFILEHEADER);
		BFileHeader->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

		BInfoHeader->biSize = sizeof(BITMAPINFOHEADER);
		BInfoHeader->biPlanes = 1;
		BInfoHeader->biBitCount = 24;
		BInfoHeader->biCompression = BI_RGB;
		BInfoHeader->biHeight = image->Height;
		BInfoHeader->biWidth = image->Width;

		RGBTRIPLE *Image = (RGBTRIPLE*)&image->Data[sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)];

		HDC CaptureDC = CreateCompatibleDC(DevC);
		HBITMAP CaptureBitmap = CreateCompatibleBitmap(DevC, image->Width, image->Height);
		HGDIOBJ oldBitmap = SelectObject(CaptureDC, CaptureBitmap);
		BitBlt(CaptureDC, 0, 0, image->Width, image->Height, DevC, info.rcMonitor.left + image->StartX, info.rcMonitor.top + image->StartY, SRCCOPY | CAPTUREBLT);
		GetDIBits(CaptureDC, CaptureBitmap, 0, image->Height, Image, (LPBITMAPINFO)BInfoHeader, DIB_RGB_COLORS);

		SelectObject(CaptureDC, oldBitmap);
		DeleteObject(CaptureBitmap);
		DeleteDC(CaptureDC);

		return FALSE;
	}

	return TRUE;
}

int asserthasimagedata(lua_State *L, LuaImage * img) {

	if (!img || !img->Data) {
		luaL_error(L, "Image does not contain any image data");
		return false;
	}

	return true;
}

int lua_setpixels(lua_State *L) {

	LuaImage * img = lua_toimage(L, 1);

	if (!asserthasimagedata(L, img)) {
		return 0;
	}

	size_t arraylen = lua_rawlen(L, 2);
	size_t imglen = img->Height * img->Width;

	if (arraylen != imglen) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushstring(L, "Size of image does not match pixel array size");
		return 2;
	}

	PBITMAPFILEHEADER BFileHeader = (PBITMAPFILEHEADER)img->Data;
	PBITMAPINFOHEADER  BInfoHeader = (PBITMAPINFOHEADER)&img->Data[sizeof(BITMAPFILEHEADER)];

	RGBTRIPLE * Image = (RGBTRIPLE*)&img->Data[BFileHeader->bfOffBits];
	RGBTRIPLE * rgb;
	size_t n = 0;

	int pitch = ((img->Width * BInfoHeader->biBitCount) + 31) / 32 * 4;
	int padding = pitch - (img->Width * (BInfoHeader->biBitCount / 8));
	int inpad;

	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {

		n = (size_t)lua_tointeger(L, -2) - 1;

		if (n < 0 || n >= imglen) {
			lua_pop(L, lua_gettop(L));
			lua_pushboolean(L, false);
			lua_pushstring(L, "Invalid array key");
			return 2;
		}

		inpad = (n / img->Width)*padding;
		rgb = &Image[n];

		rgb = (RGBTRIPLE*)((BYTE*)rgb + inpad);

		lua_pushstring(L, "r");
		lua_gettable(L, -2);

		if (lua_isnumber(L, -1)) {
			rgb->rgbtRed = (BYTE)max(min(lua_tonumber(L, -1), 255), 0);
		}
		else {
			lua_pop(L, lua_gettop(L));
			lua_pushboolean(L, false);
			lua_pushstring(L, "Missing pixel value");
			return 2;
		}

		lua_pop(L, 1);
		lua_pushstring(L, "g");
		lua_gettable(L, -2);

		if (lua_isnumber(L, -1)) {
			rgb->rgbtGreen = (BYTE)max(min(lua_tonumber(L, -1), 255), 0);
		}
		else {
			lua_pop(L, lua_gettop(L));
			lua_pushboolean(L, false);
			lua_pushstring(L, "Missing pixel value");
			return 2;
		}

		lua_pop(L, 1);
		lua_pushstring(L, "b");
		lua_gettable(L, -2);

		if (lua_isnumber(L, -1)) {
			rgb->rgbtBlue = (BYTE)max(min(lua_tonumber(L, -1), 255), 0);
		}
		else {
			lua_pop(L, lua_gettop(L));
			lua_pushboolean(L, false);
			lua_pushstring(L, "Missing pixel value");
			return 2;
		}

		lua_pop(L, 2);
	}

	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, true);

	return 1;
}

int lua_getpixels(lua_State *L) {

	LuaImage * img = lua_toimage(L, 1);

	if (!asserthasimagedata(L, img)) {
		return 0;
	}

	PBITMAPFILEHEADER BFileHeader = (PBITMAPFILEHEADER)img->Data;
	PBITMAPINFOHEADER  BInfoHeader = (PBITMAPINFOHEADER)&img->Data[sizeof(BITMAPFILEHEADER)];

	RGBTRIPLE * Image = (RGBTRIPLE*)&img->Data[BFileHeader->bfOffBits];
	RGBTRIPLE * rgb;
	size_t len = img->Height * img->Width;

	int pitch = ((img->Width * BInfoHeader->biBitCount) + 31) / 32 * 4;
	int padding = pitch - (img->Width * (BInfoHeader->biBitCount / 8));
	int inpad;

	lua_createtable(L, len, 0);

	for (size_t n = 0; n < len; n++) {

		inpad = (n / img->Width)*padding;

		rgb = &Image[n];//(RGBTRIPLE*)(Image + ((sizeof(RGBTRIPLE) * n) + inpad));

		rgb = (RGBTRIPLE*)((BYTE*)rgb + inpad);

		lua_createtable(L, 0, 3);

		lua_pushstring(L, "r");
		lua_pushinteger(L, rgb->rgbtRed);
		lua_settable(L, -3);

		lua_pushstring(L, "g");
		lua_pushinteger(L, rgb->rgbtGreen);
		lua_settable(L, -3);

		lua_pushstring(L, "b");
		lua_pushinteger(L, rgb->rgbtBlue);
		lua_settable(L, -3);

		lua_rawseti(L, -2, n + 1);
	}

	lua_copy(L, -1, 1);
	lua_pop(L, lua_gettop(L) - 1);

	return 1;
}

int lua_screenshotwindow(lua_State* L) {

	LuaWindow* window = lua_tonwindow(L, 1);

	HDC hdc = GetDC(window->handle);
	LuaImage* image = lua_pushimage(L);

	if (!hdc) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}

	BITMAP structBitmapHeader;
	memset(&structBitmapHeader, 0, sizeof(BITMAP));

	HGDIOBJ hBitmap = GetCurrentObject(hdc, OBJ_BITMAP);
	GetObject(hBitmap, sizeof(BITMAP), &structBitmapHeader);

	image->Height = structBitmapHeader.bmHeight;
	image->Width = structBitmapHeader.bmWidth;

	DWORD FileSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + (sizeof(RGBTRIPLE) + 1 * (image->Width * image->Height * 4));
	
	image->Data = (BYTE*)gff_calloc(FileSize, sizeof(1));

	if (!image->Data) {
		ReleaseDC(window->handle, hdc);
		luaL_error(L, "Out of memory");
		return 0;
	}

	PBITMAPFILEHEADER BFileHeader = (PBITMAPFILEHEADER)image->Data;
	PBITMAPINFOHEADER  BInfoHeader = (PBITMAPINFOHEADER)&image->Data[sizeof(BITMAPFILEHEADER)];

	BFileHeader->bfType = 0x4D42; // BM
	BFileHeader->bfSize = sizeof(BITMAPFILEHEADER);
	BFileHeader->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

	BInfoHeader->biSize = sizeof(BITMAPINFOHEADER);
	BInfoHeader->biPlanes = 1;
	BInfoHeader->biBitCount = 24;
	BInfoHeader->biCompression = BI_RGB;
	BInfoHeader->biHeight = image->Height;
	BInfoHeader->biWidth = image->Width;

	RGBTRIPLE* Image = (RGBTRIPLE*)&image->Data[sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)];

	HDC CaptureDC = CreateCompatibleDC(hdc);
	HBITMAP CaptureBitmap = CreateCompatibleBitmap(hdc, image->Width, image->Height);
	HGDIOBJ oldBitmap = SelectObject(CaptureDC, CaptureBitmap);
	BitBlt(CaptureDC, 0, 0, image->Width, image->Height, hdc, 0, 0, SRCCOPY);
	GetDIBits(CaptureDC, CaptureBitmap, 0, image->Height, Image, (LPBITMAPINFO)BInfoHeader, DIB_RGB_COLORS);

	SelectObject(CaptureDC, oldBitmap);
	DeleteObject(CaptureBitmap);
	DeleteDC(CaptureDC);
	ReleaseDC(window->handle, hdc);
	
	return 1;
}

int lua_screenshot(lua_State *L) {

	int x, y, startx, starty;
	x = (int)luaL_optinteger(L, 1, 0);
	y = (int)luaL_optinteger(L, 2, 0);
	startx = (int)max(luaL_optinteger(L, 3, 1) - 1, 0);
	starty = (int)max(luaL_optinteger(L, 4, 1) - 1, 0);
	int screen = (int)luaL_optinteger(L, 5, 1);

	lua_pop(L, lua_gettop(L));

	LuaImage * img = lua_pushimage(L);
	HDC DevC = GetDC(NULL);
	img->screen = screen;
	img->Width = x;
	img->Height = y;
	img->StartX = startx;
	img->StartY = starty;
	iterator = 0;
	BOOL b = EnumDisplayMonitors(DevC, NULL, MonitorEnumProcCallback, (LPARAM)img);

	ReleaseDC(NULL, DevC);

	if (!img->Data) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}

	return 1;
}

int lua_getsize(lua_State *L) {

	LuaImage * img = lua_toimage(L, 1);

	if (!asserthasimagedata(L, img)) {
		return 0;
	}

	lua_pop(L, lua_gettop(L));

	lua_pushinteger(L, img->Width);
	lua_pushinteger(L, img->Height);

	return 2;
}

int lua_crop(lua_State *L) {

	LuaImage * original = lua_toimage(L, 1);

	if (!asserthasimagedata(L, original)) {
		return 0;
	}

	int w = (int)luaL_checkinteger(L, 2);
	int h = (int)luaL_checkinteger(L, 3);
	int x = (int)luaL_checkinteger(L, 4) - 1;
	int y = (int)luaL_checkinteger(L, 5) - 1;

	if (w + x > (int)original->Width || h + y > (int)original->Height || x < 0 || y < 0 || w <= 0 || h <= 0) {

		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushstring(L, "Crop out of bounds");
		return 2;
	}

	LuaImage * image = lua_pushimage(L);

	DWORD FileSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + (sizeof(RGBTRIPLE) + 1 * (w*h * 4));
	BYTE * buffer = (BYTE*)gff_calloc(FileSize, sizeof(1));

	if (!buffer) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushstring(L, "Unable to allocate memory");
		return 2;
	}

	image->Data = buffer;
	image->DataSize = FileSize;
	image->Height = h;
	image->Width = w;

	PBITMAPFILEHEADER BFileHeader = (PBITMAPFILEHEADER)image->Data;
	PBITMAPINFOHEADER  BInfoHeader = (PBITMAPINFOHEADER)&image->Data[sizeof(BITMAPFILEHEADER)];

	BFileHeader->bfType = 0x4D42; // BM
	BFileHeader->bfSize = sizeof(BITMAPFILEHEADER);
	BFileHeader->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

	BInfoHeader->biSize = sizeof(BITMAPINFOHEADER);
	BInfoHeader->biPlanes = 1;
	BInfoHeader->biBitCount = 24;
	BInfoHeader->biCompression = BI_RGB;
	BInfoHeader->biHeight = image->Height;
	BInfoHeader->biWidth = image->Width;

	PBITMAPFILEHEADER oriBFileHeader = (PBITMAPFILEHEADER)original->Data;
	PBITMAPINFOHEADER  oriBInfoHeader = (PBITMAPINFOHEADER)&original->Data[sizeof(BITMAPFILEHEADER)];

	BYTE *Original = (BYTE*)&original->Data[oriBFileHeader->bfOffBits];
	BYTE *Image = (BYTE*)&image->Data[BFileHeader->bfOffBits];

	int coord_x, coord_y;
	size_t i = 0;
	size_t n;
	size_t hoffset = (original->Height - h) - y;
	size_t woffset = x;

	RGBTRIPLE * dst;
	RGBTRIPLE * src;

	int pitch = ((original->Width * oriBInfoHeader->biBitCount) + 31) / 32 * 4;
	int padding_out = pitch - (original->Width * (oriBInfoHeader->biBitCount / 8));

	pitch = ((image->Width * BInfoHeader->biBitCount) + 31) / 32 * 4;
	int padding_in = pitch - (image->Width * (BInfoHeader->biBitCount / 8));

	int inpad = 0;
	int outpad = 0;

	for (coord_y = 0; coord_y < h; coord_y++) {

		inpad = (coord_y*padding_in);
		outpad = ((coord_y + hoffset)*padding_out);

		for (coord_x = 0; coord_x < w; coord_x++) {

			n = (coord_x + woffset) + (original->Width * (coord_y + hoffset));
			i = coord_x + (image->Width * coord_y);


			dst = (RGBTRIPLE*)(Image + ((sizeof(RGBTRIPLE) * i) + inpad));

			src = (RGBTRIPLE*)(Original + ((sizeof(RGBTRIPLE) * n) + outpad));

			memcpy(dst, src, sizeof(RGBTRIPLE));
		}
	}

	lua_copy(L, lua_gettop(L), 1);
	lua_pop(L, lua_gettop(L) - 1);

	return 1;
}

int lua_createimage(lua_State *L) {

	int w = (int)luaL_checkinteger(L, 1);
	int h = (int)luaL_checkinteger(L, 2);

	lua_pop(L, lua_gettop(L));
	LuaImage * image = lua_pushimage(L);

	DWORD FileSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + (sizeof(RGBTRIPLE) + 1 * (w*h * 4));
	BYTE * buffer = (BYTE*)gff_calloc(FileSize, sizeof(1));

	if (!buffer) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushstring(L, "Unable to allocate memory");
		return 2;
	}

	image->Data = buffer;
	image->DataSize = FileSize;
	image->Height = h;
	image->Width = w;

	PBITMAPFILEHEADER BFileHeader = (PBITMAPFILEHEADER)image->Data;
	PBITMAPINFOHEADER  BInfoHeader = (PBITMAPINFOHEADER)&image->Data[sizeof(BITMAPFILEHEADER)];

	BFileHeader->bfType = 0x4D42; // BM
	BFileHeader->bfSize = sizeof(BITMAPFILEHEADER);
	BFileHeader->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

	BInfoHeader->biSize = sizeof(BITMAPINFOHEADER);
	BInfoHeader->biPlanes = 1;
	BInfoHeader->biBitCount = 24;
	BInfoHeader->biCompression = BI_RGB;
	BInfoHeader->biHeight = image->Height;
	BInfoHeader->biWidth = image->Width;

	return 1;
}

int lua_getpixel(lua_State *L) {

	LuaImage * img = lua_toimage(L, 1);

	if (!asserthasimagedata(L, img)) {
		return 0;
	}

	int x = (int)luaL_checkinteger(L, 2) - 1;
	int y = (int)luaL_checkinteger(L, 3) - 1;

	if (y < 0 || x < 0 || y >= (int)img->Height || x >= (int)img->Width) {
		luaL_error(L, "Argument out of range");
		return 0;
	}

	PBITMAPFILEHEADER BFileHeader = (PBITMAPFILEHEADER)img->Data;
	PBITMAPINFOHEADER  BInfoHeader = (PBITMAPINFOHEADER)&img->Data[sizeof(BITMAPFILEHEADER)];

	RGBTRIPLE * Image = (RGBTRIPLE*)&img->Data[BFileHeader->bfOffBits];
	RGBTRIPLE * rgb;

	int i = x + img->Width * (img->Height - y - 1);
	int pitch = ((img->Width * BInfoHeader->biBitCount) + 31) / 32 * 4;
	int padding = pitch - (img->Width * (BInfoHeader->biBitCount / 8));
	int inpad = (img->Height - y - 1)*padding;

	rgb = &Image[i];

	rgb = (RGBTRIPLE*)((BYTE*)rgb + inpad);

	lua_createtable(L, 0, 3);

	lua_pushstring(L, "r");
	lua_pushinteger(L, rgb->rgbtRed);
	lua_settable(L, -3);

	lua_pushstring(L, "g");
	lua_pushinteger(L, rgb->rgbtGreen);
	lua_settable(L, -3);

	lua_pushstring(L, "b");
	lua_pushinteger(L, rgb->rgbtBlue);
	lua_settable(L, -3);

	lua_copy(L, -1, 1);
	lua_pop(L, lua_gettop(L) - 1);

	return 1;
}

int lua_setpixel(lua_State *L) {

	LuaImage * img = lua_toimage(L, 1);

	if (!asserthasimagedata(L, img)) {
		return 0;
	}

	int y = (int)luaL_checkinteger(L, 2) - 1;
	int x = (int)luaL_checkinteger(L, 3) - 1;

	if (y < 0 || x < 0 || y >= (int)img->Height || x >= (int)img->Width) {
		luaL_error(L, "Argument out of range");
		return 0;
	}
	else if (!lua_istable(L, 4)) {
		luaL_error(L, "pixel is not a table");
		return 0;
	}

	PBITMAPFILEHEADER BFileHeader = (PBITMAPFILEHEADER)img->Data;
	PBITMAPINFOHEADER  BInfoHeader = (PBITMAPINFOHEADER)&img->Data[sizeof(BITMAPFILEHEADER)];

	RGBTRIPLE * Image = (RGBTRIPLE*)&img->Data[BFileHeader->bfOffBits];
	RGBTRIPLE * rgb;

	int i = x + img->Width * (img->Height - y - 1);
	int pitch = ((img->Width * BInfoHeader->biBitCount) + 31) / 32 * 4;
	int padding = pitch - (img->Width * (BInfoHeader->biBitCount / 8));
	int inpad = (img->Height - y - 1)*padding;

	rgb = &Image[i];

	rgb = (RGBTRIPLE*)((BYTE*)rgb + inpad);

	lua_pushstring(L, "r");
	lua_gettable(L, -2);

	if (lua_isnumber(L, -1)) {
		rgb->rgbtRed = (BYTE)max(min(lua_tonumber(L, -1), 255), 0);
	}
	else {
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		lua_pushstring(L, "Missing pixel value");
		return 2;
	}

	lua_pop(L, 1);
	lua_pushstring(L, "g");
	lua_gettable(L, -2);

	if (lua_isnumber(L, -1)) {
		rgb->rgbtGreen = (BYTE)max(min(lua_tonumber(L, -1), 255), 0);
	}
	else {
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		lua_pushstring(L, "Missing pixel value");
		return 2;
	}

	lua_pop(L, 1);
	lua_pushstring(L, "b");
	lua_gettable(L, -2);

	if (lua_isnumber(L, -1)) {
		rgb->rgbtBlue = (BYTE)max(min(lua_tonumber(L, -1), 255), 0);
	}
	else {
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		lua_pushstring(L, "Missing pixel value");
		return 2;
	}

	lua_pop(L, lua_gettop(L));

	return 0;
}

int lua_setpixelmatrix(lua_State *L) {

	LuaImage * img = lua_toimage(L, 1);

	if (!asserthasimagedata(L, img)) {
		return 0;
	}

	PBITMAPFILEHEADER BFileHeader = (PBITMAPFILEHEADER)img->Data;
	PBITMAPINFOHEADER  BInfoHeader = (PBITMAPINFOHEADER)&img->Data[sizeof(BITMAPFILEHEADER)];

	RGBTRIPLE * Image = (RGBTRIPLE*)&img->Data[BFileHeader->bfOffBits];
	RGBTRIPLE * rgb;
	int x, y, i;

	int pitch = ((img->Width * BInfoHeader->biBitCount) + 31) / 32 * 4;
	int padding = pitch - (img->Width * (BInfoHeader->biBitCount / 8));
	int inpad;

	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {

		y = (int)lua_tointeger(L, -2) - 1;

		if (y < 0 || y >= (int)img->Height) {
			lua_pop(L, lua_gettop(L));
			lua_pushboolean(L, false);
			lua_pushstring(L, "Invalid array key");
			return 2;
		}

		lua_pushnil(L);
		while (lua_next(L, -2) != 0) {

			x = (int)lua_tointeger(L, -2) - 1;

			if (x < 0 || x >= (int)img->Width) {
				lua_pop(L, lua_gettop(L));
				lua_pushboolean(L, false);
				lua_pushstring(L, "Invalid array key");
				return 2;
			}

			i = x + img->Width * (img->Height - y - 1);

			inpad = (img->Height - y - 1)*padding;
			rgb = &Image[i];

			rgb = (RGBTRIPLE*)((BYTE*)rgb + inpad);

			lua_pushstring(L, "r");
			lua_gettable(L, -2);

			if (lua_isnumber(L, -1)) {
				rgb->rgbtRed = (BYTE)max(min(lua_tonumber(L, -1), 255), 0);
			}
			else {
				lua_pop(L, lua_gettop(L));
				lua_pushboolean(L, false);
				lua_pushstring(L, "Missing pixel value");
				return 2;
			}

			lua_pop(L, 1);
			lua_pushstring(L, "g");
			lua_gettable(L, -2);

			if (lua_isnumber(L, -1)) {
				rgb->rgbtGreen = (BYTE)max(min(lua_tonumber(L, -1), 255), 0);
			}
			else {
				lua_pop(L, lua_gettop(L));
				lua_pushboolean(L, false);
				lua_pushstring(L, "Missing pixel value");
				return 2;
			}

			lua_pop(L, 1);
			lua_pushstring(L, "b");
			lua_gettable(L, -2);

			if (lua_isnumber(L, -1)) {
				rgb->rgbtBlue = (BYTE)max(min(lua_tonumber(L, -1), 255), 0);
			}
			else {
				lua_pop(L, lua_gettop(L));
				lua_pushboolean(L, false);
				lua_pushstring(L, "Missing pixel value");
				return 2;
			}

			lua_pop(L, 2);
		}

		lua_pop(L, 1);
	}

	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, true);

	return 1;
}

int lua_getpixelmatrix(lua_State *L) {

	LuaImage * img = lua_toimage(L, 1);

	if (!asserthasimagedata(L, img)) {
		return 0;
	}

	size_t i = 0;
	PBITMAPFILEHEADER BFileHeader = (PBITMAPFILEHEADER)img->Data;
	PBITMAPINFOHEADER  BInfoHeader = (PBITMAPINFOHEADER)&img->Data[sizeof(BITMAPFILEHEADER)];

	RGBTRIPLE * Image = (RGBTRIPLE*)&img->Data[BFileHeader->bfOffBits];
	RGBTRIPLE * rgb;

	int pitch = ((img->Width * BInfoHeader->biBitCount) + 31) / 32 * 4;
	int padding = pitch - (img->Width * (BInfoHeader->biBitCount / 8));
	int inpad;

	lua_createtable(L, img->Height, 0);

	for (DWORD coord_y = img->Height - 1; coord_y >= 0; coord_y--) {

		lua_createtable(L, img->Width, 0);

		for (DWORD coord_x = 0; coord_x < img->Width; coord_x++) {

			i = coord_x + img->Width * (img->Height - coord_y - 1);

			rgb = &Image[i];

			inpad = (img->Height - coord_y - 1)*padding;

			rgb = (RGBTRIPLE*)((BYTE*)rgb + inpad);

			lua_createtable(L, 0, 3);

			lua_pushstring(L, "r");
			lua_pushinteger(L, rgb->rgbtRed);
			lua_settable(L, -3);

			lua_pushstring(L, "g");
			lua_pushinteger(L, rgb->rgbtGreen);
			lua_settable(L, -3);

			lua_pushstring(L, "b");
			lua_pushinteger(L, rgb->rgbtBlue);
			lua_settable(L, -3);

			lua_rawseti(L, -2, coord_x + 1);
		}

		lua_rawseti(L, -2, coord_y + 1);
	}

	lua_copy(L, lua_gettop(L), 1);
	lua_pop(L, lua_gettop(L) - 1);

	return 1;
}

int lua_loadfromfile(lua_State *L) {

	const char * file = luaL_checkstring(L, 1);
	HANDLE FH = CreateFileA(file, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);

	if (FH == INVALID_HANDLE_VALUE) {

		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		lua_pushstring(L, "Unable to open file");
		return 2;
	}

	DWORD size = GetFileSize(FH, NULL);

	if (size == INVALID_FILE_SIZE) {
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		lua_pushstring(L, "Unable to retrive filesize");
		CloseHandle(FH);
		return 2;
	}

	BYTE * buffer = (BYTE*)gff_calloc(size, sizeof(BYTE));

	if (!buffer) {
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		lua_pushstring(L, "Unable to allocate memory");
		CloseHandle(FH);
		return 2;
	}

	DWORD read;
	if (!ReadFile(FH, buffer, size, &read, NULL) || read != size) {
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		lua_pushstring(L, "Unable to read file");
		CloseHandle(FH);
		gff_free(buffer);
		return 2;
	}

	CloseHandle(FH);

	PBITMAPFILEHEADER BFileHeader = (PBITMAPFILEHEADER)buffer;
	PBITMAPINFOHEADER  BInfoHeader = (PBITMAPINFOHEADER)&buffer[sizeof(BITMAPFILEHEADER)];

	if (BFileHeader->bfType != 0x4D42) {
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		lua_pushstring(L, "Invalid filetype (bmp only)");
		gff_free(buffer);
		return 2;
	}
	else if (BFileHeader->bfSize < sizeof(BITMAPFILEHEADER) ||
		BFileHeader->bfOffBits < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) {
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		lua_pushstring(L, "Invalid sizes, can only load simple bmp files");
		gff_free(buffer);
		return 2;
	}
	else if (BInfoHeader->biSize != sizeof(BITMAPINFOHEADER) ||
		BInfoHeader->biPlanes != 1 ||
		BInfoHeader->biBitCount != 24 ||
		BInfoHeader->biCompression != BI_RGB) {
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		lua_pushstring(L, "Invalid bits or multiplane, can only load simple bmp files");
		gff_free(buffer);
		return 2;
	}

	lua_pop(L, lua_gettop(L));
	LuaImage * img = lua_pushimage(L);
	img->Data = buffer;
	img->DataSize = size;
	img->Height = BInfoHeader->biHeight;
	img->Width = BInfoHeader->biWidth;

	return 1;
}

int lua_savetofile(lua_State *L) {

	LuaImage * img = lua_toimage(L, 1);

	if (!asserthasimagedata(L, img)) {
		return 0;
	}

	const char * file = luaL_checkstring(L, 2);

	DWORD Junk;
	HANDLE FH = CreateFileA(file, GENERIC_WRITE, FILE_SHARE_WRITE, 0, CREATE_ALWAYS, 0, 0);
	if (FH == INVALID_HANDLE_VALUE) {
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		return 1;
	}

	if (WriteFile(FH, img->Data, img->DataSize, &Junk, 0) && Junk == img->DataSize) {

		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, true);
	}
	else {
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, true);
	}

	CloseHandle(FH);

	return 1;
}

LuaImage * lua_pushimage(lua_State *L) {
	LuaImage * image = (LuaImage*)lua_newuserdata(L, sizeof(LuaImage));
	if (image == NULL)
		luaL_error(L, "Unable to push image");
	luaL_getmetatable(L, IMAGE);
	lua_setmetatable(L, -2);
	memset(image, 0, sizeof(LuaImage));

	return image;
}

LuaImage * lua_toimage(lua_State *L, int index) {
	LuaImage * image = (LuaImage*)luaL_checkudata(L, index, IMAGE);
	if (image == NULL)
		luaL_error(L, "parameter is not a %s", IMAGE);
	return image;
}

int image_gc(lua_State *L) {

	LuaImage * img = lua_toimage(L, 1);

	if (img->Data) {
		gff_free(img->Data);
		img->Data = NULL;
	}

	return 0;
}

int image_tostring(lua_State *L) {
	char tim[100];
	sprintf(tim, "Image: 0x%08X", lua_toimage(L, 1));
	lua_pushfstring(L, tim);
	return 1;
}