// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

// This is an abridged version of the minipal utf8.cpp implementation.
// It only contains the code necessary for the minipal_convert_utf16_to_utf8
// function, which is called as an intrinsic by the JIT.

#include "../jitpch.h"

#include <minipal/utf8.h>

#include <limits.h>
#include <string.h>

#define HIGH_SURROGATE_START 0xd800
#define HIGH_SURROGATE_END   0xdbff
#define LOW_SURROGATE_START  0xdc00
#define LOW_SURROGATE_END    0xdfff

// Test if the wide character is a high surrogate
static bool IsHighSurrogate(const CHAR16_T c)
{
    return (c & 0xFC00) == HIGH_SURROGATE_START;
}

// Test if the wide character is a low surrogate
static bool IsLowSurrogate(const CHAR16_T c)
{
    return (c & 0xFC00) == LOW_SURROGATE_START;
}

typedef struct
{
    // Store our default string
    unsigned char* byteStart;
    CHAR16_T*      charEnd;
    const CHAR16_T strDefault[2];
    int            strDefaultLength;
    int            fallbackCount;
    int            fallbackIndex;
} DecoderBuffer;

typedef struct
{
    const CHAR16_T strDefault[3];
    int            strDefaultLength;
    CHAR16_T*      charStart;
    CHAR16_T*      charEnd;
    bool           setEncoder;
    bool           bUsedEncoder;
    bool           bFallingBack;
    int            iRecursionCount;
    int            fallbackCount;
    int            fallbackIndex;
} EncoderBuffer;

#define MAX_RECURSION 250

// Set the above values
// This can't be part of the constructor because EncoderFallbacks would have to know how to implement these.
static void EncoderReplacementFallbackBuffer_InternalInitialize(EncoderBuffer* self,
                                                                CHAR16_T*      charStart,
                                                                CHAR16_T*      charEnd,
                                                                bool           setEncoder)
{
    self->charStart       = charStart;
    self->charEnd         = charEnd;
    self->setEncoder      = setEncoder;
    self->bUsedEncoder    = false;
    self->bFallingBack    = false;
    self->iRecursionCount = 0;
}

static CHAR16_T EncoderReplacementFallbackBuffer_InternalGetNextChar(EncoderBuffer* self)
{
    // We want it to get < 0 because == 0 means that the current/last character is a fallback
    // and we need to detect recursion.  We could have a flag but we already have this counter.
    self->fallbackCount--;
    self->fallbackIndex++;

    // Do we have anything left? 0 is now last fallback char, negative is nothing left
    if (self->fallbackCount < 0)
        return '\0';

    // Need to get it out of the buffer.
    // Make sure it didn't wrap from the fast count-- path
    if (self->fallbackCount == INT_MAX)
    {
        self->fallbackCount = -1;
        return '\0';
    }

    // Now make sure its in the expected range
    assert(self->fallbackIndex < self->strDefaultLength && self->fallbackIndex >= 0);

    CHAR16_T ch        = self->strDefault[self->fallbackIndex];
    self->bFallingBack = (ch != 0);
    if (ch == 0)
        self->iRecursionCount = 0;
    return ch;
}

// Fallback Methods
static bool EncoderReplacementFallbackBuffer_Fallback(EncoderBuffer* self)
{
    // If we had a buffer already we're being recursive, throw, it's probably at the suspect
    // character in our array.
    assert(self->fallbackCount < 1);

    // Go ahead and get our fallback
    // Divide by 2 because we aren't a surrogate pair
    self->fallbackCount = self->strDefaultLength / 2;
    self->fallbackIndex = -1;

    return self->fallbackCount != 0;
}

static bool EncoderReplacementFallbackBuffer_Fallback_Unknown(EncoderBuffer* self)
{
    // If we had a buffer already we're being recursive, throw, it's probably at the suspect
    // character in our array.
    assert(self->fallbackCount < 1);

    // Go ahead and get our fallback
    self->fallbackCount = self->strDefaultLength;
    self->fallbackIndex = -1;

    return self->fallbackCount != 0;
}

