//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_FUNCTIONAL_LOADTYPE_H_INCLUDED
#define RIPPLE_CORE_FUNCTIONAL_LOADTYPE_H_INCLUDED

// types of load that can be placed on the server
/** The type of load placed on the server.
*/
/* VFALCO TODO
        - Remove LT_ from each enum
        - Put LoadType into a struct like this:
            (Note this is modeled after boost::system::error_code::err_c)

    struct LoadType
    {
        enum load_c
        {
            invalidRequest,
            //...
        }
    };

    // For parameters
    typedef LoadType::load_c LoadTypeParam;

    // Example of passing a LoadType:
    peer->applyLoadCharge (LoadType::newTransaction);

    // Example function prototype
    void applyLoadCharge (LoadTypeParam loadType);
*/
enum LoadType
{
    // Bad things
    LT_InvalidRequest,          // A request that we can immediately tell is invalid
    LT_RequestNoReply,          // A request that we cannot satisfy
    LT_InvalidSignature,        // An object whose signature we had to check and it failed
    LT_UnwantedData,            // Data we have no use for
    LT_BadPoW,                  // Proof of work not valid
    LT_BadData,                 // Data we have to verify before rejecting

    // RPC loads
    LT_RPCInvalid,              // An RPC request that we can immediately tell is invalid.
    LT_RPCReference,            // A default "reference" unspecified load
    LT_RPCException,            // An RPC load that causes an exception
    LT_RPCBurden,               // A particularly burdensome RPC load

    // Good things
    LT_NewTrusted,              // A new transaction/validation/proposal we trust
    LT_NewTransaction,          // A new, valid transaction
    LT_NeededData,              // Data we requested

    // Requests
    LT_RequestData,             // A request that is hard to satisfy, disk access
    LT_CheapQuery,              // A query that is trivial, cached data

    LT_MAX                      // MUST BE LAST
};

#endif
