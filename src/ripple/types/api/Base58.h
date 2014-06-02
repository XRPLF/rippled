//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.
//
// Why base-58 instead of standard base-64 encoding?
// - Don't want 0OIl characters that look the same in some fonts and
//      could be used to create visually identical looking account numbers.
// - A string with non-alphanumeric characters is not as easily accepted as an account number.
// - E-mail usually won't line-break if there's no punctuation to break at.
// - Doubleclicking selects the whole number as one word if it's all alphanumeric.
//
#ifndef RIPPLE_TYPES_BASE58_H
#define RIPPLE_TYPES_BASE58_H

#include <iterator>
#include <type_traits>

#include <ripple/types/api/Blob.h>

namespace ripple {

/** Performs Base 58 encoding and decoding. */
class Base58
{
public:
    class Alphabet
    {
    public:
        // chars may not contain high-ASCII values
        explicit Alphabet (char const* chars)
            : m_chars (chars)
        {
            std::fill (m_inverse, m_inverse + 128, -1);
            int i (0);
            for (char const* c (chars); *c; ++c)
                m_inverse [*c] = i++;
        }

        char const* chars () const
            { return m_chars; }

        char to_char (int digit) const
            { return m_chars [digit]; }

        char operator[] (int digit) const
            { return to_char (digit); }

        int from_char (char c) const
            { return m_inverse [c]; }

    private:
        char const* m_chars;
        int m_inverse [128];
    };

    static Alphabet const& getBitcoinAlphabet ();
    static Alphabet const& getRippleAlphabet ();

    static std::string raw_encode (
        unsigned char const* begin, unsigned char const* end,
            Alphabet const& alphabet, bool withCheck);

    static void fourbyte_hash256 (void* out, void const* in, std::size_t bytes);

    template <class InputIt>
    static std::string encode (InputIt first, InputIt last,
        Alphabet const& alphabet, bool withCheck)
    {
        typedef typename std::iterator_traits<InputIt>::value_type value_type;
        std::vector <typename std::remove_const <value_type>::type> v;
        std::size_t const size (std::distance (first, last));
        if (withCheck)
        {
            v.reserve (size + 1 + 4);
            v.insert (v.begin(), first, last);
            unsigned char hash [4];
            fourbyte_hash256 (hash, &v.front(), v.size());
            v.resize (0);
            // Place the hash reversed in the front
            std::copy (std::reverse_iterator <unsigned char const*> (hash+4),
                       std::reverse_iterator <unsigned char const*> (hash),
                       std::back_inserter (v));
        }
        else
        {
            v.reserve (size + 1);
        }
        // Append input little endian
        std::copy (std::reverse_iterator <InputIt> (last),
                   std::reverse_iterator <InputIt> (first),
                   std::back_inserter (v));
        // Pad zero to make the BIGNUM positive
        v.push_back (0);
        return raw_encode (&v.front(), &v.back()+1, alphabet, withCheck);
    }

    template <class Container>
    static std::string encode (Container const& container)
    {
        return encode (container.container.begin(), container.end(),
            getRippleAlphabet(), false);
    }

    template <class Container>
    static std::string encodeWithCheck (Container const& container)
    {
        return encode (&container.front(), &container.back()+1,
            getRippleAlphabet(), true);
    }

    static std::string encode (const unsigned char* pbegin, const unsigned char* pend)
    {
        return encode (pbegin, pend, getRippleAlphabet(), false);
    }

    //--------------------------------------------------------------------------

    // Raw decoder leaves the check bytes in place if present

    static bool raw_decode (char const* first, char const* last,
        void* dest, std::size_t size, bool checked, Alphabet const& alphabet);

    static bool decode (const char* psz, Blob& vchRet, Alphabet const& alphabet = getRippleAlphabet ());
    static bool decode (const std::string& str, Blob& vchRet);
    static bool decodeWithCheck (const char* psz, Blob& vchRet, Alphabet const& alphabet = getRippleAlphabet());
    static bool decodeWithCheck (const std::string& str, Blob& vchRet, Alphabet const& alphabet = getRippleAlphabet());
};

}

#endif
