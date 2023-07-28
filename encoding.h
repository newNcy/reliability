#pragma once 
#include <stdint.h>
//---------------------------------------------------------------------
// WORD ORDER
//---------------------------------------------------------------------
#ifndef IWORDS_BIG_ENDIAN
#ifdef _BIG_ENDIAN_
#if _BIG_ENDIAN_
            #define IWORDS_BIG_ENDIAN 1
        #endif
#endif
#ifndef IWORDS_BIG_ENDIAN
#if defined(__hppa__) || \
            defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
            (defined(__MIPS__) && defined(__MISPEB__)) || \
            defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
            defined(__sparc__) || defined(__powerpc__) || \
            defined(__mc68000__) || defined(__s390x__) || defined(__s390__)
#define IWORDS_BIG_ENDIAN 1
#endif
#endif
#ifndef IWORDS_BIG_ENDIAN
#define IWORDS_BIG_ENDIAN  0
#endif
#endif
typedef unsigned char byte;

/* encode 8 bits unsigned int (lsb) */
inline byte* encode8u(byte* p, uint8_t w)
{
	*(unsigned char*)p = w;
	p += 1;
	return p;
}

/* Decode 8 bits unsigned int (lsb) */
inline const byte* decode8u(const byte* p, uint8_t* w)
{
	*w = *(const unsigned char*)p;
	p += 1;
	return p;
}

/* encode 16 bits unsigned int (lsb) */
inline byte *encode16u(byte *p, uint16_t w)
{
#if IWORDS_BIG_ENDIAN
    *(byte*)(p + 0) = (w & 255);
	*(byte*)(p + 1) = (w >> 8);
#else
	*(unsigned short*)(p) = w;
#endif
    p += 2;
    return p;
}

/* Decode 16 bits unsigned int (lsb) */
inline const byte *decode16u(const byte *p, uint16_t *w)
{
#if IWORDS_BIG_ENDIAN
    *w = *(const unsigned char*)(p + 1);
	*w = *(const unsigned char*)(p + 0) + (*w << 8);
#else
    *w = *(const unsigned short*)p;
#endif
    p += 2;
    return p;
}

/* encode 32 bits unsigned int (lsb) */
inline byte *encode32u(byte *p, uint32_t l)
{
#if IWORDS_BIG_ENDIAN
    *(unsigned char*)(p + 0) = (unsigned char)((l >>  0) & 0xff);
	*(unsigned char*)(p + 1) = (unsigned char)((l >>  8) & 0xff);
	*(unsigned char*)(p + 2) = (unsigned char)((l >> 16) & 0xff);
	*(unsigned char*)(p + 3) = (unsigned char)((l >> 24) & 0xff);
#else
    *(uint32_t*)p = l;
#endif
    p += 4;
    return p;
}

/* Decode 32 bits unsigned int (lsb) */
inline const byte *decode32u(const byte *p, uint32_t *l)
{
#if IWORDS_BIG_ENDIAN
    *l = *(const unsigned char*)(p + 3);
	*l = *(const unsigned char*)(p + 2) + (*l << 8);
	*l = *(const unsigned char*)(p + 1) + (*l << 8);
	*l = *(const unsigned char*)(p + 0) + (*l << 8);
#else
    *l = *(const uint32_t*)p;
#endif
    p += 4;
    return p;
}

static inline long _itimediff(IUINT32 later, IUINT32 earlier) 
{
	return ((IINT32)(later - earlier));
}

