//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TYPES_RIPPLECRYPTOIDENTIFIER_H_INCLUDED
#define RIPPLE_TYPES_RIPPLECRYPTOIDENTIFIER_H_INCLUDED

#include "beast/beast/FixedArray.h"
#include "beast/beast/intrusive/IntrusiveArray.h"
#include "beast/beast/crypto/Sha256.h"

#include "Base58.h"

#include "CryptoIdentifierStorage.h"

namespace ripple {

/** Shared CryptoIdentifierType traits for Ripple crypto identifiers.

    @tparam Size The number of bytes in the identifier, exclusive of version,
                 checksum, or padding.
    
    @tparam Token A byte prepended to the binary data that distinguishes
                  the type of identifier.

    @tparam Checked A `bool` indicating whether or not the string
                    representation includes an appended a four byte checksum on
                    the data including the Token.
*/
template <std::size_t Size, uint8 Token, bool Checked>
class RippleCryptoIdentifier
{
public:
    typedef std::size_t         size_type;

    // 1 token byte,
    static std::size_t const    pre_size = 1;
    static size_type const      size = Size;
    // 4 checksum bytes (optional)
    static std::size_t const    post_size = (Checked ? 4 : 0);
    static uint8 const          token = Token;
    static bool const           checked = Checked;

    // This is what the wrapper creates, it includes the padding.
    typedef CryptoIdentifierStorage <
        pre_size, size, post_size>          value_type;

    typedef typename value_type::hasher     hasher;
    typedef typename value_type::equal      equal;

    /** Initialize from an input sequence. */
    static void construct (
        uint8 const* begin, uint8 const* end,
            value_type& value)
    {
        value.storage()[0] = Token;
        bassert (std::distance (begin, end) == size);
        std::copy (begin, end, value.begin());
        if (Checked)
        {
            Sha256::digest_type digest;
            Sha256::hash (Sha256::hash (value.storage().cbegin(),
                value.storage().cend() - post_size), digest);
            // We use the first 4 bytes as a checksum
            std::copy (digest.begin(), digest.begin() + 4,
                value.end());
        }
    }

    /** Base class for CryptoIdentifierType. */
    class base
    {
    public:
        template <class UnsignedIntegralType>
        static value_type createFromInteger (UnsignedIntegralType i)
        {
            static_bassert (size >= sizeof (UnsignedIntegralType));
            FixedArray <uint8, size> data;
            data.fill (0);
            i = toNetworkByteOrder <UnsignedIntegralType> (i);
            std::memcpy (data.end () - sizeof (i), &i, std::min (size, sizeof (i)));
            value_type value;
            construct (data.begin(), data.end(), value);
            return value;
        }
    };

    /** Convert to std::string. */
    static std::string to_string (value_type const& value)
    {
        typename value_type::storage_type const& storage (value.storage());
        // We will convert to little endian with an extra pad byte
        FixedArray <uint8, value_type::storage_size + 1> le;
        std::reverse_copy (storage.begin(), storage.end(), le.begin());
        // Set pad byte zero to make BIGNUM always positive
        le.back() = 0;
        return Base58::raw_encode (le.begin(), le.end(),
            Base58::getRippleAlphabet(), Checked);
    }
};

}

#endif