// Fallback the current character using the remaining buffer and encoder if necessary
// This can only be called by our encodings (other have to use the public fallback methods), so
// we can use our EncoderNLS here too.
// setEncoder is true if we're calling from a GetBytes method, false if we're calling from a GetByteCount
//
// Note that this could also change the contents of self->buffer.encoder, which is the same
// object that the caller is using, so the caller could mess up the encoder for us
// if they aren't careful.
static bool EncoderReplacementFallbackBuffer_InternalFallback(EncoderBuffer* self, CHAR16_T ch, CHAR16_T** chars)
{
    // Shouldn't have null charStart
    assert(self->charStart != NULL);

    // See if it was a high surrogate
    if (IsHighSurrogate(ch))
    {
        // See if there's a low surrogate to go with it
        if (*chars >= self->charEnd)
        {
            // Nothing left in input buffer
            // No input, return 0
        }
        else
        {
            // Might have a low surrogate
            CHAR16_T cNext = **chars;
            if (IsLowSurrogate(cNext))
            {
                // If already falling back then fail
                assert(!self->bFallingBack || self->iRecursionCount++ <= MAX_RECURSION);

                // Next is a surrogate, add it as surrogate pair, and increment chars
                (*chars)++;
                self->bFallingBack = EncoderReplacementFallbackBuffer_Fallback_Unknown(self);
                return self->bFallingBack;
            }

            // Next isn't a low surrogate, just fallback the high surrogate
        }
    }

    // If already falling back then fail
    assert(!self->bFallingBack || self->iRecursionCount++ <= MAX_RECURSION);

    // Fall back our char
    self->bFallingBack = EncoderReplacementFallbackBuffer_Fallback(self);

    return self->bFallingBack;
}

static bool EncoderReplacementFallbackBuffer_MovePrevious(EncoderBuffer* self)
{
    // Back up one, only if we just processed the last character (or earlier)
    if (self->fallbackCount >= -1 && self->fallbackIndex >= 0)
    {
        self->fallbackIndex--;
        self->fallbackCount++;
        return true;
    }

    // Return false 'cause we couldn't do it.
    return false;
}

typedef struct
{
    union
    {
        DecoderBuffer decoder;
        EncoderBuffer encoder;
    } buffer;

    bool useFallback;

#if BIGENDIAN
    bool treatAsLE;
#endif
} UTF8Encoding;

// These are bitmasks used to maintain the state in the decoder. They occupy the higher bits
// while the actual character is being built in the lower bits. They are shifted together
// with the actual bits of the character.

// bits 30 & 31 are used for pending bits fixup
#define FinalByte        (1 << 29)
#define SupplimentarySeq (1 << 28)
#define ThreeByteSeq     (1 << 27)

static bool InRange(int c, int begin, int end)
{
    return begin <= c && c <= end;
}

#define ENSURE_BUFFER_INC                                                                                              \
    pTarget++;                                                                                                         \
    if (pTarget > pAllocatedBufferEnd)                                                                                 \
    {                                                                                                                  \
        *errnoResult = MINIPAL_ERROR_INSUFFICIENT_BUFFER;                                                                     \
        return 0;                                                                                                      \
    }

