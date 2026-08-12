#include <nds.h>

#include <string.h>

#include "UnicodeText.h"
#include "unicode_font_bin.h"
#include "cp949_table_bin.h"

#define INVALID_CODEPOINT 0xFFFD
#define FIRST_DYNAMIC_TILE 0x1A0
#define DYNAMIC_TILE_COUNT (0x400 - FIRST_DYNAMIC_TILE)
#define GLYPH_HASH_SIZE 1024

static u16 glyphHashKeys[GLYPH_HASH_SIZE];
static u16 glyphHashTiles[GLYPH_HASH_SIZE];
static unsigned int nextDynamicTile;

static bool decodeUtf8Character(const unsigned char *text,
		unsigned int remaining, u32 *codepoint, unsigned int *byteCount) {
	const unsigned char first = text[0];
	if (first < 0x80) {
		*codepoint = first;
		*byteCount = 1;
		return true;
	}
	if (first >= 0xC2 && first <= 0xDF && remaining >= 2
			&& (text[1] & 0xC0) == 0x80) {
		*codepoint = ((first & 0x1F) << 6) | (text[1] & 0x3F);
		*byteCount = 2;
		return true;
	}
	if (first >= 0xE0 && first <= 0xEF && remaining >= 3
			&& (text[1] & 0xC0) == 0x80 && (text[2] & 0xC0) == 0x80
			&& !(first == 0xE0 && text[1] < 0xA0)
			&& !(first == 0xED && text[1] >= 0xA0)) {
		*codepoint = ((first & 0x0F) << 12)
				| ((text[1] & 0x3F) << 6) | (text[2] & 0x3F);
		*byteCount = 3;
		return true;
	}
	if (first >= 0xF0 && first <= 0xF4 && remaining >= 4
			&& (text[1] & 0xC0) == 0x80 && (text[2] & 0xC0) == 0x80
			&& (text[3] & 0xC0) == 0x80
			&& !(first == 0xF0 && text[1] < 0x90)
			&& !(first == 0xF4 && text[1] >= 0x90)) {
		*codepoint = ((first & 0x07) << 18) | ((text[1] & 0x3F) << 12)
				| ((text[2] & 0x3F) << 6) | (text[3] & 0x3F);
		*byteCount = 4;
		return true;
	}
	return false;
}

bool textIsValidUtf8(const char *text) {
	if (text == NULL) return false;
	const unsigned char *cursor = (const unsigned char *)text;
	unsigned int remaining = strlen(text);
	while (remaining) {
		u32 codepoint;
		unsigned int byteCount;
		if (!decodeUtf8Character(cursor, remaining, &codepoint, &byteCount)) {
			return false;
		}
		cursor += byteCount;
		remaining -= byteCount;
	}
	return true;
}

static u32 decodeCp949Character(const unsigned char *text,
		unsigned int remaining, unsigned int *byteCount) {
	if (text[0] < 0x80) {
		*byteCount = 1;
		return text[0];
	}
	if (remaining >= 2 && text[0] >= 0x81 && text[0] <= 0xFE) {
		const unsigned int index = (text[0] - 0x81) * 256 + text[1];
		const u32 codepoint = cp949_table_bin[index * 2]
				| (cp949_table_bin[index * 2 + 1] << 8);
		if (codepoint) {
			*byteCount = 2;
			return codepoint;
		}
	}
	*byteCount = 1;
	return INVALID_CODEPOINT;
}

u32 textDecodeCharacter(const unsigned char *text, unsigned int remaining,
		bool utf8, unsigned int *byteCount) {
	u32 codepoint;
	if (utf8 && decodeUtf8Character(text, remaining, &codepoint, byteCount)) {
		return codepoint;
	}
	return decodeCp949Character(text, remaining, byteCount);
}

unsigned int textColumns(const char *text) {
	if (text == NULL) return 0;
	const bool utf8 = textIsValidUtf8(text);
	const unsigned char *cursor = (const unsigned char *)text;
	unsigned int remaining = strlen(text);
	unsigned int columns = 0;
	while (remaining) {
		unsigned int byteCount;
		textDecodeCharacter(cursor, remaining, utf8, &byteCount);
		cursor += byteCount;
		remaining -= byteCount;
		columns++;
	}
	return columns;
}

const char *textSkipColumns(const char *text, unsigned int columns) {
	if (text == NULL) return NULL;
	const bool utf8 = textIsValidUtf8(text);
	const unsigned char *cursor = (const unsigned char *)text;
	unsigned int remaining = strlen(text);
	while (remaining && columns) {
		unsigned int byteCount;
		textDecodeCharacter(cursor, remaining, utf8, &byteCount);
		cursor += byteCount;
		remaining -= byteCount;
		columns--;
	}
	return (const char *)cursor;
}

