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

#ifndef RIPPLE_TYPES_CRYPTOIDENTIFIER_H_INCLUDED
#define RIPPLE_TYPES_CRYPTOIDENTIFIER_H_INCLUDED

#include <beast/ByteOrder.h>
#include <beast/crypto/Sha256.h>
#include <array>

#include <ripple/types/api/Base58.h>

#include <ripple/types/api/IdentifierStorage.h>

namespace ripple {

/** Shared IdentifierType traits for Ripple crypto identifiers.

    @tparam Size The number of bytes in the identifier, exclusive of version,
                 checksum, or padding.
    
    @tparam Token A byte prepended to the binary data that distinguishes
                  the type of identifier.

    @tparam Checked A `bool` indicating whether or not the string
                    representation includes an appended a four byte checksum on
                    the data including the Token.
*/
template <std::size_t Size, std::uint8_t Token, bool Checked>
class CryptoIdentifier
{
public:
    typedef std::size_t         size_type;

    // 1 token byte,
    static std::size_t const    pre_size = 1;
    static size_type const      size = Size;
    // 4 checksum bytes (optional)
    static std::size_t const    post_size = (Checked ? 4 : 0);
    static std::uint8_t const   token = Token;
    static bool const           checked = Checked;

    // This is what the wrapper creates, it includes the padding.
    typedef IdentifierStorage <
        pre_size, size, post_size>          value_type;

    typedef typename value_type::hasher     hasher;
    typedef typename value_type::key_equal  key_equal;

    /** Initialize from an input sequence. */
    static void construct (
        std::uint8_t const* begin, std::uint8_t const* end,
            value_type& value)
    {
        value.storage()[0] = Token;
        bassert (std::distance (begin, end) == size);
        std::copy (begin, end, value.begin());
        if (Checked)
        {
            beast::Sha256::digest_type digest;
            auto const& vs = value.storage();
            beast::Sha256::hash (beast::Sha256::hash (vs.data(),
                                                      vs.data() + (vs.size() - post_size)),
                                 digest);
            // We use the first 4 bytes as a checksum
            std::copy (digest.begin(), digest.begin() + 4,
                value.end());
        }
    }

    /** Base class for IdentifierType. */
    class base
    {
    public:
        template <class UnsignedIntegralType>
        static value_type createFromInteger (UnsignedIntegralType i)
        {
            static_bassert (size >= sizeof (UnsignedIntegralType));
            std::array <std::uint8_t, size> data;
            data.fill (0);
            i = beast::toNetworkByteOrder <UnsignedIntegralType> (i);
            std::memcpy (data.data () + (data.size() - sizeof (i)), &i, std::min (size, sizeof (i)));
            value_type value;
            construct (data.data(), data.data() + data.size(), value);
            return value;
        }
    };

    /** Convert to std::string. */
    static std::string to_string (value_type const& value)
    {
        typename value_type::storage_type const& storage (value.storage());
        // We will convert to little endian with an extra pad byte
        std::array <std::uint8_t, value_type::storage_size + 1> le;
        std::reverse_copy (storage.begin(), storage.end(), le.begin());
        // Set pad byte zero to make BIGNUM always positive
        le.back() = 0;
        return Base58::raw_encode (le.data(), le.data() + le.size(),
            Base58::getRippleAlphabet(), Checked);
    }

    /** Convert from std::string. */
    static std::pair <value_type, bool> from_string (std::string const& s)
    {
        value_type value;
        bool success (! s.empty());
        if (success && !Base58::raw_decode (&s.front(), &s.back()+1,
            value.storage().data(), value_type::storage_size, Checked,
                Base58::getRippleAlphabet()))
            success = false;
        if (success && value.storage()[0] != Token)
            success = false;
        return std::make_pair (value, success);
    }
};

template <std::size_t Size, std::uint8_t Token, bool Checked> 
    typename CryptoIdentifier <Size, Token, Checked>::size_type
    const CryptoIdentifier <Size, Token, Checked>::size;
}

#endif
