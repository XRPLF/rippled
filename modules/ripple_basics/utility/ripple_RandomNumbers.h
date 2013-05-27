//------------------------------------------------------------------------------
/*
	Copyright (c) 2011-2013, OpenCoin, Inc.

	Permission to use, copy, modify, and/or distribute this software for any
	purpose with  or without fee is hereby granted,  provided that the above
	copyright notice and this permission notice appear in all copies.

	THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
	WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES OF
	MERCHANTABILITY  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
	ANY SPECIAL,  DIRECT, INDIRECT,  OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
	WHATSOEVER  RESULTING  FROM LOSS OF USE, DATA OR PROFITS,  WHETHER IN AN
	ACTION OF CONTRACT, NEGLIGENCE  OR OTHER TORTIOUS ACTION, ARISING OUT OF
	OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_RANDOMNUMBERS_H
#define RIPPLE_RANDOMNUMBERS_H

extern bool AddSystemEntropy ();

// Cryptographically secure random number source

// VFALCO: TODO Clean this up, rename stuff
// Seriously...wtf...rename "num" to bytes, or make it work
// using a template so the destination can be a vector of objects.
//
// VFALCO: Should accept void* not unsigned char*
//
extern void getRand (unsigned char *buf, int num);

inline static void getRand (char *buf, int num)
{
	return getRand (reinterpret_cast<unsigned char *>(buf), num);
}

// VFALCO: TODO Clean this
// "num" is really bytes this should just be called getRandomBytes()
// This function is unnecessary!
//
inline static void getRand (void *buf, int num)
{
	return getRand (reinterpret_cast<unsigned char *>(buf), num);
}

#endif
