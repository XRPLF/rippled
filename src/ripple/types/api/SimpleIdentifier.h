//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
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
    static std::size_t const            size = Bytes;

    typedef std::size_t                 size_type;
    typedef base_uint <Bytes*8>         value_type;
    typedef typename value_type::hasher hasher;
    typedef typename value_type::equal  equal;

    /** Initialize from an input sequence. */
    static void construct (
        uint8 const* begin, uint8 const* end,
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
