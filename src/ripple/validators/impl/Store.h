//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORS_STORE_H_INCLUDED
#define RIPPLE_VALIDATORS_STORE_H_INCLUDED

namespace ripple {
namespace Validators {

/** Abstract persistence for Validators data. */
class Store
{
public:
    virtual ~Store () { }

    /** Insert a new SourceDesc to the Store.
        The caller's SourceDesc will have any available persistent
        information filled in from the Store.
    */
    virtual void insert (SourceDesc& desc) = 0;
    
    /** Update the SourceDesc fixed fields.  */
    virtual void update (SourceDesc& desc, bool updateFetchResults = false) = 0;

protected:
    Store () { }
};

}
}

#endif
