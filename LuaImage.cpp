#include "LuaImage.h"
#include "kitsunefile.h"
#include "luatext.h"
#include "miniz.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

// Route stb's internal allocations through the engine's tracked allocator so
// Image buffers show up in the same GetMemory()/CollectGarbage() accounting
// as every other module (see base64.cpp for the same convention).
#define STBI_MALLOC(sz)          kitsune_malloc(sz)
#define STBI_REALLOC(p, newsz)   kitsune_realloc(p, newsz)
#define STBI_FREE(p)             kitsune_free(p)
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STBIW_MALLOC(sz)         kitsune_malloc(sz)
#define STBIW_REALLOC(p, newsz)  kitsune_realloc(p, newsz)
#define STBIW_FREE(p)            kitsune_free(p)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

// ===========================================================================
// Userdata plumbing (mirrors LuaProcess.cpp)
// ===========================================================================

LuaImage* lua_toimage(lua_State* L, int index) {
	// luaL_checkudata already raises a Lua error (and never returns) on mismatch.
	return (LuaImage*)luaL_checkudata(L, index, LUAIMAGE);
}

LuaImage* lua_pushimage(lua_State* L) {
	LuaImage* img = (LuaImage*)lua_newuserdata(L, sizeof(LuaImage));
	if (!img)
		luaL_error(L, "Unable to create image userdata");
	luaL_getmetatable(L, LUAIMAGE);
	lua_setmetatable(L, -2);
	memset(img, 0, sizeof(LuaImage));
	return img;
}

int image_tostring(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	lua_pushfstring(L, "Image: %dx%d", img->width, img->height);
	return 1;
}

int image_gc(lua_State* L) {
	LuaImage* img = (LuaImage*)lua_touserdata(L, 1);
	if (!img)
		return 0;
	if (img->pixels) {
		kitsune_free(img->pixels);
		img->pixels = NULL;
	}
	for (int i = 0; i < img->tagCount; i++) {
		kitsune_free(img->tags[i].key);
		kitsune_free(img->tags[i].value);
	}
	if (img->tags) {
		kitsune_free(img->tags);
		img->tags = NULL;
	}
	img->tagCount = 0;
	img->tagCapacity = 0;
	return 0;
}

// ===========================================================================
// Small internal helpers
// ===========================================================================

static char* DupString(const char* s, size_t len) {
	char* out = (char*)kitsune_malloc(len + 1);
	memcpy(out, s, len);
	out[len] = '\0';
	return out;
}

// Replace an existing tag's value, or append a new one (grows tags[] by
// doubling). Last write for a given key always wins.
static void SetTag(LuaImage* img, const char* key, size_t keylen, const char* value, size_t vallen) {
	for (int i = 0; i < img->tagCount; i++) {
		if (strlen(img->tags[i].key) == keylen && memcmp(img->tags[i].key, key, keylen) == 0) {
			kitsune_free(img->tags[i].value);
			img->tags[i].value = DupString(value, vallen);
			return;
		}
	}
	if (img->tagCount == img->tagCapacity) {
		int newCap = img->tagCapacity == 0 ? 4 : img->tagCapacity * 2;
		img->tags = (ImageTag*)kitsune_realloc(img->tags, sizeof(ImageTag) * newCap);
		img->tagCapacity = newCap;
	}
	img->tags[img->tagCount].key = DupString(key, keylen);
	img->tags[img->tagCount].value = DupString(value, vallen);
	img->tagCount++;
}

// PNG tEXt keywords and text are Latin-1 by spec: returns a heap UTF-8 copy (at most two
// bytes per input byte) and its length. *out is NULL when out of memory; free with kitsune_free.
static size_t Latin1ToUtf8(const unsigned char* src, size_t len, char** out) {
	char* buf = (char*)kitsune_malloc(len * 2 + 1);
	*out = buf;
	if (!buf)
		return 0;
	size_t n = 0;
	for (size_t i = 0; i < len; i++)
		n += kitsune_utf8_encode(buf + n, src[i]);
	buf[n] = '\0';
	return n;
}

static void CopyTags(LuaImage* dst, const LuaImage* src) {
	for (int i = 0; i < src->tagCount; i++) {
		SetTag(dst, src->tags[i].key, strlen(src->tags[i].key), src->tags[i].value, strlen(src->tags[i].value));
	}
}

static inline unsigned char ClampByte(int v) {
	return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// Bounds-checked read; out-of-bounds reads as fully transparent.
static inline void GetPx(const LuaImage* img, int x, int y, unsigned char out[4]) {
	if (x < 0 || y < 0 || x >= img->width || y >= img->height) {
		out[0] = out[1] = out[2] = out[3] = 0;
		return;
	}
	const unsigned char* p = img->pixels + (size_t)(y * img->width + x) * 4;
	out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3];
}

// Bounds-checked read that clamps to the edge instead of returning
// transparent -- used by Resize so edges don't fade to black.
static inline void GetPxClamped(const LuaImage* img, int x, int y, unsigned char out[4]) {
	if (x < 0) x = 0; if (x >= img->width) x = img->width - 1;
	if (y < 0) y = 0; if (y >= img->height) y = img->height - 1;
	const unsigned char* p = img->pixels + (size_t)(y * img->width + x) * 4;
	out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3];
}

