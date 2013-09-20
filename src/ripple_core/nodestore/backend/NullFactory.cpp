//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace NodeStore
{

class NullFactory::BackendImp : public Backend
{
public:
    explicit BackendImp (Scheduler& scheduler)
        : m_scheduler (scheduler)
    {
    }

    ~BackendImp ()
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
    
    void storeBatch (Batch const& batch)
    {
    }

    void visitAll (VisitCallback& callback)
    {
    }

    int getWriteLoad ()
    {
        return 0;
    }

private:
    Scheduler& m_scheduler;
};

//------------------------------------------------------------------------------

NullFactory::NullFactory ()
{
}

NullFactory::~NullFactory ()
{
}

NullFactory* NullFactory::getInstance ()
{
    return new NullFactory;
}

String NullFactory::getName () const
{
    return "none";
}

Backend* NullFactory::createInstance (
    size_t,
    Parameters const&,
    Scheduler& scheduler)
{
    return new NullFactory::BackendImp (scheduler);
}

}
