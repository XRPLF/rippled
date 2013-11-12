//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
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
