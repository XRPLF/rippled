//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class NullBackendFactory::Backend : public NodeStore::Backend
{
public:
    Backend ()
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
};

//------------------------------------------------------------------------------

NullBackendFactory::NullBackendFactory ()
{
}

NullBackendFactory::~NullBackendFactory ()
{
}

NullBackendFactory& NullBackendFactory::getInstance ()
{
    static NullBackendFactory instance;

    return instance;
}

String NullBackendFactory::getName () const
{
    return "none";
}

NodeStore::Backend* NullBackendFactory::createInstance (
    size_t,
    StringPairArray const&,
    NodeStore::Scheduler&)
{
    return new NullBackendFactory::Backend;
}

//------------------------------------------------------------------------------

