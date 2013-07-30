//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class MemoryBackendFactory::Backend : public NodeStore::Backend
{
private:
    typedef std::map <uint256 const, NodeObject::pointer> Map;

public:
    Backend (size_t keyBytes, StringPairArray const& keyValues)
        : m_keyBytes (keyBytes)
    {
    }

    ~Backend ()
    {
    }

    std::string getDataBaseName ()
    {
        return "memory";
    }

    //--------------------------------------------------------------------------

    NodeObject::pointer retrieve (uint256 const &hash)
    {
        Map::iterator iter = m_map.find (hash);

        if (iter != m_map.end ())
            return iter->second;

        return NodeObject::pointer ();
    }

    bool store (NodeObject::ref object)
    {
        Map::iterator iter = m_map.find (object->getHash ());

        if (iter == m_map.end ())
        {
            m_map.insert (std::make_pair (object->getHash (), object));
        }

        return true;
    }

    bool bulkStore (const std::vector< NodeObject::pointer >& batch)
    {
        for (int i = 0; i < batch.size (); ++i)
            store (batch [i]);

        return true;
    }

    void visitAll (FUNCTION_TYPE <void (NodeObject::pointer)> f)
    {
        for (Map::const_iterator iter = m_map.begin (); iter != m_map.end (); ++iter)
            f (iter->second);
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

MemoryBackendFactory& MemoryBackendFactory::getInstance ()
{
    static MemoryBackendFactory instance;

    return instance;
}

String MemoryBackendFactory::getName () const
{
    return "Memory";
}

NodeStore::Backend* MemoryBackendFactory::createInstance (StringPairArray const& keyValues)
{
    return new MemoryBackendFactory::Backend (32, keyValues);
}

//------------------------------------------------------------------------------

