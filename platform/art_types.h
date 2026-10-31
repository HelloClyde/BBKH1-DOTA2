#ifndef H1_DOTA_ART_TYPES_H
#define H1_DOTA_ART_TYPES_H
#ifndef H1_DOTA_ART_TYPE_DEFINED
#define H1_DOTA_ART_TYPE_DEFINED
typedef struct DotaArt { unsigned short w,h;const unsigned short *pixels;const unsigned char *alpha; } DotaArt;
#endif
#ifndef H1_DOTA_GLYPH_TYPE_DEFINED
#define H1_DOTA_GLYPH_TYPE_DEFINED
typedef struct DotaGlyph {unsigned short code;unsigned char width;unsigned short rows[14];} DotaGlyph;
#endif
#endif
