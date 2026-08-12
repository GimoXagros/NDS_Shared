#ifndef UNICODETEXT_HEADER
#define UNICODETEXT_HEADER

#include <nds.h>
#include <stdbool.h>
#include <stddef.h>

bool textIsValidUtf8(const char *text);
u32 textDecodeCharacter(const unsigned char *text, unsigned int remaining,
		bool utf8, unsigned int *byteCount);
unsigned int textColumns(const char *text);
const char *textSkipColumns(const char *text, unsigned int columns);
void textCopyColumns(char *destination, size_t destinationSize,
		const char *source, unsigned int maxColumns);
u16 textTileForCodepoint(u32 codepoint);
void textResetGlyphCache(void);

#endif // UNICODETEXT_HEADER
