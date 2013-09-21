//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORS_CHOSENLIST_H_INCLUDED
#define RIPPLE_VALIDATORS_CHOSENLIST_H_INCLUDED

namespace Validators
{

class ChosenList : public SharedObject
{
public:
    typedef SharedPtr <ChosenList> Ptr;

    struct Info
    {
        Info ()
        {
        }
    };

    typedef boost::unordered_map <PublicKey, Info, PublicKey::HashFunction> MapType;

    ChosenList (std::size_t expectedSize = 0)
    {
        // Available only in recent boost versions?
        //m_map.reserve (expectedSize);
    }

    MapType const& map() const
    {
        return m_map;
    }

    std::size_t size () const noexcept
    {
        return m_map.size ();
    }

    void insert (PublicKey const& key, Info const& info) noexcept
    {
        m_map [key] = info;
    }

    bool containsPublicKey (PublicKey const& publicKey) const noexcept
    {
        return m_map.find (publicKey) != m_map.cend ();
    }

    bool containsPublicKeyHash (PublicKeyHash const& publicKeyHash) const noexcept
    {
        return false;
    }

private:
    MapType m_map;
};

}

#endif
