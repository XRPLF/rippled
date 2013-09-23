//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORS_TYPES_H_INCLUDED
#define RIPPLE_VALIDATORS_TYPES_H_INCLUDED

namespace ripple {
namespace Validators {

struct ReceivedValidation
{
    RippleLedgerHash        ledgerHash;
    RipplePublicKey         publicKey;
    RipplePublicKeyHash     publicKeyHash;
};

/** Callback used to optionally cancel long running fetch operations. */
struct CancelCallback
{
    virtual bool shouldCancel () = 0;
};

}
}

#endif
