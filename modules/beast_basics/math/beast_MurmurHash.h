/*============================================================================*/
/*
  VFLib: https://github.com/vinniefalco/VFLib

  Copyright (C) 2008 by Vinnie Falco <vinnie.falco@gmail.com>

  This library contains portions of other open source products covered by
  separate licenses. Please see the corresponding source files for specific
  terms.

  VFLib is provided under the terms of The MIT License (MIT):

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.
*/
/*============================================================================*/

#ifndef BEAST_MURMURHASH_BEASTHEADER
#define BEAST_MURMURHASH_BEASTHEADER

// Original source code links in .cpp file

// This file depends on some Juce declarations and defines

namespace Murmur
{

extern void MurmurHash3_x86_32  (const void* key, int len, uint32 seed, void* out);
extern void MurmurHash3_x86_128 (const void* key, int len, uint32 seed, void* out);
extern void MurmurHash3_x64_128 (const void* key, int len, uint32 seed, void* out);

// Uses Juce to choose an appropriate routine

// This handy template deduces which size hash is desired
template <typename HashType>
inline void Hash (const void* key, int len, uint32 seed, HashType* out)
{
    switch (8 * sizeof (HashType))
    {
    case 32:
        MurmurHash3_x86_32 (key, len, seed, out);
        break;

#if BEAST_64BIT

    case 128:
        MurmurHash3_x64_128 (key, len, seed, out);
        break;
#else

    case 128:
        MurmurHash3_x86_128 (key, len, seed, out);
        break;
#endif

    default:
        Throw (std::runtime_error ("invalid key size in MurmurHash"));
        break;
    };
}

}

#endif
