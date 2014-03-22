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

#ifndef RIPPLE_TYPES_SIMPLEIDENTIFIER_H_INCLUDED
#define RIPPLE_TYPES_SIMPLEIDENTIFIER_H_INCLUDED

namespace ripple {

/** Provides common traits for non-signing identifiers like ledger hashes.
    The storage is a suitably sized instance of base_uint.
*/
template <std::size_t Bytes>
class SimpleIdentifier
{
public:
    static std::size_t const                size = Bytes;

    typedef std::size_t                     size_type;
    typedef base_uint <Bytes*8>             value_type;
    typedef typename value_type::hasher     hasher;
    typedef typename value_type::key_equal  key_equal;

    /** Initialize from an input sequence. */
    static void construct (
        std::uint8_t const* begin, std::uint8_t const* end,
            value_type& value)
    {
        std::copy (begin, end, value.begin());
    }

    /** Base class for IdentifierType. */
    struct base { };

    /** Convert to std::string. */
    static std::string to_string (value_type const& value)
    {
        return strHex (value.cbegin(), size);
    }

    /** Assignment specializations.
        When Other is the same as value_type, this is a copy assignment.
    */
    template <typename Other>
    struct assign
    {
        void operator() (value_type& value, Other const& other)
        {
            value = other;
        }
    };
};

}

#endif
