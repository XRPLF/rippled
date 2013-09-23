//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_BASICS_RIPPLEPUBLICKEY_H_INCLUDED
#define RIPPLE_BASICS_RIPPLEPUBLICKEY_H_INCLUDED

/** A container used to hold a public key in binary format. */
typedef UnsignedInteger <33> RipplePublicKey;
#if 0
class RipplePublicKey
{
private:
    typedef UnsignedInteger <33> integer_type;

public:
    enum
    {
        size        = integer_type::sizeInBytes
    };

    typedef integer_type::value_type      value_type;
    typedef integer_type::iterator        iterator;
    typedef integer_type::const_iterator  const_iterator;

    class HashFunction
    {
    public:
        HashFunction (HashValue seedToUse = Random::getSystemRandom().nextInt())
            : m_hash (seedToUse)
        {
        }

        HashValue operator() (RipplePublicKey const& value) const
        {
            return m_hash (value);
        }

    private:
        integer_type::HashFunction m_hash;
    };

    iterator begin()
    {
        return m_value.begin();
    }
    
    iterator end()
    {
        return m_value.end();
    }

    const_iterator begin() const
    {
        return m_value.begin();
    }
    
    const_iterator end() const
    {
        return m_value.end();
    }

    const_iterator cbegin() const
    {
        return m_value.cbegin();
    }
    
    const_iterator cend() const
    {
        return m_value.cend();
    }

private:
    integer_type m_value;
};
#endif

#endif
