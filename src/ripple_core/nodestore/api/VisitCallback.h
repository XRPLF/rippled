//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_VISITCALLBACK_H_INCLUDED
#define RIPPLE_NODESTORE_VISITCALLBACK_H_INCLUDED

namespace NodeStore
{

/** Callback for iterating through objects.

    @see visitAll
*/
struct VisitCallback
{
    virtual void visitObject (NodeObject::Ptr const& object) = 0;
};

}

#endif
