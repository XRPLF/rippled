//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class MemoryBackendFactory::Backend : public NodeStore::Backend
{
private:
    typedef std::map <uint256 const, NodeObject::Ptr> Map;

public:
    Backend (size_t keyBytes, StringPairArray const& keyValues)
        : m_keyBytes (keyBytes)
    {
    }

    ~Backend ()
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

    void storeBatch (NodeStore::Batch const& batch)
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
};

//------------------------------------------------------------------------------

MemoryBackendFactory::MemoryBackendFactory ()
{
}

MemoryBackendFactory::~MemoryBackendFactory ()
{
}

MemoryBackendFactory* MemoryBackendFactory::getInstance ()
{
    return new MemoryBackendFactory;
}

String MemoryBackendFactory::getName () const
{
    return "Memory";
}

NodeStore::Backend* MemoryBackendFactory::createInstance (
    size_t keyBytes,
    StringPairArray const& keyValues,
    NodeStore::Scheduler& scheduler)
{
    return new MemoryBackendFactory::Backend (keyBytes, keyValues);
}

//------------------------------------------------------------------------------