// Bounds-checked overwrite (no blending).
static inline void SetPxRaw(LuaImage* img, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
	if (x < 0 || y < 0 || x >= img->width || y >= img->height)
		return;
	unsigned char* p = img->pixels + (size_t)(y * img->width + x) * 4;
	p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

// Straight-alpha "source over destination" compositing -- used by every
// drawing primitive so partial-alpha colors blend the way a paint tool would.
static inline void BlendPx(LuaImage* img, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
	if (x < 0 || y < 0 || x >= img->width || y >= img->height || a == 0)
		return;
	if (a == 255) {
		SetPxRaw(img, x, y, r, g, b, a);
		return;
	}
	unsigned char dst[4];
	GetPx(img, x, y, dst);
	double sa = a / 255.0;
	double da = dst[3] / 255.0;
	double outA = sa + da * (1.0 - sa);
	if (outA <= 0.0001) {
		SetPxRaw(img, x, y, 0, 0, 0, 0);
		return;
	}
	double outR = (r * sa + dst[0] * da * (1.0 - sa)) / outA;
	double outG = (g * sa + dst[1] * da * (1.0 - sa)) / outA;
	double outB = (b * sa + dst[2] * da * (1.0 - sa)) / outA;
	SetPxRaw(img, x, y, ClampByte((int)(outR + 0.5)), ClampByte((int)(outG + 0.5)), ClampByte((int)(outB + 0.5)), ClampByte((int)(outA * 255.0 + 0.5)));
}

static LuaImage* PushBlankImage(lua_State* L, int width, int height) {
	LuaImage* img = lua_pushimage(L);
	img->width = width;
	img->height = height;
	img->pixels = (unsigned char*)kitsune_calloc((size_t)width * height * 4, 1);
	return img;
}

// ===========================================================================
// PNG tEXt/zTXt/iTXt chunk handling (hand-rolled -- see plan notes: PNG
// chunks are [4B length][4B type][data][4B CRC32], and CRC32 is already
// available via miniz's mz_crc32, so no second PNG library is needed just
// for metadata).
// ===========================================================================

static uint32_t ReadBE32(const unsigned char* p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void WriteBE32(unsigned char* p, uint32_t v) {
	p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
	p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)(v);
}

static mz_ulong Crc32(const unsigned char* data, size_t len) {
	mz_ulong seed = mz_crc32(0, NULL, 0);
	return mz_crc32(seed, data, len);
}

// Walks every chunk in a raw PNG byte buffer and merges tEXt/iTXt keyword/text
// pairs into img's tags (last chunk for a given key wins). zTXt and
// compressed iTXt chunks are skipped -- their payload is deflate-compressed
// and not needed for the tag workflow this module targets.
static void ScanTextChunks(const unsigned char* data, size_t len, LuaImage* img) {
	if (len < 8 || memcmp(data, "\x89PNG\r\n\x1a\n", 8) != 0)
		return; // not a PNG signature; nothing to scan

	size_t offset = 8;
	while (offset + 12 <= len) {
		uint32_t chunkLen = ReadBE32(data + offset);
		const unsigned char* type = data + offset + 4;
		const unsigned char* chunkData = data + offset + 8;
		if (offset + 12 + (size_t)chunkLen > len)
			break; // truncated/corrupt -- stop scanning defensively

		if (memcmp(type, "IEND", 4) == 0)
			break;

		if (memcmp(type, "tEXt", 4) == 0) {
			const unsigned char* nul = (const unsigned char*)memchr(chunkData, 0, chunkLen);
			if (nul) {
				size_t keyLen = nul - chunkData;
				size_t valLen = chunkLen - keyLen - 1;
				char* key8;
				char* val8;
				size_t key8Len = Latin1ToUtf8(chunkData, keyLen, &key8);
				size_t val8Len = Latin1ToUtf8(nul + 1, valLen, &val8);
				if (key8 && val8)
					SetTag(img, key8, key8Len, val8, val8Len);
				kitsune_free(key8);
				kitsune_free(val8);
			}
		}
		else if (memcmp(type, "iTXt", 4) == 0) {
			const unsigned char* p = chunkData;
			const unsigned char* end = chunkData + chunkLen;
			const unsigned char* nul1 = (const unsigned char*)memchr(p, 0, end - p);
			if (nul1 && nul1 + 2 <= end) {
				size_t keyLen = nul1 - p;
				unsigned char compressed = nul1[1];
				// nul1[2] is the compression method, only meaningful when compressed.
				const unsigned char* q = nul1 + 3;
				const unsigned char* nul2 = (const unsigned char*)memchr(q, 0, end - q); // language tag
				if (nul2) {
					const unsigned char* r = nul2 + 1;
					const unsigned char* nul3 = (const unsigned char*)memchr(r, 0, end - r); // translated keyword
					if (nul3 && !compressed) {
						const unsigned char* text = nul3 + 1;
						size_t textLen = end - text;
						SetTag(img, (const char*)p, keyLen, (const char*)text, textLen);
					}
				}
			}
		}
		// zTXt and compressed iTXt are intentionally skipped.

		offset += 12 + chunkLen;
	}
}

// Builds one uncompressed iTXt chunk (length+type+data+crc) for a tag.
static unsigned char* BuildITXtChunk(const char* key, const char* value, size_t* outLen) {
	size_t keyLen = strlen(key);
	size_t valLen = strlen(value);
	// keyword \0 compressionFlag compressionMethod languageTag\0 translatedKeyword\0 text
	size_t dataLen = keyLen + 1 + 1 + 1 + 1 + 1 + valLen;
	size_t total = 4 + 4 + dataLen + 4;
	unsigned char* chunk = (unsigned char*)kitsune_malloc(total);

	WriteBE32(chunk, (uint32_t)dataLen);
	memcpy(chunk + 4, "iTXt", 4);

	unsigned char* p = chunk + 8;
	memcpy(p, key, keyLen); p += keyLen;
	*p++ = 0;      // keyword terminator
	*p++ = 0;      // compression flag: uncompressed
	*p++ = 0;      // compression method
	*p++ = 0;      // language tag (empty) terminator
	*p++ = 0;      // translated keyword (empty) terminator
	memcpy(p, value, valLen);

	uint32_t crc = (uint32_t)Crc32(chunk + 4, 4 + dataLen);
	WriteBE32(chunk + 8 + dataLen, crc);

	*outLen = total;
	return chunk;
}

// Inserts img's tags as iTXt chunks into a PNG buffer produced by
// stb_image_write, right before the trailing IEND chunk.
static unsigned char* SpliceTagsIntoPng(const LuaImage* img, const unsigned char* png, size_t pngLen, size_t* outLen) {
	if (img->tagCount == 0) {
		unsigned char* out = (unsigned char*)kitsune_malloc(pngLen);
		memcpy(out, png, pngLen);
		*outLen = pngLen;
		return out;
	}

	// IEND is always exactly 12 bytes (0-length data) and is the last chunk;
	// verify that fast assumption, falling back to a full scan if it ever
	// doesn't hold (e.g. a future stb_image_write emits trailing bytes).
	size_t splitAt = pngLen >= 12 && memcmp(png + pngLen - 8, "IEND", 4) == 0 ? pngLen - 12 : (size_t)-1;
	if (splitAt == (size_t)-1) {
		size_t offset = 8;
		while (offset + 12 <= pngLen) {
			uint32_t chunkLen = ReadBE32(png + offset);
			if (memcmp(png + offset + 4, "IEND", 4) == 0) { splitAt = offset; break; }
			offset += 12 + chunkLen;
		}
		if (splitAt == (size_t)-1) splitAt = pngLen; // give up gracefully; append at end
	}

	unsigned char** chunks = (unsigned char**)kitsune_malloc(sizeof(unsigned char*) * img->tagCount);
	size_t* chunkLens = (size_t*)kitsune_malloc(sizeof(size_t) * img->tagCount);
	size_t extraLen = 0;
	for (int i = 0; i < img->tagCount; i++) {
		chunks[i] = BuildITXtChunk(img->tags[i].key, img->tags[i].value, &chunkLens[i]);
		extraLen += chunkLens[i];
	}

	size_t total = pngLen + extraLen;
	unsigned char* out = (unsigned char*)kitsune_malloc(total);
	memcpy(out, png, splitAt);
	size_t pos = splitAt;
	for (int i = 0; i < img->tagCount; i++) {
		memcpy(out + pos, chunks[i], chunkLens[i]);
		pos += chunkLens[i];
		kitsune_free(chunks[i]);
	}
	memcpy(out + pos, png + splitAt, pngLen - splitAt);

	kitsune_free(chunks);
	kitsune_free(chunkLens);
	*outLen = total;
	return out;
}

// Shared encode path for Save/ToBytes: encodes pixels to PNG then splices
// this image's tags in as iTXt chunks. Caller owns the returned buffer
// (kitsune_free it).
static unsigned char* EncodeWithTags(const LuaImage* img, size_t* outLen) {
	int rawLen = 0;
	unsigned char* raw = stbi_write_png_to_mem(img->pixels, img->width * 4, img->width, img->height, 4, &rawLen);
	if (!raw)
		return NULL;
	unsigned char* out = SpliceTagsIntoPng(img, raw, (size_t)rawLen, outLen);
	STBIW_FREE(raw);
	return out;
}

// ===========================================================================
// Module-level constructors
// ===========================================================================

int Image_Open(lua_State* L) {
	const char* path = luaL_checkstring(L, 1);

	FILE* f = kitsune_fopen(path, "rb");
	if (!f)
		return luaL_error(L, "Image.Open: cannot open '%s'", path);
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size <= 0) {
		fclose(f);
		return luaL_error(L, "Image.Open: '%s' is empty or unreadable", path);
	}
	unsigned char* buf = (unsigned char*)kitsune_malloc((size_t)size);
	size_t read = fread(buf, 1, (size_t)size, f);
	fclose(f);
	if (read != (size_t)size) {
		kitsune_free(buf);
		return luaL_error(L, "Image.Open: failed reading '%s'", path);
	}

	int w, h, channels;
	unsigned char* pixels = stbi_load_from_memory(buf, (int)size, &w, &h, &channels, 4);
	if (!pixels) {
		kitsune_free(buf);
		return luaL_error(L, "Image.Open: failed to decode '%s' (%s)", path, stbi_failure_reason());
	}

	LuaImage* img = lua_pushimage(L);
	img->width = w;
	img->height = h;
	img->pixels = pixels;
	ScanTextChunks(buf, (size_t)size, img);
	kitsune_free(buf);
	return 1;
}

int Image_New(lua_State* L) {
	int w = (int)luaL_checkinteger(L, 1);
	int h = (int)luaL_checkinteger(L, 2);
	if (w <= 0 || h <= 0)
		return luaL_error(L, "Image.New: width/height must be positive");
	PushBlankImage(L, w, h);
	return 1;
}

int Image_FromBytes(lua_State* L) {
	size_t len;
	const char* data = luaL_checklstring(L, 1, &len);

	int w, h, channels;
	unsigned char* pixels = stbi_load_from_memory((const unsigned char*)data, (int)len, &w, &h, &channels, 4);
	if (!pixels)
		return luaL_error(L, "Image.FromBytes: failed to decode (%s)", stbi_failure_reason());

	LuaImage* img = lua_pushimage(L);
	img->width = w;
	img->height = h;
	img->pixels = pixels;
	ScanTextChunks((const unsigned char*)data, len, img);
	return 1;
}

// ===========================================================================
// Metadata
// ===========================================================================

int Image_GetMetadata(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);

	lua_createtable(L, 0, 3);
	lua_pushstring(L, "width");
	lua_pushinteger(L, img->width);
	lua_settable(L, -3);
	lua_pushstring(L, "height");
	lua_pushinteger(L, img->height);
	lua_settable(L, -3);

	lua_pushstring(L, "tags");
	lua_createtable(L, 0, img->tagCount);
	for (int i = 0; i < img->tagCount; i++) {
		lua_pushstring(L, img->tags[i].key);
		lua_pushstring(L, img->tags[i].value);
		lua_settable(L, -3);
	}
	lua_settable(L, -3);

	return 1;
}

int Image_SetMetadata(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	size_t keyLen, valLen;
	const char* key = luaL_checklstring(L, 2, &keyLen);
	const char* value = luaL_checklstring(L, 3, &valLen);
	SetTag(img, key, keyLen, value, valLen);
	return 0;
}

int Image_GetWidth(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	lua_pushinteger(L, img->width);
	return 1;
}

int Image_GetHeight(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	lua_pushinteger(L, img->height);
	return 1;
}

// ===========================================================================
// Pixel access
// ===========================================================================

int Image_GetPixel(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	int x = (int)luaL_checkinteger(L, 2);
	int y = (int)luaL_checkinteger(L, 3);
	if (x < 0 || y < 0 || x >= img->width || y >= img->height)
		return luaL_error(L, "Image:GetPixel: (%d,%d) out of bounds for %dx%d image", x, y, img->width, img->height);
	unsigned char px[4];
	GetPx(img, x, y, px);
	lua_pushinteger(L, px[0]);
	lua_pushinteger(L, px[1]);
	lua_pushinteger(L, px[2]);
	lua_pushinteger(L, px[3]);
	return 4;
}

int Image_SetPixel(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	int x = (int)luaL_checkinteger(L, 2);
	int y = (int)luaL_checkinteger(L, 3);
	int r = (int)luaL_checkinteger(L, 4);
	int g = (int)luaL_checkinteger(L, 5);
	int b = (int)luaL_checkinteger(L, 6);
	int a = (int)luaL_optinteger(L, 7, 255);
	if (x < 0 || y < 0 || x >= img->width || y >= img->height)
		return luaL_error(L, "Image:SetPixel: (%d,%d) out of bounds for %dx%d image", x, y, img->width, img->height);
	SetPxRaw(img, x, y, ClampByte(r), ClampByte(g), ClampByte(b), ClampByte(a));
	return 0;
}

// ===========================================================================
// Non-mutating transforms (Crop/Resize/Clone)
// ===========================================================================

int Image_Crop(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	int x = (int)luaL_checkinteger(L, 2);
	int y = (int)luaL_checkinteger(L, 3);
	int w = (int)luaL_checkinteger(L, 4);
	int h = (int)luaL_checkinteger(L, 5);
	if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > img->width || y + h > img->height)
		return luaL_error(L, "Image:Crop: rect (%d,%d,%d,%d) out of bounds for %dx%d image", x, y, w, h, img->width, img->height);

	LuaImage* out = PushBlankImage(L, w, h);
	for (int row = 0; row < h; row++) {
		const unsigned char* src = img->pixels + (size_t)((y + row) * img->width + x) * 4;
		unsigned char* dst = out->pixels + (size_t)(row * w) * 4;
		memcpy(dst, src, (size_t)w * 4);
	}
	CopyTags(out, img);
	return 1;
}

