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

    std::string getDataBaseName()
    {
        return std::string ();
    }

    bool store (NodeObject::ref obj)
    {
        return false;
    }

    bool bulkStore (const std::vector< NodeObject::pointer >& objs)
    {
        return false;
    }

    NodeObject::pointer retrieve (uint256 const& hash)
    {
        return NodeObject::pointer ();
    }

    void visitAll (FUNCTION_TYPE <void (NodeObject::pointer)> func)
    {
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

NodeStore::Backend* NullBackendFactory::createInstance (StringPairArray const& keyValues)
{
    return new NullBackendFactory::Backend;
}

//------------------------------------------------------------------------------

