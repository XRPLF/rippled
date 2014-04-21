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

namespace ripple {
namespace NodeStore {

class MemoryBackend : public Backend
{
public:
    typedef std::map <uint256 const, NodeObject::Ptr> Map;
    beast::Journal m_journal;
    size_t const m_keyBytes;
    Map m_map;
    Scheduler& m_scheduler;

    MemoryBackend (size_t keyBytes, Parameters const& keyValues,
        Scheduler& scheduler, beast::Journal journal)
        : m_journal (journal)
        , m_keyBytes (keyBytes)
        , m_scheduler (scheduler)
    {
    }

    ~MemoryBackend ()
    {
    }

    std::string
    getName ()
    {
        return "memory";
    }

    //--------------------------------------------------------------------------

    Status
    fetch (void const* key, NodeObject::Ptr* pObject)
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

    void
    store (NodeObject::ref object)
    {
        Map::iterator iter = m_map.find (object->getHash ());

        if (iter == m_map.end ())
        {
            m_map.insert (std::make_pair (object->getHash (), object));
        }
    }

    void
    storeBatch (Batch const& batch)
    {
        for (auto const& e : batch)
            store (e);
    }

    void
    for_each (std::function <void(NodeObject::Ptr)> f)
    {
        for (auto const& e : m_map)
            f (e.second);
    }

    int
    getWriteLoad ()
    {
        return 0;
    }
};

//------------------------------------------------------------------------------

class MemoryFactory : public Factory
{
public:
    beast::String
    getName () const
    {
        return "Memory";
    }

    std::unique_ptr <Backend>
    createInstance (
        size_t keyBytes,
        Parameters const& keyValues,
        Scheduler& scheduler,
        beast::Journal journal)
    {
        return std::make_unique <MemoryBackend> (
            keyBytes, keyValues, scheduler, journal);
    }
};

//------------------------------------------------------------------------------

std::unique_ptr <Factory>
make_MemoryFactory ()
{
    return std::make_unique <MemoryFactory> ();
}

}
}