int Image_Resize(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	int w = (int)luaL_checkinteger(L, 2);
	int h = (int)luaL_checkinteger(L, 3);
	const char* filter = luaL_optstring(L, 4, "bilinear");
	bool nearest = strcmp(filter, "nearest") == 0;
	if (!nearest && strcmp(filter, "bilinear") != 0)
		return luaL_error(L, "Image:Resize: filter must be \"bilinear\" or \"nearest\"");
	if (w <= 0 || h <= 0)
		return luaL_error(L, "Image:Resize: width/height must be positive");

	LuaImage* out = PushBlankImage(L, w, h);
	double sx = (double)img->width / w;
	double sy = (double)img->height / h;

	// Nearest-neighbor: no blending, so crisp pixel-art edges stay crisp
	// when zooming in (e.g. reading pixel coordinates off a sprite) instead
	// of smearing across a blended boundary like bilinear would.
	if (nearest) {
		for (int dy = 0; dy < h; dy++) {
			int sy0 = MIN((int)((dy + 0.5) * sy), img->height - 1);
			for (int dx = 0; dx < w; dx++) {
				int sx0 = MIN((int)((dx + 0.5) * sx), img->width - 1);
				unsigned char px[4];
				GetPx(img, sx0, sy0, px);
				SetPxRaw(out, dx, dy, px[0], px[1], px[2], px[3]);
			}
		}
		CopyTags(out, img);
		return 1;
	}

	for (int dy = 0; dy < h; dy++) {
		double fy = (dy + 0.5) * sy - 0.5;
		int y0 = (int)floor(fy);
		double wy = fy - y0;
		for (int dx = 0; dx < w; dx++) {
			double fx = (dx + 0.5) * sx - 0.5;
			int x0 = (int)floor(fx);
			double wx = fx - x0;

			unsigned char c00[4], c10[4], c01[4], c11[4];
			GetPxClamped(img, x0, y0, c00);
			GetPxClamped(img, x0 + 1, y0, c10);
			GetPxClamped(img, x0, y0 + 1, c01);
			GetPxClamped(img, x0 + 1, y0 + 1, c11);

			unsigned char px[4];
			for (int ch = 0; ch < 4; ch++) {
				double top = c00[ch] * (1 - wx) + c10[ch] * wx;
				double bot = c01[ch] * (1 - wx) + c11[ch] * wx;
				px[ch] = ClampByte((int)(top * (1 - wy) + bot * wy + 0.5));
			}
			SetPxRaw(out, dx, dy, px[0], px[1], px[2], px[3]);
		}
	}
	CopyTags(out, img);
	return 1;
}

int Image_Clone(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	LuaImage* out = PushBlankImage(L, img->width, img->height);
	memcpy(out->pixels, img->pixels, (size_t)img->width * img->height * 4);
	CopyTags(out, img);
	return 1;
}

// ===========================================================================
// In-place drawing primitives (all alpha-composited via BlendPx)
// ===========================================================================

int Image_FillRect(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	int x = (int)luaL_checkinteger(L, 2);
	int y = (int)luaL_checkinteger(L, 3);
	int w = (int)luaL_checkinteger(L, 4);
	int h = (int)luaL_checkinteger(L, 5);
	int r = (int)luaL_checkinteger(L, 6);
	int g = (int)luaL_checkinteger(L, 7);
	int b = (int)luaL_checkinteger(L, 8);
	int a = (int)luaL_optinteger(L, 9, 255);

	unsigned char cr = ClampByte(r), cg = ClampByte(g), cb = ClampByte(b), ca = ClampByte(a);
	int x0 = MAX(x, 0), y0 = MAX(y, 0);
	int x1 = MIN(x + w, img->width), y1 = MIN(y + h, img->height);
	for (int yy = y0; yy < y1; yy++)
		for (int xx = x0; xx < x1; xx++)
			BlendPx(img, xx, yy, cr, cg, cb, ca);
	return 0;
}

// Stamps a small filled square at (cx,cy) -- the building block DrawLine
// uses to draw a line "thick" pixels wide without gaps between steps.
static void StampSquare(LuaImage* img, int cx, int cy, int thickness, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
	int half = thickness / 2;
	for (int yy = cy - half; yy <= cy + (thickness - 1 - half); yy++)
		for (int xx = cx - half; xx <= cx + (thickness - 1 - half); xx++)
			BlendPx(img, xx, yy, r, g, b, a);
}

// Shortest distance from point (px,py) to the segment (x1,y1)-(x2,y2). Used
// by DrawLine's antialiased path to turn "how close is this pixel to the
// line" into edge coverage instead of a hard in/out test.
static double DistToSegment(double px, double py, double x1, double y1, double x2, double y2) {
	double dx = x2 - x1, dy = y2 - y1;
	double lenSq = dx * dx + dy * dy;
	double t = lenSq > 0.0 ? ((px - x1) * dx + (py - y1) * dy) / lenSq : 0.0;
	t = MAX(0.0, MIN(1.0, t));
	double cx = x1 + t * dx, cy = y1 + t * dy;
	double ddx = px - cx, ddy = py - cy;
	return sqrt(ddx * ddx + ddy * ddy);
}

int Image_DrawLine(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	int x1 = (int)luaL_checkinteger(L, 2);
	int y1 = (int)luaL_checkinteger(L, 3);
	int x2 = (int)luaL_checkinteger(L, 4);
	int y2 = (int)luaL_checkinteger(L, 5);
	int r = (int)luaL_checkinteger(L, 6);
	int g = (int)luaL_checkinteger(L, 7);
	int b = (int)luaL_checkinteger(L, 8);
	int a = (int)luaL_optinteger(L, 9, 255);
	int thickness = (int)luaL_optinteger(L, 10, 1);
	bool antialias = lua_toboolean(L, 11) != 0;
	if (thickness < 1) thickness = 1;

	unsigned char cr = ClampByte(r), cg = ClampByte(g), cb = ClampByte(b), ca = ClampByte(a);

	// Coverage-based path: for pixel-art use antialiasing is usually
	// unwanted (crisp edges are the point), so it stays opt-in.
	if (antialias) {
		double half = thickness / 2.0;
		int minX = MAX((int)floor(MIN(x1, x2) - half - 1), 0);
		int minY = MAX((int)floor(MIN(y1, y2) - half - 1), 0);
		int maxX = MIN((int)ceil(MAX(x1, x2) + half + 1), img->width - 1);
		int maxY = MIN((int)ceil(MAX(y1, y2) + half + 1), img->height - 1);
		for (int yy = minY; yy <= maxY; yy++) {
			for (int xx = minX; xx <= maxX; xx++) {
				double d = DistToSegment(xx + 0.5, yy + 0.5, x1, y1, x2, y2);
				double coverage = half + 0.5 - d;
				if (coverage <= 0.0) continue;
				if (coverage > 1.0) coverage = 1.0;
				BlendPx(img, xx, yy, cr, cg, cb, ClampByte((int)(ca * coverage + 0.5)));
			}
		}
		return 0;
	}

	int steps = MAX(abs(x2 - x1), abs(y2 - y1));
	if (steps == 0) {
		StampSquare(img, x1, y1, thickness, cr, cg, cb, ca);
		return 0;
	}
	for (int i = 0; i <= steps; i++) {
		double t = (double)i / steps;
		int x = (int)(x1 + (x2 - x1) * t + 0.5);
		int y = (int)(y1 + (y2 - y1) * t + 0.5);
		StampSquare(img, x, y, thickness, cr, cg, cb, ca);
	}
	return 0;
}

int Image_DrawCircle(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	int cx = (int)luaL_checkinteger(L, 2);
	int cy = (int)luaL_checkinteger(L, 3);
	int radius = (int)luaL_checkinteger(L, 4);
	int r = (int)luaL_checkinteger(L, 5);
	int g = (int)luaL_checkinteger(L, 6);
	int b = (int)luaL_checkinteger(L, 7);
	int a = (int)luaL_optinteger(L, 8, 255);
	bool filled = lua_isnoneornil(L, 9) ? true : (lua_toboolean(L, 9) != 0);
	bool antialias = lua_toboolean(L, 10) != 0;
	if (radius < 0)
		return luaL_error(L, "Image:DrawCircle: radius must be non-negative");

	unsigned char cr8 = ClampByte(r), cg8 = ClampByte(g), cb8 = ClampByte(b), ca8 = ClampByte(a);
	double rOuter2 = (double)radius * radius;
	double rInner2 = (double)(radius - 1) * (radius - 1);
	for (int yy = cy - radius - 1; yy <= cy + radius + 1; yy++) {
		for (int xx = cx - radius - 1; xx <= cx + radius + 1; xx++) {
			double dx = xx - cx, dy = yy - cy;
			double d2 = dx * dx + dy * dy;
			if (antialias) {
				double d = sqrt(d2);
				double coverage = filled ? (radius + 0.5 - d) : (1.0 - fabs(d - radius));
				if (coverage <= 0.0) continue;
				if (coverage > 1.0) coverage = 1.0;
				BlendPx(img, xx, yy, cr8, cg8, cb8, ClampByte((int)(ca8 * coverage + 0.5)));
			}
			else if (filled) {
				if (d2 <= rOuter2)
					BlendPx(img, xx, yy, cr8, cg8, cb8, ca8);
			}
			else {
				if (d2 <= rOuter2 && d2 >= rInner2)
					BlendPx(img, xx, yy, cr8, cg8, cb8, ca8);
			}
		}
	}
	return 0;
}