void textCopyColumns(char *destination, size_t destinationSize,
		const char *source, unsigned int maxColumns) {
	if (destination == NULL || destinationSize == 0) return;
	destination[0] = 0;
	if (source == NULL) return;
	const bool utf8 = textIsValidUtf8(source);
	const unsigned char *cursor = (const unsigned char *)source;
	unsigned int remaining = strlen(source);
	size_t used = 0;
	while (remaining && maxColumns) {
		unsigned int byteCount;
		textDecodeCharacter(cursor, remaining, utf8, &byteCount);
		if (used + byteCount >= destinationSize) break;
		memcpy(destination + used, cursor, byteCount);
		used += byteCount;
		cursor += byteCount;
		remaining -= byteCount;
		maxColumns--;
	}
	destination[used] = 0;
}

static u16 read16(const unsigned char *value) {
	return value[0] | (value[1] << 8);
}

static u32 read32(const unsigned char *value) {
	return value[0] | (value[1] << 8) | (value[2] << 16)
			| (value[3] << 24);
}

static const unsigned char *bitmapForCodepoint(u32 codepoint) {
	if (codepoint > 0xFFFF || unicode_font_bin_size < 12
			|| memcmp(unicode_font_bin, "GYUF", 4) != 0
			|| read16(unicode_font_bin + 4) != 1) {
		return NULL;
	}
	const u32 count = read32(unicode_font_bin + 8);
	u32 low = 0;
	u32 high = count;
	while (low < high) {
		const u32 mid = low + (high - low) / 2;
		const unsigned char *record = unicode_font_bin + 12 + mid * 10;
		if (read16(record) < codepoint) low = mid + 1;
		else high = mid;
	}
	if (low >= count) return NULL;
	const unsigned char *record = unicode_font_bin + 12 + low * 10;
	return read16(record) == codepoint ? record + 2 : NULL;
}

static unsigned int glyphHashSlot(u32 codepoint) {
	return (codepoint * 2654435761U) & (GLYPH_HASH_SIZE - 1);
}

static bool bitmapPixel(const unsigned char *bitmap, int x, int y) {
	return x >= 0 && x < 8 && y >= 0 && y < 8
			&& (bitmap[y] & (1 << (7 - x))) != 0;
}

static u8 styledGlyphPixel(const unsigned char *bitmap, int x, int y) {
	// EmuFont uses a white-to-grey vertical bevel with a black lower/right
	// outline. Apply the same palette ramp to the monochrome CJK glyphs.
	static const u8 shadeByRow[8] = {9, 9, 9, 8, 7, 6, 5, 4};
	if (bitmapPixel(bitmap, x, y)) return shadeByRow[y];
	if (bitmapPixel(bitmap, x - 1, y)
			|| bitmapPixel(bitmap, x, y - 1)
			|| bitmapPixel(bitmap, x - 1, y - 1)) return 1;
	return 0;
}

u16 textTileForCodepoint(u32 codepoint) {
	if (codepoint < 0x80) return 0x100 + codepoint;
	if (codepoint > 0xFFFF) return 0x100 + '?';
	const unsigned int hash = glyphHashSlot(codepoint);
	for (unsigned int probe = 0; probe < GLYPH_HASH_SIZE; probe++) {
		const unsigned int slot = (hash + probe) & (GLYPH_HASH_SIZE - 1);
		if (glyphHashTiles[slot] == 0) break;
		if (glyphHashKeys[slot] == codepoint) return glyphHashTiles[slot] - 1;
	}
	if (nextDynamicTile >= DYNAMIC_TILE_COUNT) return 0x100 + '?';
	const unsigned char *bitmap = bitmapForCodepoint(codepoint);
	if (bitmap == NULL) return 0x100 + '?';

	const u16 tile = FIRST_DYNAMIC_TILE + nextDynamicTile++;
	for (unsigned int probe = 0; probe < GLYPH_HASH_SIZE; probe++) {
		const unsigned int slot = (hash + probe) & (GLYPH_HASH_SIZE - 1);
		if (glyphHashTiles[slot] == 0) {
			glyphHashKeys[slot] = codepoint;
			glyphHashTiles[slot] = tile + 1;
			break;
		}
	}
	u8 *destination = (u8 *)BG_GFX_SUB + tile * 32;
	for (unsigned int y = 0; y < 8; y++) {
		for (unsigned int pair = 0; pair < 4; pair++) {
			const unsigned int left = pair * 2;
			u8 value = styledGlyphPixel(bitmap, left, y);
			value |= styledGlyphPixel(bitmap, left + 1, y) << 4;
			destination[y * 4 + pair] = value;
		}
	}
	return tile;
}

void textResetGlyphCache(void) {
	memset(glyphHashKeys, 0, sizeof(glyphHashKeys));
	memset(glyphHashTiles, 0, sizeof(glyphHashTiles));
	nextDynamicTile = 0;
}
