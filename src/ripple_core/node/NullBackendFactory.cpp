//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class NullBackendFactory::Backend : public NodeStore::Backend
{
public:
    explicit Backend (NodeStore::Scheduler& scheduler)
        : m_scheduler (scheduler)
    {
    }

    ~Backend ()
    {
    }

    std::string getName()
    {
        return std::string ();
    }

    Status fetch (void const*, NodeObject::Ptr*)
    {
        return notFound;
    }
    
    void store (NodeObject::ref object)
    {
    }
    
    void storeBatch (NodeStore::Batch const& batch)
    {
    }

    void visitAll (VisitCallback& callback)
    {
    }

    int getWriteLoad ()
    {
        return 0;
    }

    void stopAsync ()
    {
        m_scheduler.scheduledTasksStopped ();
    }

private:
    NodeStore::Scheduler& m_scheduler;
};

//------------------------------------------------------------------------------

NullBackendFactory::NullBackendFactory ()
{
}

NullBackendFactory::~NullBackendFactory ()
{
}

NullBackendFactory* NullBackendFactory::getInstance ()
{
    return new NullBackendFactory;
}

String NullBackendFactory::getName () const
{
    return "none";
}

NodeStore::Backend* NullBackendFactory::createInstance (
    size_t,
    StringPairArray const&,
    NodeStore::Scheduler& scheduler)
{
    return new NullBackendFactory::Backend (scheduler);
}

//------------------------------------------------------------------------------