int Image_Composite(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	LuaImage* other = lua_toimage(L, 2);
	int x = (int)luaL_checkinteger(L, 3);
	int y = (int)luaL_checkinteger(L, 4);
	double alpha = luaL_optnumber(L, 5, 1.0);
	if (alpha < 0.0) alpha = 0.0;
	if (alpha > 1.0) alpha = 1.0;

	for (int oy = 0; oy < other->height; oy++) {
		int dy = y + oy;
		if (dy < 0 || dy >= img->height) continue;
		for (int ox = 0; ox < other->width; ox++) {
			int dx = x + ox;
			if (dx < 0 || dx >= img->width) continue;
			unsigned char px[4];
			GetPx(other, ox, oy, px);
			int a = (int)(px[3] * alpha + 0.5);
			if (a == 0) continue;
			BlendPx(img, dx, dy, px[0], px[1], px[2], ClampByte(a));
		}
	}
	return 0;
}

// ===========================================================================
// Diff -- answers "what region did the user hand-edit", so a canonical
// source can be updated by transplanting just the changed rectangle instead
// of the whole file (see pipeline-notes.md).
// ===========================================================================

int Image_Diff(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	LuaImage* other = lua_toimage(L, 2);
	if (img->width != other->width || img->height != other->height)
		return luaL_error(L, "Image:Diff: dimension mismatch (%dx%d vs %dx%d)", img->width, img->height, other->width, other->height);

	int minX = img->width, minY = img->height, maxX = -1, maxY = -1;
	lua_Integer changed = 0;
	size_t count = (size_t)img->width * img->height;
	for (size_t i = 0; i < count; i++) {
		const unsigned char* a = img->pixels + i * 4;
		const unsigned char* b = other->pixels + i * 4;
		if (memcmp(a, b, 4) != 0) {
			int x = (int)(i % img->width);
			int y = (int)(i / img->width);
			if (x < minX) minX = x;
			if (x > maxX) maxX = x;
			if (y < minY) minY = y;
			if (y > maxY) maxY = y;
			changed++;
		}
	}

	if (changed == 0) {
		lua_pushnil(L);
		lua_pushnil(L);
		lua_pushnil(L);
		lua_pushnil(L);
		lua_pushinteger(L, 0);
		return 5;
	}

	lua_pushinteger(L, minX);
	lua_pushinteger(L, minY);
	lua_pushinteger(L, maxX - minX + 1);
	lua_pushinteger(L, maxY - minY + 1);
	lua_pushinteger(L, changed);
	return 5;
}

// ===========================================================================
// Encode / persist
// ===========================================================================

int Image_Save(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	const char* path = luaL_checkstring(L, 2);

	size_t len;
	unsigned char* data = EncodeWithTags(img, &len);
	if (!data)
		return luaL_error(L, "Image:Save: failed to encode PNG");

	FILE* f = kitsune_fopen(path, "wb");
	if (!f) {
		kitsune_free(data);
		return luaL_error(L, "Image:Save: cannot open '%s' for writing", path);
	}
	size_t written = fwrite(data, 1, len, f);
	fclose(f);
	kitsune_free(data);
	if (written != len)
		return luaL_error(L, "Image:Save: short write to '%s'", path);

	lua_pushinteger(L, (lua_Integer)written);
	return 1;
}

int Image_ToBytes(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	size_t len;
	unsigned char* data = EncodeWithTags(img, &len);
	if (!data)
		return luaL_error(L, "Image:ToBytes: failed to encode PNG");
	lua_pushlstring(L, (const char*)data, len);
	kitsune_free(data);
	return 1;
}

// ===========================================================================
// RGB/HSV/HSL conversion (shared math + thin Lua wrappers). Standard
// formulas; h in [0,360), s/v/l in [0,1]. Used internally by Tint/AdjustHSV
// and also exposed directly since callers doing their own color math (e.g.
// picking N evenly-spaced hues for a palette) need it too.
// ===========================================================================

static void RGBtoHSV_d(double r, double g, double b, double& h, double& s, double& v) {
	r /= 255.0; g /= 255.0; b /= 255.0;
	double mx = MAX(r, MAX(g, b));
	double mn = MIN(r, MIN(g, b));
	double delta = mx - mn;
	v = mx;
	s = mx <= 0.0 ? 0.0 : delta / mx;
	if (delta <= 0.00001) { h = 0.0; return; }
	if (mx == r)      h = 60.0 * fmod((g - b) / delta, 6.0);
	else if (mx == g) h = 60.0 * (((b - r) / delta) + 2.0);
	else              h = 60.0 * (((r - g) / delta) + 4.0);
	if (h < 0.0) h += 360.0;
}

// Shared "which third of the hue wheel" branch used by both HSV->RGB and HSL->RGB.
static void HueToRGBPrime(double h, double c, double x, double& rp, double& gp, double& bp) {
	if (h < 60)       { rp = c; gp = x; bp = 0; }
	else if (h < 120) { rp = x; gp = c; bp = 0; }
	else if (h < 180) { rp = 0; gp = c; bp = x; }
	else if (h < 240) { rp = 0; gp = x; bp = c; }
	else if (h < 300) { rp = x; gp = 0; bp = c; }
	else              { rp = c; gp = 0; bp = x; }
}

static void HSVtoRGB_d(double h, double s, double v, double& r, double& g, double& b) {
	h = fmod(h, 360.0); if (h < 0.0) h += 360.0;
	double c = v * s;
	double x = c * (1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0));
	double m = v - c;
	double rp, gp, bp;
	HueToRGBPrime(h, c, x, rp, gp, bp);
	r = (rp + m) * 255.0; g = (gp + m) * 255.0; b = (bp + m) * 255.0;
}

static void RGBtoHSL_d(double r, double g, double b, double& h, double& s, double& l) {
	r /= 255.0; g /= 255.0; b /= 255.0;
	double mx = MAX(r, MAX(g, b));
	double mn = MIN(r, MIN(g, b));
	double delta = mx - mn;
	l = (mx + mn) / 2.0;
	if (delta <= 0.00001) { h = 0.0; s = 0.0; return; }
	s = delta / (1.0 - fabs(2.0 * l - 1.0));
	if (mx == r)      h = 60.0 * fmod((g - b) / delta, 6.0);
	else if (mx == g) h = 60.0 * (((b - r) / delta) + 2.0);
	else              h = 60.0 * (((r - g) / delta) + 4.0);
	if (h < 0.0) h += 360.0;
}

static void HSLtoRGB_d(double h, double s, double l, double& r, double& g, double& b) {
	h = fmod(h, 360.0); if (h < 0.0) h += 360.0;
	double c = (1.0 - fabs(2.0 * l - 1.0)) * s;
	double x = c * (1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0));
	double m = l - c / 2.0;
	double rp, gp, bp;
	HueToRGBPrime(h, c, x, rp, gp, bp);
	r = (rp + m) * 255.0; g = (gp + m) * 255.0; b = (bp + m) * 255.0;
}

int Image_RGBtoHSV(lua_State* L) {
	double h, s, v;
	RGBtoHSV_d(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3), h, s, v);
	lua_pushnumber(L, h);
	lua_pushnumber(L, s);
	lua_pushnumber(L, v);
	return 3;
}

int Image_HSVtoRGB(lua_State* L) {
	double r, g, b;
	HSVtoRGB_d(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3), r, g, b);
	lua_pushinteger(L, ClampByte((int)(r + 0.5)));
	lua_pushinteger(L, ClampByte((int)(g + 0.5)));
	lua_pushinteger(L, ClampByte((int)(b + 0.5)));
	return 3;
}

int Image_RGBtoHSL(lua_State* L) {
	double h, s, l;
	RGBtoHSL_d(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3), h, s, l);
	lua_pushnumber(L, h);
	lua_pushnumber(L, s);
	lua_pushnumber(L, l);
	return 3;
}

int Image_HSLtoRGB(lua_State* L) {
	double r, g, b;
	HSLtoRGB_d(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3), r, g, b);
	lua_pushinteger(L, ClampByte((int)(r + 0.5)));
	lua_pushinteger(L, ClampByte((int)(g + 0.5)));
	lua_pushinteger(L, ClampByte((int)(b + 0.5)));
	return 3;
}

// ===========================================================================
// Recolor / tone (all mutate in place, like FillRect/SetPixel)
// ===========================================================================

// Recolors every opaque pixel toward (r,g,b)'s hue/saturation while keeping
// each pixel's own brightness (HSV value) -- preserves shading/highlights so
// one base sprite can be reused as a "team color" / "ore type" variant.
int Image_Tint(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	int r = (int)luaL_checkinteger(L, 2);
	int g = (int)luaL_checkinteger(L, 3);
	int b = (int)luaL_checkinteger(L, 4);
	double strength = luaL_optnumber(L, 5, 1.0);
	if (strength < 0.0) strength = 0.0;
	if (strength > 1.0) strength = 1.0;

	double th, ts, tv;
	RGBtoHSV_d(r, g, b, th, ts, tv);

	size_t count = (size_t)img->width * img->height;
	for (size_t i = 0; i < count; i++) {
		unsigned char* p = img->pixels + i * 4;
		if (p[3] == 0) continue;
		double h, s, v;
		RGBtoHSV_d(p[0], p[1], p[2], h, s, v);
		double nr, ng, nb;
		HSVtoRGB_d(th, ts, v, nr, ng, nb);
		p[0] = ClampByte((int)(p[0] + (nr - p[0]) * strength + 0.5));
		p[1] = ClampByte((int)(p[1] + (ng - p[1]) * strength + 0.5));
		p[2] = ClampByte((int)(p[2] + (nb - p[2]) * strength + 0.5));
	}
	return 0;
}

