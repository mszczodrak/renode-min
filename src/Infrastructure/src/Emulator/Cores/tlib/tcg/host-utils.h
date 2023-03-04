/*
 * Utility compute operations used by translated code.
 *
 * Copyright (c) 2007 Thiemo Seufer
 * Copyright (c) 2007 Jocelyn Mayer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef __HOST_UTILS_H__
#define __HOST_UTILS_H__

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifndef glue
#define xglue(x, y)   x ## y
#define glue(x, y)    xglue(x, y)
#define stringify(s)  tostring(s)
#define tostring(s)   #s
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#if defined(TCG_TARGET_I386) && HOST_LONG_BITS == 32
#define REGPARM __attribute((regparm(3)))
#else
#define REGPARM
#endif

// Define the '__has_builtin' macro if the compiler doesn't support it.
// GCCs older than v10 don't have the macro but support all the builtins used.
#ifndef __has_builtin
  #ifdef __GNUC__
    #define __has_builtin(x) 1
  #else
    #define __has_builtin(x) 0
  #endif
#endif

#if defined(TCG_TARGET_I386) && HOST_LONG_BITS == 64
#define __HAVE_FAST_MULU64__
static inline void mulu64(uint64_t *plow, uint64_t *phigh, uint64_t a, uint64_t b)
{
    __asm__ ("mul %0\n\t"
             : "=d" (*phigh), "=a" (*plow)
             : "a" (a), "0" (b));
}
#define __HAVE_FAST_MULS64__
static inline void muls64(uint64_t *plow, uint64_t *phigh, int64_t a, int64_t b)
{
    __asm__ ("imul %0\n\t"
             : "=d" (*phigh), "=a" (*plow)
             : "a" (a), "0" (b));
}
#else
void muls64(uint64_t *plow, uint64_t *phigh, int64_t a, int64_t b);
void mulu64(uint64_t *plow, uint64_t *phigh, uint64_t a, uint64_t b);
#endif

/* Binary search for leading zeros.  */

static inline int clz32(uint32_t val)
{
    if(!val)
    {
        return 32;
    }

#if __has_builtin(__builtin_clz)
    return __builtin_clz(val);
#else
    int cnt = 0;

    if (!(val & 0xFFFF0000U)) {
        cnt += 16;
        val <<= 16;
    }
    if (!(val & 0xFF000000U)) {
        cnt += 8;
        val <<= 8;
    }
    if (!(val & 0xF0000000U)) {
        cnt += 4;
        val <<= 4;
    }
    if (!(val & 0xC0000000U)) {
        cnt += 2;
        val <<= 2;
    }
    if (!(val & 0x80000000U)) {
        cnt++;
        val <<= 1;
    }
    if (!(val & 0x80000000U)) {
        cnt++;
    }
    return cnt;
#endif
}

static inline int clz64(uint64_t val)
{
    if(!val)
    {
        return 64;
    }

#if __has_builtin(__builtin_clzll)
    return __builtin_clzll(val);
#else
    int cnt = 0;

    if (!(val >> 32)) {
        cnt += 32;
    } else {
        val >>= 32;
    }

    return cnt + clz32(val);
#endif
}

static inline int ctz32(uint32_t val)
{
    if(!val)
    {
        return 32;
    }

#if __has_builtin(__builtin_ctz)
    return __builtin_ctz(val);
#else
    int cnt;

    cnt = 0;
    if (!(val & 0x0000FFFFUL)) {
        cnt += 16;
        val >>= 16;
    }
    if (!(val & 0x000000FFUL)) {
        cnt += 8;
        val >>= 8;
    }
    if (!(val & 0x0000000FUL)) {
        cnt += 4;
        val >>= 4;
    }
    if (!(val & 0x00000003UL)) {
        cnt += 2;
        val >>= 2;
    }
    if (!(val & 0x00000001UL)) {
        cnt++;
        val >>= 1;
    }
    if (!(val & 0x00000001UL)) {
        cnt++;
    }

    return cnt;
#endif
}

static inline int ctz64(uint64_t val)
{
    if(!val)
    {
        return 64;
    }

#if __has_builtin(__builtin_ctzll)
    return __builtin_ctzll(val);
#else
    int cnt;

    cnt = 0;
    if (!((uint32_t)val)) {
        cnt += 32;
        val >>= 32;
    }

    return cnt + ctz32(val);
#endif
}

static inline int ctpop64(uint64_t val)
{
#if __has_builtin(__builtin_popcountll)
    return __builtin_popcountll(val);
#else
    val = (val & 0x5555555555555555ULL) + ((val >>  1) & 0x5555555555555555ULL);
    val = (val & 0x3333333333333333ULL) + ((val >>  2) & 0x3333333333333333ULL);
    val = (val & 0x0f0f0f0f0f0f0f0fULL) + ((val >>  4) & 0x0f0f0f0f0f0f0f0fULL);
    val = (val & 0x00ff00ff00ff00ffULL) + ((val >>  8) & 0x00ff00ff00ff00ffULL);
    val = (val & 0x0000ffff0000ffffULL) + ((val >> 16) & 0x0000ffff0000ffffULL);
    val = (val & 0x00000000ffffffffULL) + ((val >> 32) & 0x00000000ffffffffULL);

    return val;
#endif
}

/**
 * clrsb32 - count leading redundant sign bits in a 32-bit value.
 * @val: The value to search
 *
 * Returns the number of bits following the sign bit that are equal to it.
 * No special cases; output range is [0-31].
 */
static inline int clrsb32(uint32_t val)
{
#if __has_builtin(__builtin_clrsb)
    return __builtin_clrsb(val);
#else
    return clz32(val ^ ((int32_t)val >> 1)) - 1;
#endif
}

#endif
