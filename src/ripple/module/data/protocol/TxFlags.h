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

#ifndef RIPPLE_TXFLAGS_H
#define RIPPLE_TXFLAGS_H

namespace ripple {

//
// Transaction flags.
//

/** Transaction flags.

    These flags modify the behavior of an operation.

    @note Changing these will create a hard fork
    @ingroup protocol
*/
class TxFlag
{
public:
    static uint32 const requireDestTag = 0x00010000;
};
// VFALCO TODO Move all flags into this container after some study.

// Universal Transaction flags:
const uint32 tfFullyCanonicalSig    = 0x80000000;
const uint32 tfUniversal            = tfFullyCanonicalSig;
const uint32 tfUniversalMask        = ~ tfUniversal;

// AccountSet flags:
// VFALCO TODO Javadoc comment every one of these constants
//const uint32 TxFlag::requireDestTag       = 0x00010000;
const uint32 tfOptionalDestTag      = 0x00020000;
const uint32 tfRequireAuth          = 0x00040000;
const uint32 tfOptionalAuth         = 0x00080000;
const uint32 tfDisallowXRP          = 0x00100000;
const uint32 tfAllowXRP             = 0x00200000;
const uint32 tfAccountSetMask       = ~ (tfUniversal | TxFlag::requireDestTag | tfOptionalDestTag
                                             | tfRequireAuth | tfOptionalAuth
                                             | tfDisallowXRP | tfAllowXRP);

// AccountSet SetFlag/ClearFlag values
const uint32 asfRequireDest         = 1;
const uint32 asfRequireAuth         = 2;
const uint32 asfDisallowXRP         = 3;
const uint32 asfDisableMaster       = 4;
const uint32 asfAccountTxnID        = 5;

// OfferCreate flags:
const uint32 tfPassive              = 0x00010000;
const uint32 tfImmediateOrCancel    = 0x00020000;
const uint32 tfFillOrKill           = 0x00040000;
const uint32 tfSell                 = 0x00080000;
const uint32 tfOfferCreateMask      = ~ (tfUniversal | tfPassive | tfImmediateOrCancel | tfFillOrKill | tfSell);

// Payment flags:
const uint32 tfNoRippleDirect       = 0x00010000;
const uint32 tfPartialPayment       = 0x00020000;
const uint32 tfLimitQuality         = 0x00040000;
const uint32 tfPaymentMask          = ~ (tfUniversal | tfPartialPayment | tfLimitQuality | tfNoRippleDirect);

// TrustSet flags:
const uint32 tfSetfAuth             = 0x00010000;
const uint32 tfSetNoRipple          = 0x00020000;
const uint32 tfClearNoRipple        = 0x00040000;
const uint32 tfTrustSetMask         = ~ (tfUniversal | tfSetfAuth | tfSetNoRipple | tfClearNoRipple);

} // ripple

#endif