// Exact color-swap recoloring: mapping is a Lua array of
// { {fromR,fromG,fromB, toR,toG,toB, opt tolerance}, ... }. The first rule
// within `tolerance` (per-channel max absolute difference, default 0 = exact)
// wins for each pixel.
int Image_RecolorPalette(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);

	int n = (int)lua_rawlen(L, 2);
	if (n == 0)
		return 0;

	struct Rule { int fr, fg, fb, tr, tg, tb, tol; };
	Rule* rules = (Rule*)kitsune_malloc(sizeof(Rule) * n);
	for (int i = 0; i < n; i++) {
		lua_rawgeti(L, 2, i + 1);
		luaL_checktype(L, -1, LUA_TTABLE);
		lua_rawgeti(L, -1, 1); rules[i].fr = (int)lua_tointeger(L, -1); lua_pop(L, 1);
		lua_rawgeti(L, -1, 2); rules[i].fg = (int)lua_tointeger(L, -1); lua_pop(L, 1);
		lua_rawgeti(L, -1, 3); rules[i].fb = (int)lua_tointeger(L, -1); lua_pop(L, 1);
		lua_rawgeti(L, -1, 4); rules[i].tr = (int)lua_tointeger(L, -1); lua_pop(L, 1);
		lua_rawgeti(L, -1, 5); rules[i].tg = (int)lua_tointeger(L, -1); lua_pop(L, 1);
		lua_rawgeti(L, -1, 6); rules[i].tb = (int)lua_tointeger(L, -1); lua_pop(L, 1);
		lua_rawgeti(L, -1, 7);
		rules[i].tol = lua_isnil(L, -1) ? 0 : (int)lua_tointeger(L, -1);
		lua_pop(L, 1);
		lua_pop(L, 1); // pop the rule table itself
	}

	size_t count = (size_t)img->width * img->height;
	for (size_t i = 0; i < count; i++) {
		unsigned char* p = img->pixels + i * 4;
		if (p[3] == 0) continue;
		for (int ri = 0; ri < n; ri++) {
			int dr = abs((int)p[0] - rules[ri].fr);
			int dg = abs((int)p[1] - rules[ri].fg);
			int db = abs((int)p[2] - rules[ri].fb);
			if (dr <= rules[ri].tol && dg <= rules[ri].tol && db <= rules[ri].tol) {
				p[0] = ClampByte(rules[ri].tr);
				p[1] = ClampByte(rules[ri].tg);
				p[2] = ClampByte(rules[ri].tb);
				break;
			}
		}
	}
	kitsune_free(rules);
	return 0;
}

// Adjusts hue/saturation/value over a region (defaults to the whole image).
// hueShift is in degrees; saturationMul/valueMul multiply the existing
// channel (so 1.0 = unchanged, >1 boosts, <1 mutes/darkens).
int Image_AdjustHSV(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	double hueShift = luaL_optnumber(L, 2, 0.0);
	double satMul = luaL_optnumber(L, 3, 1.0);
	double valMul = luaL_optnumber(L, 4, 1.0);
	int x = (int)luaL_optinteger(L, 5, 0);
	int y = (int)luaL_optinteger(L, 6, 0);
	int w = (int)luaL_optinteger(L, 7, img->width - x);
	int h = (int)luaL_optinteger(L, 8, img->height - y);

	int x0 = MAX(x, 0), y0 = MAX(y, 0);
	int x1 = MIN(x + w, img->width), y1 = MIN(y + h, img->height);
	for (int yy = y0; yy < y1; yy++) {
		for (int xx = x0; xx < x1; xx++) {
			unsigned char* p = img->pixels + (size_t)(yy * img->width + xx) * 4;
			if (p[3] == 0) continue;
			double hh, ss, vv;
			RGBtoHSV_d(p[0], p[1], p[2], hh, ss, vv);
			hh = fmod(hh + hueShift, 360.0); if (hh < 0.0) hh += 360.0;
			ss = MAX(0.0, MIN(1.0, ss * satMul));
			vv = MAX(0.0, MIN(1.0, vv * valMul));
			double r, g, b;
			HSVtoRGB_d(hh, ss, vv, r, g, b);
			p[0] = ClampByte((int)(r + 0.5));
			p[1] = ClampByte((int)(g + 0.5));
			p[2] = ClampByte((int)(b + 0.5));
		}
	}
	return 0;
}

// 4x4 ordered (Bayer) dither matrix, normalized to [0,16).
static const int BAYER4[4][4] = {
	{  0,  8,  2, 10 },
	{ 12,  4, 14,  6 },
	{  3, 11,  1,  9 },
	{ 15,  7, 13,  5 },
};

// Quantizes the image to `palette` (a Lua array of {r,g,b} triples), adding a
// per-pixel Bayer-matrix bias of +/-(amount/2) before picking the nearest
// palette color by Euclidean RGB distance -- breaks up flat color bands
// instead of hard-banding like naive nearest-color quantization would.
int Image_Dither(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);
	double amount = luaL_optnumber(L, 3, 32.0);

	int n = (int)lua_rawlen(L, 2);
	if (n == 0)
		return luaL_error(L, "Image:Dither: palette must have at least one color");

	unsigned char* palette = (unsigned char*)kitsune_malloc((size_t)3 * n);
	for (int i = 0; i < n; i++) {
		lua_rawgeti(L, 2, i + 1);
		luaL_checktype(L, -1, LUA_TTABLE);
		lua_rawgeti(L, -1, 1); palette[i * 3 + 0] = ClampByte((int)lua_tointeger(L, -1)); lua_pop(L, 1);
		lua_rawgeti(L, -1, 2); palette[i * 3 + 1] = ClampByte((int)lua_tointeger(L, -1)); lua_pop(L, 1);
		lua_rawgeti(L, -1, 3); palette[i * 3 + 2] = ClampByte((int)lua_tointeger(L, -1)); lua_pop(L, 1);
		lua_pop(L, 1);
	}

	size_t count = (size_t)img->width * img->height;
	for (size_t i = 0; i < count; i++) {
		unsigned char* p = img->pixels + i * 4;
		if (p[3] == 0) continue;
		int x = (int)(i % img->width);
		int y = (int)(i / img->width);
		double bias = (BAYER4[y & 3][x & 3] / 16.0 - 0.5) * amount;

		int tr = ClampByte((int)(p[0] + bias + 0.5));
		int tg = ClampByte((int)(p[1] + bias + 0.5));
		int tb = ClampByte((int)(p[2] + bias + 0.5));

		int best = 0;
		long bestDist = -1;
		for (int k = 0; k < n; k++) {
			long dr = tr - palette[k * 3 + 0], dg = tg - palette[k * 3 + 1], db = tb - palette[k * 3 + 2];
			long dist = dr * dr + dg * dg + db * db;
			if (bestDist < 0 || dist < bestDist) { bestDist = dist; best = k; }
		}
		p[0] = palette[best * 3 + 0]; p[1] = palette[best * 3 + 1]; p[2] = palette[best * 3 + 2];
	}
	kitsune_free(palette);
	return 0;
}

// Grows a colored border of `thickness` pixels around the image's existing
// silhouette (based on a snapshot of the original alpha, so the outline
// doesn't bleed into itself). Common "outlined icon/sprite" requirement.
int Image_Outline(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	int r = (int)luaL_checkinteger(L, 2);
	int g = (int)luaL_checkinteger(L, 3);
	int b = (int)luaL_checkinteger(L, 4);
	int a = (int)luaL_optinteger(L, 5, 255);
	int thickness = (int)luaL_optinteger(L, 6, 1);
	if (thickness < 1) thickness = 1;

	size_t count = (size_t)img->width * img->height;
	unsigned char* wasOpaque = (unsigned char*)kitsune_malloc(count);
	for (size_t i = 0; i < count; i++)
		wasOpaque[i] = img->pixels[i * 4 + 3] > 0 ? 1 : 0;

	unsigned char cr = ClampByte(r), cg = ClampByte(g), cb = ClampByte(b), ca = ClampByte(a);
	double t2 = (double)thickness * thickness;

	for (int y = 0; y < img->height; y++) {
		for (int x = 0; x < img->width; x++) {
			if (wasOpaque[(size_t)y * img->width + x])
				continue;
			bool near = false;
			for (int dy = -thickness; dy <= thickness && !near; dy++) {
				int ny = y + dy;
				if (ny < 0 || ny >= img->height) continue;
				for (int dx = -thickness; dx <= thickness; dx++) {
					if ((double)(dx * dx + dy * dy) > t2) continue;
					int nx = x + dx;
					if (nx < 0 || nx >= img->width) continue;
					if (wasOpaque[(size_t)ny * img->width + nx]) { near = true; break; }
				}
			}
			if (near)
				BlendPx(img, x, y, cr, cg, cb, ca);
		}
	}
	kitsune_free(wasOpaque);
	return 0;
}

// ===========================================================================
// Brush painting (mutate in place, built on BlendPx like every other
// drawing primitive)
// ===========================================================================

// Pastes `brush` centered on (cx,cy), alpha-blended, its own per-pixel alpha
// further scaled by `opacity`. Shared by Image_Stamp and Image_StrokePath.
static void StampBrush(LuaImage* img, const LuaImage* brush, double cx, double cy, double opacity) {
	int ox = (int)(cx - brush->width / 2.0 + 0.5);
	int oy = (int)(cy - brush->height / 2.0 + 0.5);
	for (int by = 0; by < brush->height; by++) {
		int dy = oy + by;
		if (dy < 0 || dy >= img->height) continue;
		for (int bx = 0; bx < brush->width; bx++) {
			int dx = ox + bx;
			if (dx < 0 || dx >= img->width) continue;
			unsigned char px[4];
			GetPx(brush, bx, by, px);
			int a = (int)(px[3] * opacity + 0.5);
			if (a == 0) continue;
			BlendPx(img, dx, dy, px[0], px[1], px[2], ClampByte(a));
		}
	}
}

int Image_Stamp(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	LuaImage* brush = lua_toimage(L, 2);
	double x = luaL_checknumber(L, 3);
	double y = luaL_checknumber(L, 4);
	double opacity = luaL_optnumber(L, 5, 1.0);
	if (opacity < 0.0) opacity = 0.0;
	if (opacity > 1.0) opacity = 1.0;
	StampBrush(img, brush, x, y, opacity);
	return 0;
}

