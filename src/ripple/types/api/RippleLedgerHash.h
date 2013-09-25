//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TYPES_RIPPLELEDGERHASH_H_INCLUDED
#define RIPPLE_TYPES_RIPPLELEDGERHASH_H_INCLUDED

namespace ripple {

/*
    Hashes are used to uniquely identify objects like
    transactions, peers, validators, and accounts.

    For historical reasons, some hashes are 256 bits and some are 160.

    David:
        "The theory is that you may need to communicate public keys
         to others, so having them be shorter is a good idea. plus,
         you can't arbitrarily tweak them because you wouldn't know
         the corresponding private key anyway. So the security
         requirements aren't as great."
*/
/** The SHA256 bit hash of a signed ledger. */
class RippleLedgerHashTraits : public SimpleIdentifier <32>
{
public:
};

/** A ledger hash. */
typedef IdentifierType <RippleLedgerHashTraits> RippleLedgerHash;

// Legacy
typedef uint256 LedgerHash;

}

#endif
