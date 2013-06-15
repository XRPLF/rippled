#ifndef RIPPLE_TXFLAGS_H
#define RIPPLE_TXFLAGS_H

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

// AccountSet flags:
// VFALCO TODO Javadoc comment every one of these constants
//const uint32 TxFlag::requireDestTag       = 0x00010000;
const uint32 tfOptionalDestTag      = 0x00020000;
const uint32 tfRequireAuth          = 0x00040000;
const uint32 tfOptionalAuth         = 0x00080000;
const uint32 tfDisallowXRP          = 0x00100000;
const uint32 tfAllowXRP             = 0x00200000;
const uint32 tfAccountSetMask       = ~ (TxFlag::requireDestTag | tfOptionalDestTag
                                      | tfRequireAuth | tfOptionalAuth
                                      | tfDisallowXRP | tfAllowXRP);

// OfferCreate flags:
const uint32 tfPassive              = 0x00010000;
const uint32 tfImmediateOrCancel    = 0x00020000;
const uint32 tfFillOrKill           = 0x00040000;
const uint32 tfSell                 = 0x00080000;
const uint32 tfOfferCreateMask      = ~ (tfPassive | tfImmediateOrCancel | tfFillOrKill | tfSell);

// Payment flags:
const uint32 tfNoRippleDirect       = 0x00010000;
const uint32 tfPartialPayment       = 0x00020000;
const uint32 tfLimitQuality         = 0x00040000;
const uint32 tfPaymentMask          = ~ (tfPartialPayment | tfLimitQuality | tfNoRippleDirect);

// TrustSet flags:
const uint32 tfSetfAuth             = 0x00010000;
const uint32 tfTrustSetMask         = ~ (tfSetfAuth);

#endif