// Drags `brush` along a polyline (a Lua array of {x,y} points), stamping it
// every `spacing` pixels of travelled distance so a fast multi-segment
// stroke has no gaps.
int Image_StrokePath(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	LuaImage* brush = lua_toimage(L, 2);
	luaL_checktype(L, 3, LUA_TTABLE);
	double spacing = luaL_optnumber(L, 4, 1.0);
	if (spacing < 0.5) spacing = 0.5;
	double opacity = luaL_optnumber(L, 5, 1.0);
	if (opacity < 0.0) opacity = 0.0;
	if (opacity > 1.0) opacity = 1.0;

	int n = (int)lua_rawlen(L, 3);
	if (n == 0)
		return 0;

	double* xs = (double*)kitsune_malloc(sizeof(double) * n);
	double* ys = (double*)kitsune_malloc(sizeof(double) * n);
	for (int i = 0; i < n; i++) {
		lua_rawgeti(L, 3, i + 1);
		luaL_checktype(L, -1, LUA_TTABLE);
		lua_rawgeti(L, -1, 1); xs[i] = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_rawgeti(L, -1, 2); ys[i] = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_pop(L, 1);
	}

	StampBrush(img, brush, xs[0], ys[0], opacity);
	for (int i = 0; i + 1 < n; i++) {
		double x1 = xs[i], y1 = ys[i], x2 = xs[i + 1], y2 = ys[i + 1];
		double dist = sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
		int steps = (int)(dist / spacing);
		for (int s = 1; s <= steps; s++) {
			double t = (double)s / steps;
			StampBrush(img, brush, x1 + (x2 - x1) * t, y1 + (y2 - y1) * t, opacity);
		}
	}

	kitsune_free(xs);
	kitsune_free(ys);
	return 0;
}

// ===========================================================================
// Geometric transforms (non-mutating, like Crop/Resize/Clone -- return a new
// Image rather than modifying the receiver)
// ===========================================================================

int Image_FlipHorizontal(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	LuaImage* out = PushBlankImage(L, img->width, img->height);
	for (int y = 0; y < img->height; y++) {
		for (int x = 0; x < img->width; x++) {
			unsigned char px[4];
			GetPx(img, img->width - 1 - x, y, px);
			SetPxRaw(out, x, y, px[0], px[1], px[2], px[3]);
		}
	}
	CopyTags(out, img);
	return 1;
}

int Image_FlipVertical(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	LuaImage* out = PushBlankImage(L, img->width, img->height);
	for (int y = 0; y < img->height; y++) {
		for (int x = 0; x < img->width; x++) {
			unsigned char px[4];
			GetPx(img, x, img->height - 1 - y, px);
			SetPxRaw(out, x, y, px[0], px[1], px[2], px[3]);
		}
	}
	CopyTags(out, img);
	return 1;
}

// clockwise defaults to true; pass false for counter-clockwise.
int Image_Rotate90(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	bool clockwise = lua_isnoneornil(L, 2) ? true : (lua_toboolean(L, 2) != 0);
	LuaImage* out = PushBlankImage(L, img->height, img->width);
	for (int y = 0; y < img->height; y++) {
		for (int x = 0; x < img->width; x++) {
			unsigned char px[4];
			GetPx(img, x, y, px);
			int nx, ny;
			if (clockwise) { nx = img->height - 1 - y; ny = x; }
			else           { nx = y; ny = img->width - 1 - x; }
			SetPxRaw(out, nx, ny, px[0], px[1], px[2], px[3]);
		}
	}
	CopyTags(out, img);
	return 1;
}

// Grows the canvas, pasting the original image at (left,top) and leaving the
// new border transparent. Non-mutating, like Crop/Resize -- any op that
// changes dimensions returns a new Image rather than resizing the receiver.
int Image_PadCanvas(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	int left = (int)luaL_checkinteger(L, 2);
	int top = (int)luaL_checkinteger(L, 3);
	int right = (int)luaL_checkinteger(L, 4);
	int bottom = (int)luaL_checkinteger(L, 5);
	if (left < 0 || top < 0 || right < 0 || bottom < 0)
		return luaL_error(L, "Image:PadCanvas: padding must be non-negative");

	LuaImage* out = PushBlankImage(L, img->width + left + right, img->height + top + bottom);
	for (int y = 0; y < img->height; y++) {
		const unsigned char* src = img->pixels + (size_t)(y * img->width) * 4;
		unsigned char* dst = out->pixels + (size_t)((y + top) * out->width + left) * 4;
		memcpy(dst, src, (size_t)img->width * 4);
	}
	CopyTags(out, img);
	return 1;
}

// ===========================================================================
// Blur (separable box blur, alpha-aware). Reused by DropShadow below.
// ===========================================================================

// Box-blurs `img` in place, `passes` times (3 box-blur passes closely
// approximates a Gaussian without needing a real Gaussian kernel). Blurs
// premultiplied color so transparent pixels don't bleed black into opaque
// edges, then un-premultiplies at the end.
static void BoxBlur(LuaImage* img, int radius, int passes) {
	if (radius <= 0 || passes <= 0) return;
	int w = img->width, h = img->height;
	size_t n = (size_t)w * h;

	double* buf = (double*)kitsune_malloc(sizeof(double) * n * 4); // premultiplied r,g,b + straight a
	double* tmp = (double*)kitsune_malloc(sizeof(double) * n * 4);

	for (size_t i = 0; i < n; i++) {
		const unsigned char* p = img->pixels + i * 4;
		double a = p[3];
		buf[i * 4 + 0] = p[0] * (a / 255.0);
		buf[i * 4 + 1] = p[1] * (a / 255.0);
		buf[i * 4 + 2] = p[2] * (a / 255.0);
		buf[i * 4 + 3] = a;
	}

	for (int pass = 0; pass < passes; pass++) {
		// horizontal: buf -> tmp
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				double sum[4] = { 0,0,0,0 };
				int cnt = 0;
				for (int dx = -radius; dx <= radius; dx++) {
					int sx = x + dx;
					if (sx < 0 || sx >= w) continue;
					const double* s = buf + (size_t)(y * w + sx) * 4;
					sum[0] += s[0]; sum[1] += s[1]; sum[2] += s[2]; sum[3] += s[3];
					cnt++;
				}
				double* d = tmp + (size_t)(y * w + x) * 4;
				d[0] = sum[0] / cnt; d[1] = sum[1] / cnt; d[2] = sum[2] / cnt; d[3] = sum[3] / cnt;
			}
		}
		// vertical: tmp -> buf
		for (int x = 0; x < w; x++) {
			for (int y = 0; y < h; y++) {
				double sum[4] = { 0,0,0,0 };
				int cnt = 0;
				for (int dy = -radius; dy <= radius; dy++) {
					int sy = y + dy;
					if (sy < 0 || sy >= h) continue;
					const double* s = tmp + (size_t)(sy * w + x) * 4;
					sum[0] += s[0]; sum[1] += s[1]; sum[2] += s[2]; sum[3] += s[3];
					cnt++;
				}
				double* d = buf + (size_t)(y * w + x) * 4;
				d[0] = sum[0] / cnt; d[1] = sum[1] / cnt; d[2] = sum[2] / cnt; d[3] = sum[3] / cnt;
			}
		}
	}

	for (size_t i = 0; i < n; i++) {
		double a = buf[i * 4 + 3];
		unsigned char* p = img->pixels + i * 4;
		p[3] = ClampByte((int)(a + 0.5));
		if (a > 0.5) {
			p[0] = ClampByte((int)(buf[i * 4 + 0] / (a / 255.0) + 0.5));
			p[1] = ClampByte((int)(buf[i * 4 + 1] / (a / 255.0) + 0.5));
			p[2] = ClampByte((int)(buf[i * 4 + 2] / (a / 255.0) + 0.5));
		}
		else {
			p[0] = p[1] = p[2] = 0;
		}
	}

	kitsune_free(buf);
	kitsune_free(tmp);
}

int Image_Blur(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	int radius = (int)luaL_checkinteger(L, 2);
	int passes = (int)luaL_optinteger(L, 3, 3);
	if (radius < 0)
		return luaL_error(L, "Image:Blur: radius must be non-negative");
	if (passes < 1) passes = 1;
	BoxBlur(img, radius, passes);
	return 0;
}

// Renders a blurred, offset, colored silhouette of `img` into a new (larger)
// canvas -- the "shadow.png" workflow the factorio-art pipeline currently
// does by hand. The canvas grows to fit both the blur falloff and however
// far offsetX/offsetY shift the silhouette, so a large offset (Factorio
// shadows commonly shift 40+ px) never clips.
int Image_DropShadow(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	int offsetX = (int)luaL_checkinteger(L, 2);
	int offsetY = (int)luaL_checkinteger(L, 3);
	int blurRadius = (int)luaL_checkinteger(L, 4);
	int r = (int)luaL_checkinteger(L, 5);
	int g = (int)luaL_checkinteger(L, 6);
	int b = (int)luaL_checkinteger(L, 7);
	int a = (int)luaL_optinteger(L, 8, 255);
	if (blurRadius < 0)
		return luaL_error(L, "Image:DropShadow: blurRadius must be non-negative");

	int margin = blurRadius + 2 + MAX(abs(offsetX), abs(offsetY));
	LuaImage* out = PushBlankImage(L, img->width + margin * 2, img->height + margin * 2);

	unsigned char cr = ClampByte(r), cg = ClampByte(g), cb = ClampByte(b), ca = ClampByte(a);
	for (int y = 0; y < img->height; y++) {
		for (int x = 0; x < img->width; x++) {
			unsigned char px[4];
			GetPx(img, x, y, px);
			if (px[3] == 0) continue;
			int dx = margin + offsetX + x;
			int dy = margin + offsetY + y;
			int alpha = (int)(px[3] * (ca / 255.0) + 0.5);
			SetPxRaw(out, dx, dy, cr, cg, cb, ClampByte(alpha));
		}
	}
	if (blurRadius > 0)
		BoxBlur(out, blurRadius, 3);
	return 1;
}