static size_t GetBytes(UTF8Encoding* self, CHAR16_T* chars, size_t charCount, unsigned char* bytes, size_t byteCount, int* errnoResult)
{
    assert(chars != NULL);
    assert(byteCount >= 0);
    assert(charCount >= 0);
    assert(bytes != NULL);

    // For fallback we may need a fallback buffer.
    // We wait to initialize it though in case we don't have any broken input unicode
    bool           fallbackUsed = false;
    CHAR16_T*      pSrc         = chars;
    unsigned char* pTarget      = bytes;

    CHAR16_T*      pEnd                = pSrc + charCount;
    unsigned char* pAllocatedBufferEnd = pTarget + byteCount;

    int ch = 0;
    int chd;

    // assume that JIT will enregister pSrc, pTarget and ch

    while (true)
    {
        // SLOWLOOP: does all range checks, handles all special cases, but it is slow

        if (pSrc >= pEnd)
        {
            if (ch == 0)
            {
                // Check if there's anything left to get out of the fallback buffer
                ch = fallbackUsed ? EncoderReplacementFallbackBuffer_InternalGetNextChar(&self->buffer.encoder) : 0;
                if (ch > 0)
                    goto ProcessChar;
            }
            else
            {
                // Case of leftover surrogates in the fallback buffer
                if (fallbackUsed && self->buffer.encoder.bFallingBack)
                {
                    assert(ch >= 0xD800 && ch <= 0xDBFF);

                    int cha = ch;

                    ch = EncoderReplacementFallbackBuffer_InternalGetNextChar(&self->buffer.encoder);

                    if (InRange(ch, LOW_SURROGATE_START, LOW_SURROGATE_END))
                    {
                        ch = ch + (cha << 10) + (0x10000 - LOW_SURROGATE_START - (HIGH_SURROGATE_START << 10));
                        goto EncodeChar;
                    }
                    else if (ch > 0)
                    {
                        goto ProcessChar;
                    }

                    break;
                }
            }

            // attempt to encode the partial surrogate (will fail or ignore)
            if (ch > 0)
                goto EncodeChar;

            // We're done
            break;
        }

        if (ch > 0)
        {
            // We have a high surrogate left over from a previous loop.
            assert(ch >= 0xD800 && ch <= 0xDBFF);

            // use separate helper variables for local contexts so that the jit optimizations
            // won't get confused about the variable lifetimes
            int cha = *pSrc;

            // In previous byte, we encountered a high surrogate, so we are expecting a low surrogate here.
            if (InRange(cha, LOW_SURROGATE_START, LOW_SURROGATE_END))
            {
                ch = cha + (ch << 10) + (0x10000 - LOW_SURROGATE_START - (HIGH_SURROGATE_START << 10));

                pSrc++;
            }
            // else ch is still high surrogate and encoding will fail

            // attempt to encode the surrogate or partial surrogate
            goto EncodeChar;
        }

        // If we've used a fallback, then we have to check for it
        if (fallbackUsed)
        {
            ch = EncoderReplacementFallbackBuffer_InternalGetNextChar(&self->buffer.encoder);
            if (ch > 0)
                goto ProcessChar;
        }

        // read next char. The JIT optimization seems to be getting confused when
        // compiling "ch = *pSrc++;", so rather use "ch = *pSrc; pSrc++;" instead
        ch = *pSrc;
        pSrc++;

    ProcessChar:
        if (InRange(ch, HIGH_SURROGATE_START, HIGH_SURROGATE_END))
            continue;

        // either good char or partial surrogate

    EncodeChar:
        // throw exception on partial surrogate if necessary
        if (InRange(ch, HIGH_SURROGATE_START, LOW_SURROGATE_END))
        {
            // Lone surrogates aren't allowed, we have to do fallback for them
            // Have to make a fallback buffer if we don't have one
            if (!fallbackUsed)
            {
                // wait on fallbacks if we can
                // For fallback we may need a fallback buffer
                fallbackUsed = true;

                // Set our internal fallback interesting things.
                EncoderReplacementFallbackBuffer_InternalInitialize(&self->buffer.encoder, chars, pEnd, true);
            }

            // Do our fallback.  Actually we already know its a mixed up surrogate,
            // so the ref pSrc isn't gonna do anything.
            EncoderReplacementFallbackBuffer_InternalFallback(&self->buffer.encoder, (CHAR16_T)ch, &pSrc);

            // Ignore it if we don't throw
            ch = 0;
            continue;
        }

        // Count bytes needed
        int bytesNeeded = 1;
        if (ch > 0x7F)
        {
            if (ch > 0x7FF)
            {
                if (ch > 0xFFFF)
                {
                    bytesNeeded++; // 4 bytes (surrogate pair)
                }
                bytesNeeded++; // 3 bytes (800-FFFF)
            }
            bytesNeeded++; // 2 bytes (80-7FF)
        }

        if (pTarget > pAllocatedBufferEnd - bytesNeeded)
        {
            // Left over surrogate from last time will cause pSrc == chars, so we'll throw
            if (fallbackUsed && self->buffer.encoder.bFallingBack)
            {
                EncoderReplacementFallbackBuffer_MovePrevious(&self->buffer.encoder); // Didn't use this fallback char
                if (ch > 0xFFFF)
                    EncoderReplacementFallbackBuffer_MovePrevious(&self->buffer.encoder); // Was surrogate, didn't use
                                                                                          // 2nd part either
            }
            else
            {
                pSrc--; // Didn't use this char
                if (ch > 0xFFFF)
                    pSrc--; // Was surrogate, didn't use 2nd part either
            }

            assert(pSrc >= chars || pTarget == bytes);

            if (pTarget == bytes) // Throw if we must
            {
                *errnoResult = MINIPAL_ERROR_INSUFFICIENT_BUFFER;
                return 0;
            }
            ch = 0; // Nothing left over (we backed up to start of pair if supplimentary)
            break;
        }

        if (ch <= 0x7F)
        {
            *pTarget = (unsigned char)ch;
        }
        else
        {
            // use separate helper variables for local contexts so that the jit optimizations
            // won't get confused about the variable lifetimes
            int chb;
            if (ch <= 0x7FF)
            {
                // 2 unsigned char encoding
                chb = (unsigned char)(0xC0 | (ch >> 6));
            }
            else
            {
                if (ch <= 0xFFFF)
                {
                    chb = (unsigned char)(0xE0 | (ch >> 12));
                }
                else
                {
                    *pTarget = (unsigned char)(0xF0 | (ch >> 18));
                    ENSURE_BUFFER_INC

                    chb = 0x80 | ((ch >> 12) & 0x3F);
                }
                *pTarget = (unsigned char)chb;
                ENSURE_BUFFER_INC

                chb = 0x80 | ((ch >> 6) & 0x3F);
            }
            *pTarget = (unsigned char)chb;
            ENSURE_BUFFER_INC

            *pTarget = (unsigned char)0x80 | (ch & 0x3F);
        }

        ENSURE_BUFFER_INC

        // If still have fallback don't do fast loop
        if (fallbackUsed && (ch = EncoderReplacementFallbackBuffer_InternalGetNextChar(&self->buffer.encoder)) != 0)
            goto ProcessChar;

        size_t availableChars = (size_t)(pEnd - pSrc);
        size_t availableBytes = (size_t)(pAllocatedBufferEnd - pTarget);

        // don't fall into the fast decoding loop if we don't have enough characters
        // Note that if we don't have enough bytes, pStop will prevent us from entering the fast loop.
        if (availableChars <= 13)
        {
            // we are hoping for 1 unsigned char per char
            if (availableBytes < availableChars)
            {
                // not enough output room.  no pending bits at this point
                ch = 0;
                continue;
            }

            // try to get over the remainder of the ascii characters fast though
            CHAR16_T* pLocalEnd = pEnd; // hint to get pLocalEnd enregistered
            while (pSrc < pLocalEnd)
            {
                ch = *pSrc;
                pSrc++;

                // Not ASCII, need more than 1 unsigned char per char
                if (ch > 0x7F)
                    goto ProcessChar;

                *pTarget = (unsigned char)ch;
                ENSURE_BUFFER_INC
            }
            // we are done, let ch be 0 to clear encoder
            ch = 0;
            break;
        }

        // we need at least 1 unsigned char per character, but Convert might allow us to convert
        // only part of the input, so try as much as we can.  Reduce charCount if necessary
        if (availableBytes < availableChars)
        {
            availableChars = availableBytes;
        }

        // FASTLOOP:
        // - optimistic range checks
        // - fallbacks to the slow loop for all special cases, exception throwing, etc.

        // To compute the upper bound, assume that all characters are ASCII characters at this point,
        //  the boundary will be decreased for every non-ASCII character we encounter
        // Also, we need 5 chars reserve for the unrolled ansi decoding loop and for decoding of surrogates
        // If there aren't enough bytes for the output, then pStop will be <= pSrc and will bypass the loop.
        CHAR16_T* pStop = pSrc + availableChars - 5;

        while (pSrc < pStop)
        {
            ch = *pSrc;
            pSrc++;

            if (ch > 0x7F)
                goto LongCode;

            *pTarget = (unsigned char)ch;
            ENSURE_BUFFER_INC

            // get pSrc aligned
            if (((size_t)pSrc & 0x2) != 0)
            {
                ch = *pSrc;
                pSrc++;
                if (ch > 0x7F)
                    goto LongCode;

                *pTarget = (unsigned char)ch;
                ENSURE_BUFFER_INC
            }

            // Run 4 characters at a time!
            while (pSrc < pStop)
            {
                ch      = *(int*)pSrc;
                int chc = *(int*)(pSrc + 2);

                if (((ch | chc) & (int)0xFF80FF80) != 0)
                    goto LongCodeWithMask;

                if (pTarget + 4 > pAllocatedBufferEnd)
                {
                    *errnoResult = MINIPAL_ERROR_INSUFFICIENT_BUFFER;
                    return 0;
                }

                // Unfortunately, this is endianness sensitive
#if BIGENDIAN
                if (!self->treatAsLE)
                {
                    *pTarget       = (unsigned char)(ch >> 16);
                    *(pTarget + 1) = (unsigned char)ch;
                    pSrc += 4;
                    *(pTarget + 2) = (unsigned char)(chc >> 16);
                    *(pTarget + 3) = (unsigned char)chc;
                    pTarget += 4;
                }
                else
#endif
                {
                    *pTarget       = (unsigned char)ch;
                    *(pTarget + 1) = (unsigned char)(ch >> 16);
                    pSrc += 4;
                    *(pTarget + 2) = (unsigned char)chc;
                    *(pTarget + 3) = (unsigned char)(chc >> 16);
                    pTarget += 4;
                }
            }
            continue;

        LongCodeWithMask:
#if BIGENDIAN
            // be careful about the sign extension
            if (!self->treatAsLE)
                ch = (int)(((unsigned int)ch) >> 16);
            else
#endif
                ch = (CHAR16_T)ch;
            pSrc++;

            if (ch > 0x7F)
                goto LongCode;

            *pTarget = (unsigned char)ch;
            ENSURE_BUFFER_INC
            continue;

        LongCode:
            // use separate helper variables for slow and fast loop so that the jit optimizations
            // won't get confused about the variable lifetimes
            if (ch <= 0x7FF)
            {
                // 2 unsigned char encoding
                chd = 0xC0 | (ch >> 6);
            }
            else
            {
                if (!InRange(ch, HIGH_SURROGATE_START, LOW_SURROGATE_END))
                {
                    // 3 unsigned char encoding
                    chd = 0xE0 | (ch >> 12);
                }
                else
                {
                    // 4 unsigned char encoding - high surrogate + low surrogate
                    if (ch > HIGH_SURROGATE_END)
                    {
                        // low without high -> bad, try again in slow loop
                        pSrc -= 1;
                        break;
                    }

                    chd = *pSrc;
                    pSrc++;

                    if (!InRange(chd, LOW_SURROGATE_START, LOW_SURROGATE_END))
                    {
                        // high not followed by low -> bad, try again in slow loop
                        pSrc -= 2;
                        break;
                    }

                    ch = chd + (ch << 10) + (0x10000 - LOW_SURROGATE_START - (HIGH_SURROGATE_START << 10));

                    *pTarget = (unsigned char)(0xF0 | (ch >> 18));
                    // pStop - this unsigned char is compensated by the second surrogate character
                    // 2 input chars require 4 output bytes.  2 have been anticipated already
                    // and 2 more will be accounted for by the 2 pStop-- calls below.
                    ENSURE_BUFFER_INC

                    chd = 0x80 | ((ch >> 12) & 0x3F);
                }
                *pTarget = (unsigned char)chd;
                pStop--; // 3 unsigned char sequence for 1 char, so need pStop-- and the one below too.
                ENSURE_BUFFER_INC

                chd = 0x80 | ((ch >> 6) & 0x3F);
            }
            *pTarget = (unsigned char)chd;
            pStop--; // 2 unsigned char sequence for 1 char so need pStop--.
            ENSURE_BUFFER_INC

            *pTarget = (unsigned char)(0x80 | (ch & 0x3F));
            // pStop - this unsigned char is already included
            ENSURE_BUFFER_INC
        }

        assert(pTarget <= pAllocatedBufferEnd);

        // no pending char at this point
        ch = 0;
    }

    if (pSrc < pEnd)
    {
        *errnoResult = MINIPAL_ERROR_INSUFFICIENT_BUFFER;
        return 0;
    }

    return (size_t)(pTarget - bytes);
}

size_t minipal_convert_utf16_to_utf8(
    const CHAR16_T* source,
    size_t sourceLength,
    char* destination,
    size_t destinationLength,
    unsigned int flags
)
{
    size_t ret;
    int errnoResult = 0;

    if (sourceLength == 0)
        return 0;

    UTF8Encoding enc = {// repeat replacement char (0xFFFD) twice for a surrogate pair
                        .buffer      = {.encoder = {
                                                    .strDefault       = {0xFFFD, 0xFFFD, 0},
                                                    .strDefaultLength = 2,
                                                    .fallbackCount    = -1,
                                                    .fallbackIndex    = -1
                                                    }},
                        .useFallback = true,
#if BIGENDIAN
                        .treatAsLE = (flags & MINIPAL_TREAT_AS_LITTLE_ENDIAN)
#endif
    };

#if !BIGENDIAN
    (void)flags; // unused
#endif

    ret = GetBytes(&enc, (CHAR16_T*)source, sourceLength, (unsigned char*)destination, destinationLength, &errnoResult);
    if (errnoResult)
        ret = 0;

    return ret;
}
