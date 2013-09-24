//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
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
