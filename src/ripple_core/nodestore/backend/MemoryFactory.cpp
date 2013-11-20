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

namespace NodeStore
{

class MemoryFactory::BackendImp : public Backend
{
private:
    typedef std::map <uint256 const, NodeObject::Ptr> Map;

public:
    BackendImp (size_t keyBytes, Parameters const& keyValues,
        Scheduler& scheduler)
        : m_keyBytes (keyBytes)
        , m_scheduler (scheduler)
    {
    }

    ~BackendImp ()
    {
    }

    std::string getName ()
    {
        return "memory";
    }

    //--------------------------------------------------------------------------

    Status fetch (void const* key, NodeObject::Ptr* pObject)
    {
        uint256 const hash (uint256::fromVoid (key));

        Map::iterator iter = m_map.find (hash);

        if (iter != m_map.end ())
        {
            *pObject = iter->second;
        }
        else
        {
            pObject->reset ();
        }

        return ok;
    }

    void store (NodeObject::ref object)
    {
        Map::iterator iter = m_map.find (object->getHash ());

        if (iter == m_map.end ())
        {
            m_map.insert (std::make_pair (object->getHash (), object));
        }
    }

    void storeBatch (Batch const& batch)
    {
        for (std::size_t i = 0; i < batch.size (); ++i)
            store (batch [i]);
    }

    void visitAll (VisitCallback& callback)
    {
        for (Map::const_iterator iter = m_map.begin (); iter != m_map.end (); ++iter)
            callback.visitObject (iter->second);
    }

    int getWriteLoad ()
    {
        return 0;
    }

    //--------------------------------------------------------------------------

private:
    size_t const m_keyBytes;

    Map m_map;
    Scheduler& m_scheduler;
};

//------------------------------------------------------------------------------

MemoryFactory::MemoryFactory ()
{
}

MemoryFactory::~MemoryFactory ()
{
}

MemoryFactory* MemoryFactory::getInstance ()
{
    return new MemoryFactory;
}

String MemoryFactory::getName () const
{
    return "Memory";
}

Backend* MemoryFactory::createInstance (
    size_t keyBytes,
    Parameters const& keyValues,
    Scheduler& scheduler)
{
    return new MemoryFactory::BackendImp (keyBytes, keyValues, scheduler);
}

}
