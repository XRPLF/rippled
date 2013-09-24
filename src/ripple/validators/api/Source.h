//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORS_SOURCE_H_INCLUDED
#define RIPPLE_VALIDATORS_SOURCE_H_INCLUDED

namespace ripple {
namespace Validators {

/** A source of validator descriptors. */
class Source
{
public:
    /** A Source's descriptor for a Validator. */
    struct Info
    {
        /** The unique key for this validator. */
        PublicKey publicKey;

        /** Optional human readable comment describing the validator. */
        String label;
    };

    /** Destroy the Source.
        This can be called from any thread. If the Source is busy
        fetching, the destructor must block until the operation is either
        canceled or complete.
    */
    virtual ~Source () { }

    virtual String name () = 0;

    virtual String uniqueID () = 0;

    virtual String createParam () = 0;

    /** Fetch the most recent list from the Source.
        If possible, the Source should periodically poll the
        CancelCallback, and abort the operation if shouldCancel
        returns `true`.
        This call will block.
    */
    struct Result
    {
        Result ();
        void swapWith (Result& other);

        bool success;
        String message;
        Time expirationTime;
        Array <Info> list;
    };
    
    virtual Result fetch (CancelCallback& callback, Journal journal) = 0;
};

}
}

#endif
