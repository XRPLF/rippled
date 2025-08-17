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

#ifndef RIPPLE_PROTOCOL_TXFLAGS_H_INCLUDED
#define RIPPLE_PROTOCOL_TXFLAGS_H_INCLUDED

#include <xrpl/protocol/LedgerFormats.h>

#include <cstdint>

namespace ripple {

/** Transaction flags.

    These flags are specified in a transaction's 'Flags' field and modify the
    behavior of that transaction.

    There are two types of flags:

        (1) Universal flags: these are flags which apply to, and are interpreted
                             the same way by, all transactions, except, perhaps,
                             to special pseudo-transactions.

        (2) Tx-Specific flags: these are flags which are interpreted according
                               to the type of the transaction being executed.
                               That is, the same numerical flag value may have
                               different effects, depending on the transaction
                               being executed.

    @note The universal transaction flags occupy the high-order 8 bits. The
          tx-specific flags occupy the remaining 24 bits.

    @warning Transaction flags form part of the protocol. **Changing them
             should be avoided because without special handling, this will
             result in a hard fork.**

    @ingroup protocol
*/

// Formatting equals sign aligned 4 spaces after longest prefix, except for
// wrapped lines

// Universal Transaction flags:
constexpr std::uint32_t tfFullyCanonicalSig = 0x80000000;
constexpr std::uint32_t tfInnerBatchTxn = 0x40000000;
constexpr std::uint32_t tfUniversal = tfFullyCanonicalSig | tfInnerBatchTxn;
constexpr std::uint32_t tfUniversalMask = ~tfUniversal;

#pragma push_macro("XMACRO")
#pragma push_macro("TO_VALUE")
#pragma push_macro("VALUE_TO_MAP")
#pragma push_macro("NULL_NAME")
#pragma push_macro("NULL_OUTPUT")
#pragma push_macro("TO_MAP")
#pragma push_macro("TO_MASK")
#pragma push_macro("VALUE_TO_MASK")
#pragma push_macro("ALL_TX_FLAGS")

#undef XMACRO
#undef TO_VALUE
#undef VALUE_TO_MAP
#undef NULL_NAME
#undef NULL_OUTPUT
#undef TO_MAP
#undef TO_MASK
#undef VALUE_TO_MASK

// clang-format off
#undef ALL_TX_FLAGS

#define XMACRO(TX_FLAGS, TF_FLAG, TF_FLAG2)               \
    TX_FLAGS(AccountSet,                                  \
        TF_FLAG(tfRequireDestTag, 0x00010000)             \
        TF_FLAG(tfOptionalDestTag, 0x00020000)            \
        TF_FLAG(tfRequireAuth, 0x00040000)                \
        TF_FLAG(tfOptionalAuth, 0x00080000)               \
        TF_FLAG(tfDisallowXRP, 0x00100000)                \
        TF_FLAG(tfAllowXRP, 0x00200000))                  \
    TX_FLAGS(OfferCreate,                                 \
        TF_FLAG(tfPassive, 0x00010000)                    \
        TF_FLAG(tfImmediateOrCancel, 0x00020000)          \
        TF_FLAG(tfFillOrKill, 0x00040000)                 \
        TF_FLAG(tfSell, 0x00080000)                       \
        TF_FLAG(tfHybrid, 0x00100000))                    \
    TX_FLAGS(Payment,                                     \
        TF_FLAG(tfNoRippleDirect, 0x00010000)             \
        TF_FLAG(tfPartialPayment, 0x00020000)             \
        TF_FLAG(tfLimitQuality, 0x00040000))              \
    TX_FLAGS(TrustSet,                                    \
        TF_FLAG(tfSetfAuth, 0x00010000)                   \
        TF_FLAG(tfSetNoRipple, 0x00020000)                \
        TF_FLAG(tfClearNoRipple, 0x00040000)              \
        TF_FLAG(tfSetFreeze, 0x00100000)                  \
        TF_FLAG(tfClearFreeze, 0x00200000)                \
        TF_FLAG(tfSetDeepFreeze, 0x00400000)              \
        TF_FLAG(tfClearDeepFreeze, 0x00800000))           \
    TX_FLAGS(EnableAmendment,                             \
        TF_FLAG(tfGotMajority, 0x00010000)                \
        TF_FLAG(tfLostMajority, 0x00020000))              \
    TX_FLAGS(PaymentChannelClaim,                         \
        TF_FLAG(tfRenew, 0x00010000)                      \
        TF_FLAG(tfClose, 0x00020000))                     \
    TX_FLAGS(NFTokenMint,                                 \
        TF_FLAG(tfBurnable, 0x00000001)                   \
        TF_FLAG(tfOnlyXRP, 0x00000002)                    \
        TF_FLAG(tfTrustLine, 0x00000004)                  \
        TF_FLAG(tfTransferable, 0x00000008)               \
        TF_FLAG(tfMutable, 0x00000010))                   \
    TX_FLAGS(MPTokenIssuanceCreate,                       \
        TF_FLAG(tfMPTCanLock, lsfMPTCanLock)              \
        TF_FLAG(tfMPTRequireAuth, lsfMPTRequireAuth)      \
        TF_FLAG(tfMPTCanEscrow, lsfMPTCanEscrow)          \
        TF_FLAG(tfMPTCanTrade, lsfMPTCanTrade)            \
        TF_FLAG(tfMPTCanTransfer, lsfMPTCanTransfer)      \
        TF_FLAG(tfMPTCanClawback, lsfMPTCanClawback))     \
    TX_FLAGS(MPTokenAuthorize,                            \
        TF_FLAG(tfMPTUnauthorize, 0x00000001))            \
    TX_FLAGS(MPTokenIssuanceSet,                          \
        TF_FLAG(tfMPTLock, 0x00000001)                    \
        TF_FLAG(tfMPTUnlock, 0x00000002))                 \
    TX_FLAGS(NFTokenCreateOffer,                          \
        TF_FLAG(tfSellNFToken, 0x00000001))               \
    TX_FLAGS(AMMDeposit,                                  \
        TF_FLAG(tfLPToken, 0x00010000)                    \
        TF_FLAG(tfWithdrawAll, 0x00020000)                \
        TF_FLAG(tfOneAssetWithdrawAll, 0x00040000)        \
        TF_FLAG(tfSingleAsset, 0x00080000)                \
        TF_FLAG(tfTwoAsset, 0x00100000)                   \
        TF_FLAG(tfOneAssetLPToken, 0x00200000)            \
        TF_FLAG(tfLimitLPToken, 0x00400000))              \
    TX_FLAGS(AMMWithdraw,                                 \
        TF_FLAG2(tfLPToken, 0x00010000)                   \
        TF_FLAG2(tfSingleAsset, 0x00080000)               \
        TF_FLAG2(tfTwoAsset, 0x00100000)                  \
        TF_FLAG2(tfOneAssetLPToken, 0x00200000)           \
        TF_FLAG2(tfLimitLPToken, 0x00400000)              \
        TF_FLAG(tfTwoAssetIfEmpty, 0x00800000))           \
    TX_FLAGS(AMMClawback,                                 \
        TF_FLAG(tfClawTwoAssets, 0x00000001))             \
    TX_FLAGS(XChainModifyBridge,                          \
        TF_FLAG(tfClearAccountCreateAmount, 0x00010000))  \
    TX_FLAGS(VaultCreate,                                 \
        TF_FLAG(tfVaultPrivate, lsfVaultPrivate)          \
        TF_FLAG(tfVaultShareNonTransferable, 0x00020000)) \
    TX_FLAGS(Batch,                                       \
        TF_FLAG(tfAllOrNothing, 0x00010000)               \
        TF_FLAG(tfOnlyOne, 0x00020000)                    \
        TF_FLAG(tfUntilFailure, 0x00040000)               \
        TF_FLAG(tfIndependent, 0x00080000))

// clang-format on

#define TO_VALUE(name, value) constexpr std::uint32_t name = value;
#define VALUE_TO_MAP(name, value) {#name, value},
#define NULL_NAME(name, values) values
#define NULL_OUTPUT(name, value)
XMACRO(NULL_NAME, TO_VALUE, NULL_OUTPUT)

#define TO_MAP(name, values) \
    std::map<std::string, std::uint32_t> const name##Flags = {values};

XMACRO(TO_MAP, VALUE_TO_MAP, VALUE_TO_MAP)

#define TO_MASK(name, values) \
    constexpr std::uint32_t tf##name##Mask = ~(tfUniversal values);
#define VALUE_TO_MASK(name, value) | name

XMACRO(TO_MASK, VALUE_TO_MASK, VALUE_TO_MASK)

#define ALL_TX_FLAGS(name, values) {#name, name##Flags},

std::vector<std::pair<std::string, std::map<std::string, std::uint32_t>>> const
    allTxFlags = {XMACRO(ALL_TX_FLAGS, NULL_OUTPUT, NULL_OUTPUT)};

#undef XMACRO
#undef TO_VALUE
#undef VALUE_TO_MAP
#undef NULL_NAME
#undef NULL_OUTPUT
#undef TO_MAP
#undef TO_MASK
#undef VALUE_TO_MASK
#undef ALL_TX_FLAGS

#pragma pop_macro("XMACRO")
#pragma pop_macro("TO_VALUE")
#pragma pop_macro("VALUE_TO_MAP")
#pragma pop_macro("NULL_NAME")
#pragma pop_macro("NULL_OUTPUT")
#pragma pop_macro("TO_MAP")
#pragma pop_macro("TO_MASK")
#pragma pop_macro("VALUE_TO_MASK")
#pragma pop_macro("ALL_TX_FLAGS")

// Additional transaction masks
constexpr std::uint32_t tfMPTPaymentMask = ~(tfUniversal | tfPartialPayment);
constexpr std::uint32_t tfTrustSetPermissionMask =
    ~(tfUniversal | tfSetfAuth | tfSetFreeze | tfClearFreeze);
constexpr std::uint32_t const tfMPTokenIssuanceSetPermissionMask =
    ~(tfUniversal | tfMPTLock | tfMPTUnlock);

// Prior to fixRemoveNFTokenAutoTrustLine, transfer of an NFToken between
// accounts allowed a TrustLine to be added to the issuer of that token
// without explicit permission from that issuer.  This was enabled by
// minting the NFToken with the tfTrustLine flag set.
//
// That capability could be used to attack the NFToken issuer.  It
// would be possible for two accounts to trade the NFToken back and forth
// building up any number of TrustLines on the issuer, increasing the
// issuer's reserve without bound.
//
// The fixRemoveNFTokenAutoTrustLine amendment disables minting with the
// tfTrustLine flag as a way to prevent the attack.  But until the
// amendment passes we still need to keep the old behavior available.
constexpr std::uint32_t const tfNFTokenMintMaskWithoutMutable =
    ~(tfUniversal | tfBurnable | tfOnlyXRP | tfTransferable);

constexpr std::uint32_t const tfNFTokenMintOldMask =
    ~(~tfNFTokenMintMask | tfTrustLine);

// if featureDynamicNFT enabled then new flag allowing mutable URI available.
constexpr std::uint32_t const tfNFTokenMintOldMaskWithMutable =
    ~(~tfNFTokenMintOldMask | tfMutable);

constexpr std::uint32_t tfWithdrawSubTx = tfLPToken | tfSingleAsset |
    tfTwoAsset | tfOneAssetLPToken | tfLimitLPToken | tfWithdrawAll |
    tfOneAssetWithdrawAll;
constexpr std::uint32_t tfDepositSubTx = tfLPToken | tfSingleAsset |
    tfTwoAsset | tfOneAssetLPToken | tfLimitLPToken | tfTwoAssetIfEmpty;

// /**
//  * @note If nested Batch transactions are supported in the future, the
//  tfInnerBatchTxn flag
//  *  will need to be removed from this mask to allow Batch transaction to be
//  inside
//  *  the sfRawTransactions array.
//  */
// constexpr std::uint32_t const tfBatchMask =
//     ~(tfUniversal | tfAllOrNothing | tfOnlyOne | tfUntilFailure |
//     tfIndependent) | tfInnerBatchTxn;

// AccountSet SetFlag/ClearFlag values
#define ACCOUNTSET_FLAGS(ASF_FLAG)                \
    ASF_FLAG(asfRequireDest, 1)                   \
    ASF_FLAG(asfRequireAuth, 2)                   \
    ASF_FLAG(asfDisallowXRP, 3)                   \
    ASF_FLAG(asfDisableMaster, 4)                 \
    ASF_FLAG(asfAccountTxnID, 5)                  \
    ASF_FLAG(asfNoFreeze, 6)                      \
    ASF_FLAG(asfGlobalFreeze, 7)                  \
    ASF_FLAG(asfDefaultRipple, 8)                 \
    ASF_FLAG(asfDepositAuth, 9)                   \
    ASF_FLAG(asfAuthorizedNFTokenMinter, 10)      \
    /* 11 unused */                               \
    ASF_FLAG(asfDisallowIncomingNFTokenOffer, 12) \
    ASF_FLAG(asfDisallowIncomingCheck, 13)        \
    ASF_FLAG(asfDisallowIncomingPayChan, 14)      \
    ASF_FLAG(asfDisallowIncomingTrustline, 15)    \
    ASF_FLAG(asfAllowTrustLineClawback, 16)       \
    ASF_FLAG(asfAllowTrustLineLocking, 17)

#define ACCOUNTSET_FLAG_TO_VALUE(name, value) \
    constexpr std::uint32_t name = value;
#define ACCOUNTSET_FLAG_TO_MAP(name, value) {#name, value},

ACCOUNTSET_FLAGS(ACCOUNTSET_FLAG_TO_VALUE)
static std::map<std::string, int> const asfFlagMap = {
    ACCOUNTSET_FLAGS(ACCOUNTSET_FLAG_TO_MAP)};

}  // namespace ripple

#endif