// ===========================================================================
// Filters / tone (mutate in place)
// ===========================================================================

int Image_Invert(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	int x = (int)luaL_optinteger(L, 2, 0);
	int y = (int)luaL_optinteger(L, 3, 0);
	int w = (int)luaL_optinteger(L, 4, img->width - x);
	int h = (int)luaL_optinteger(L, 5, img->height - y);
	int x0 = MAX(x, 0), y0 = MAX(y, 0);
	int x1 = MIN(x + w, img->width), y1 = MIN(y + h, img->height);
	for (int yy = y0; yy < y1; yy++) {
		for (int xx = x0; xx < x1; xx++) {
			unsigned char* p = img->pixels + (size_t)(yy * img->width + xx) * 4;
			if (p[3] == 0) continue;
			p[0] = 255 - p[0]; p[1] = 255 - p[1]; p[2] = 255 - p[2];
		}
	}
	return 0;
}

// Desaturates toward luminance (0.299R + 0.587G + 0.114B); `strength` (0-1,
// default 1) blends between the original color and full grayscale.
int Image_Grayscale(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	double strength = luaL_optnumber(L, 2, 1.0);
	if (strength < 0.0) strength = 0.0;
	if (strength > 1.0) strength = 1.0;
	int x = (int)luaL_optinteger(L, 3, 0);
	int y = (int)luaL_optinteger(L, 4, 0);
	int w = (int)luaL_optinteger(L, 5, img->width - x);
	int h = (int)luaL_optinteger(L, 6, img->height - y);
	int x0 = MAX(x, 0), y0 = MAX(y, 0);
	int x1 = MIN(x + w, img->width), y1 = MIN(y + h, img->height);
	for (int yy = y0; yy < y1; yy++) {
		for (int xx = x0; xx < x1; xx++) {
			unsigned char* p = img->pixels + (size_t)(yy * img->width + xx) * 4;
			if (p[3] == 0) continue;
			double lum = 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
			p[0] = ClampByte((int)(p[0] + (lum - p[0]) * strength + 0.5));
			p[1] = ClampByte((int)(p[1] + (lum - p[1]) * strength + 0.5));
			p[2] = ClampByte((int)(p[2] + (lum - p[2]) * strength + 0.5));
		}
	}
	return 0;
}

// brightness is additive (-255..255); contrast uses the standard "contrast
// correction factor" formula (-255..255, 0 = unchanged).
int Image_AdjustBrightnessContrast(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	double brightness = luaL_optnumber(L, 2, 0.0);
	double contrast = luaL_optnumber(L, 3, 0.0);
	int x = (int)luaL_optinteger(L, 4, 0);
	int y = (int)luaL_optinteger(L, 5, 0);
	int w = (int)luaL_optinteger(L, 6, img->width - x);
	int h = (int)luaL_optinteger(L, 7, img->height - y);

	double factor = (259.0 * (contrast + 255.0)) / (255.0 * (259.0 - contrast));
	int x0 = MAX(x, 0), y0 = MAX(y, 0);
	int x1 = MIN(x + w, img->width), y1 = MIN(y + h, img->height);
	for (int yy = y0; yy < y1; yy++) {
		for (int xx = x0; xx < x1; xx++) {
			unsigned char* p = img->pixels + (size_t)(yy * img->width + xx) * 4;
			if (p[3] == 0) continue;
			for (int c = 0; c < 3; c++) {
				double v = factor * ((p[c] + brightness) - 128.0) + 128.0;
				p[c] = ClampByte((int)(v + 0.5));
			}
		}
	}
	return 0;
}

// Turns the image into a two-tone stencil: pixels at/above `cutoff` (0-255,
// compared against luminance, or alpha when useAlpha=true) become (r,g,b,a);
// everything else becomes fully transparent. Handy for cutting a clean mask
// out of a rendered/blurred image.
int Image_Threshold(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	double cutoff = luaL_checknumber(L, 2);
	int r = (int)luaL_checkinteger(L, 3);
	int g = (int)luaL_checkinteger(L, 4);
	int b = (int)luaL_checkinteger(L, 5);
	int a = (int)luaL_optinteger(L, 6, 255);
	bool useAlpha = lua_toboolean(L, 7) != 0;

	unsigned char cr = ClampByte(r), cg = ClampByte(g), cb = ClampByte(b), ca = ClampByte(a);
	size_t count = (size_t)img->width * img->height;
	for (size_t i = 0; i < count; i++) {
		unsigned char* p = img->pixels + i * 4;
		double value = useAlpha ? p[3] : (0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]);
		if (value >= cutoff) { p[0] = cr; p[1] = cg; p[2] = cb; p[3] = ca; }
		else                 { p[0] = 0;  p[1] = 0;  p[2] = 0;  p[3] = 0; }
	}
	return 0;
}

// Multiplies img's alpha by mask's alpha (or luminance, if useLuminance) --
// combine an arbitrary painted/generated shape with another image as a
// stencil. Both images must be the same size (like Diff).
int Image_ApplyMask(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	LuaImage* mask = lua_toimage(L, 2);
	bool useLuminance = lua_toboolean(L, 3) != 0;
	if (img->width != mask->width || img->height != mask->height)
		return luaL_error(L, "Image:ApplyMask: dimension mismatch (%dx%d vs %dx%d)", img->width, img->height, mask->width, mask->height);

	size_t count = (size_t)img->width * img->height;
	for (size_t i = 0; i < count; i++) {
		const unsigned char* mp = mask->pixels + i * 4;
		double factor = useLuminance
			? (0.299 * mp[0] + 0.587 * mp[1] + 0.114 * mp[2]) / 255.0
			: (mp[3] / 255.0);
		unsigned char* p = img->pixels + i * 4;
		p[3] = ClampByte((int)(p[3] * factor + 0.5));
	}
	return 0;
}

// Deterministic per-pixel random offset (seeded LCG, so the same seed always
// reproduces the same grain) over a region -- adds texture/grit without
// needing an AI generation round-trip.
static double NoiseRand(unsigned int* state) {
	*state = *state * 1664525u + 1013904223u;
	return ((*state >> 8) & 0xFFFFFFu) / (double)0xFFFFFFu;
}

int Image_Noise(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	double amount = luaL_optnumber(L, 2, 20.0);
	unsigned int seed = (unsigned int)luaL_optinteger(L, 3, 12345);
	bool monochrome = lua_isnoneornil(L, 4) ? true : (lua_toboolean(L, 4) != 0);
	int x = (int)luaL_optinteger(L, 5, 0);
	int y = (int)luaL_optinteger(L, 6, 0);
	int w = (int)luaL_optinteger(L, 7, img->width - x);
	int h = (int)luaL_optinteger(L, 8, img->height - y);

	int x0 = MAX(x, 0), y0 = MAX(y, 0);
	int x1 = MIN(x + w, img->width), y1 = MIN(y + h, img->height);
	unsigned int state = seed;
	for (int yy = y0; yy < y1; yy++) {
		for (int xx = x0; xx < x1; xx++) {
			unsigned char* p = img->pixels + (size_t)(yy * img->width + xx) * 4;
			if (p[3] == 0) continue;
			if (monochrome) {
				int delta = (int)((NoiseRand(&state) - 0.5) * 2.0 * amount);
				p[0] = ClampByte(p[0] + delta);
				p[1] = ClampByte(p[1] + delta);
				p[2] = ClampByte(p[2] + delta);
			}
			else {
				p[0] = ClampByte(p[0] + (int)((NoiseRand(&state) - 0.5) * 2.0 * amount));
				p[1] = ClampByte(p[1] + (int)((NoiseRand(&state) - 0.5) * 2.0 * amount));
				p[2] = ClampByte(p[2] + (int)((NoiseRand(&state) - 0.5) * 2.0 * amount));
			}
		}
	}
	return 0;
}

// ===========================================================================
// Fill primitives (mutate in place, alpha-composited via BlendPx)
// ===========================================================================

int Image_FillGradientLinear(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	double x1 = luaL_checknumber(L, 2);
	double y1 = luaL_checknumber(L, 3);
	int r1 = (int)luaL_checkinteger(L, 4);
	int g1 = (int)luaL_checkinteger(L, 5);
	int b1 = (int)luaL_checkinteger(L, 6);
	int a1 = (int)luaL_optinteger(L, 7, 255);
	double x2 = luaL_checknumber(L, 8);
	double y2 = luaL_checknumber(L, 9);
	int r2 = (int)luaL_checkinteger(L, 10);
	int g2 = (int)luaL_checkinteger(L, 11);
	int b2 = (int)luaL_checkinteger(L, 12);
	int a2 = (int)luaL_optinteger(L, 13, 255);

	double dx = x2 - x1, dy = y2 - y1;
	double lenSq = dx * dx + dy * dy;
	for (int y = 0; y < img->height; y++) {
		for (int x = 0; x < img->width; x++) {
			double t = lenSq > 0.0 ? (((x + 0.5) - x1) * dx + ((y + 0.5) - y1) * dy) / lenSq : 0.0;
			t = MAX(0.0, MIN(1.0, t));
			BlendPx(img, x, y,
				ClampByte((int)(r1 + (r2 - r1) * t + 0.5)),
				ClampByte((int)(g1 + (g2 - g1) * t + 0.5)),
				ClampByte((int)(b1 + (b2 - b1) * t + 0.5)),
				ClampByte((int)(a1 + (a2 - a1) * t + 0.5)));
		}
	}
	return 0;
}

int Image_FillGradientRadial(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	double cx = luaL_checknumber(L, 2);
	double cy = luaL_checknumber(L, 3);
	double radius = luaL_checknumber(L, 4);
	int r1 = (int)luaL_checkinteger(L, 5);
	int g1 = (int)luaL_checkinteger(L, 6);
	int b1 = (int)luaL_checkinteger(L, 7);
	int a1 = (int)luaL_optinteger(L, 8, 255);
	int r2 = (int)luaL_checkinteger(L, 9);
	int g2 = (int)luaL_checkinteger(L, 10);
	int b2 = (int)luaL_checkinteger(L, 11);
	int a2 = (int)luaL_optinteger(L, 12, 255);
	if (radius <= 0.0)
		return luaL_error(L, "Image:FillGradientRadial: radius must be positive");

	for (int y = 0; y < img->height; y++) {
		for (int x = 0; x < img->width; x++) {
			double dx = (x + 0.5) - cx, dy = (y + 0.5) - cy;
			double t = sqrt(dx * dx + dy * dy) / radius;
			t = MAX(0.0, MIN(1.0, t));
			BlendPx(img, x, y,
				ClampByte((int)(r1 + (r2 - r1) * t + 0.5)),
				ClampByte((int)(g1 + (g2 - g1) * t + 0.5)),
				ClampByte((int)(b1 + (b2 - b1) * t + 0.5)),
				ClampByte((int)(a1 + (a2 - a1) * t + 0.5)));
		}
	}
	return 0;
}

// Even-odd scanline fill of an arbitrary (simple or self-intersecting)
// polygon. `points` is a Lua array of {x,y} pairs.
int Image_FillPolygon(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);
	int r = (int)luaL_checkinteger(L, 3);
	int g = (int)luaL_checkinteger(L, 4);
	int b = (int)luaL_checkinteger(L, 5);
	int a = (int)luaL_optinteger(L, 6, 255);

	int n = (int)lua_rawlen(L, 2);
	if (n < 3)
		return 0;

	double* xs = (double*)kitsune_malloc(sizeof(double) * n);
	double* ys = (double*)kitsune_malloc(sizeof(double) * n);
	double minY = 1e18, maxY = -1e18;
	for (int i = 0; i < n; i++) {
		lua_rawgeti(L, 2, i + 1);
		luaL_checktype(L, -1, LUA_TTABLE);
		lua_rawgeti(L, -1, 1); xs[i] = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_rawgeti(L, -1, 2); ys[i] = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_pop(L, 1);
		if (ys[i] < minY) minY = ys[i];
		if (ys[i] > maxY) maxY = ys[i];
	}

	unsigned char cr = ClampByte(r), cg = ClampByte(g), cb = ClampByte(b), ca = ClampByte(a);
	int y0 = MAX((int)floor(minY), 0);
	int y1 = MIN((int)ceil(maxY), img->height - 1);
	double* xints = (double*)kitsune_malloc(sizeof(double) * n);

	for (int y = y0; y <= y1; y++) {
		double sy = y + 0.5;
		int cnt = 0;
		for (int i = 0; i < n; i++) {
			int j = (i + 1) % n;
			double ya = ys[i], yb = ys[j];
			if ((ya <= sy && yb > sy) || (yb <= sy && ya > sy)) {
				double t = (sy - ya) / (yb - ya);
				xints[cnt++] = xs[i] + t * (xs[j] - xs[i]);
			}
		}
		// insertion sort (cnt is small: one pair of crossings per polygon edge pair)
		for (int i = 1; i < cnt; i++) {
			double key = xints[i]; int k = i - 1;
			while (k >= 0 && xints[k] > key) { xints[k + 1] = xints[k]; k--; }
			xints[k + 1] = key;
		}
		for (int i = 0; i + 1 < cnt; i += 2) {
			int xStart = MAX((int)ceil(xints[i] - 0.5), 0);
			int xEnd = MIN((int)floor(xints[i + 1] - 0.5), img->width - 1);
			for (int x = xStart; x <= xEnd; x++)
				BlendPx(img, x, y, cr, cg, cb, ca);
		}
	}

	kitsune_free(xs);
	kitsune_free(ys);
	kitsune_free(xints);
	return 0;
}

// ===========================================================================
// Text (bundled minimal 3x5 bitmap font: digits + a few symbols -- enough
// for numeric labels/coordinates/counters, not a full alphabet)
// ===========================================================================

struct Glyph3x5 { char ch; unsigned char rows[5]; };

// Each glyph is 5 rows of 3 bits (bit 2 = leftmost column, bit 0 = rightmost).
static const Glyph3x5 FONT_3X5[] = {
	{ '0', { 0x7,0x5,0x5,0x5,0x7 } },
	{ '1', { 0x2,0x6,0x2,0x2,0x7 } },
	{ '2', { 0x7,0x1,0x7,0x4,0x7 } },
	{ '3', { 0x7,0x1,0x7,0x1,0x7 } },
	{ '4', { 0x5,0x5,0x7,0x1,0x1 } },
	{ '5', { 0x7,0x4,0x7,0x1,0x7 } },
	{ '6', { 0x7,0x4,0x7,0x5,0x7 } },
	{ '7', { 0x7,0x1,0x1,0x1,0x1 } },
	{ '8', { 0x7,0x5,0x7,0x5,0x7 } },
	{ '9', { 0x7,0x5,0x7,0x1,0x7 } },
	{ ' ', { 0x0,0x0,0x0,0x0,0x0 } },
	{ '-', { 0x0,0x0,0x7,0x0,0x0 } },
	{ '.', { 0x0,0x0,0x0,0x0,0x2 } },
	{ ':', { 0x0,0x2,0x0,0x2,0x0 } },
};
static const int FONT_3X5_COUNT = sizeof(FONT_3X5) / sizeof(FONT_3X5[0]);

static const Glyph3x5* FindGlyph(char ch) {
	for (int i = 0; i < FONT_3X5_COUNT; i++)
		if (FONT_3X5[i].ch == ch) return &FONT_3X5[i];
	return NULL; // unsupported characters render as a blank cell
}

// Draws `text` with its top-left at (x,y) using the bundled 3x5 digit font,
// each glyph scaled by `scale` (default 1 = 3x5px) with 1 scaled pixel of
// spacing between characters. Only digits, space, '-', '.', ':' are
// supported today; anything else renders as a blank cell rather than
// erroring, so a mixed-case label still lays out at the right width.
int Image_DrawText(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	const char* text = luaL_checkstring(L, 2);
	int x = (int)luaL_checkinteger(L, 3);
	int y = (int)luaL_checkinteger(L, 4);
	int r = (int)luaL_checkinteger(L, 5);
	int g = (int)luaL_checkinteger(L, 6);
	int b = (int)luaL_checkinteger(L, 7);
	int a = (int)luaL_optinteger(L, 8, 255);
	int scale = (int)luaL_optinteger(L, 9, 1);
	if (scale < 1) scale = 1;

	unsigned char cr = ClampByte(r), cg = ClampByte(g), cb = ClampByte(b), ca = ClampByte(a);
	int cellW = (3 + 1) * scale;
	int penX = x;
	for (const char* p = text; *p; p++) {
		const Glyph3x5* glyph = FindGlyph(*p);
		if (glyph) {
			for (int row = 0; row < 5; row++) {
				for (int col = 0; col < 3; col++) {
					if (!(glyph->rows[row] & (1 << (2 - col)))) continue;
					for (int sy = 0; sy < scale; sy++)
						for (int sx = 0; sx < scale; sx++)
							BlendPx(img, penX + col * scale + sx, y + row * scale + sy, cr, cg, cb, ca);
				}
			}
		}
		penX += cellW;
	}
	return 0;
}

// ===========================================================================
// Analysis (read-only)
// ===========================================================================

// Returns the top `n` most common colors as { {r=,g=,b=,count=}, ... },
// ranked by frequency. Colors are bucketed to 5 bits/channel (32768 buckets)
// before counting so near-identical anti-aliased shades merge into one
// entry; each bucket reports the true average color of the pixels in it.
// Feeds directly into Dither's palette argument.
int Image_ExtractPalette(lua_State* L) {
	LuaImage* img = lua_toimage(L, 1);
	int n = (int)luaL_optinteger(L, 2, 8);
	if (n < 1) n = 1;

	const int BUCKETS = 32 * 32 * 32;
	long* counts = (long*)kitsune_calloc(BUCKETS, sizeof(long));
	long* sumR = (long*)kitsune_calloc(BUCKETS, sizeof(long));
	long* sumG = (long*)kitsune_calloc(BUCKETS, sizeof(long));
	long* sumB = (long*)kitsune_calloc(BUCKETS, sizeof(long));

	size_t count = (size_t)img->width * img->height;
	for (size_t i = 0; i < count; i++) {
		const unsigned char* p = img->pixels + i * 4;
		if (p[3] == 0) continue;
		int idx = ((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3);
		counts[idx]++;
		sumR[idx] += p[0]; sumG[idx] += p[1]; sumB[idx] += p[2];
	}

	lua_newtable(L);
	int rank = 0;
	for (int pick = 0; pick < n; pick++) {
		int best = -1;
		long bestCount = 0;
		for (int idx = 0; idx < BUCKETS; idx++) {
			if (counts[idx] > bestCount) { bestCount = counts[idx]; best = idx; }
		}
		if (best < 0 || bestCount == 0) break;

		lua_createtable(L, 0, 4);
		lua_pushstring(L, "r"); lua_pushinteger(L, sumR[best] / bestCount); lua_settable(L, -3);
		lua_pushstring(L, "g"); lua_pushinteger(L, sumG[best] / bestCount); lua_settable(L, -3);
		lua_pushstring(L, "b"); lua_pushinteger(L, sumB[best] / bestCount); lua_settable(L, -3);
		lua_pushstring(L, "count"); lua_pushinteger(L, bestCount); lua_settable(L, -3);
		lua_rawseti(L, -2, ++rank);

		counts[best] = 0; // exclude from the next pick
	}

	kitsune_free(counts);
	kitsune_free(sumR);
	kitsune_free(sumG);
	kitsune_free(sumB);
	return 1;
}
