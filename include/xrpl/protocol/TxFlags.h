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

// specify a generic XMACRO such that a runtime std::unordered_map and cpp type
// declarations can be generated with plug-and-play macros
#define LIST_OF_TRANSACTION_FLAGS(                                             \
    X, X_CONST_TYPE_DECL, START_TX_FLAGS_XMACRO, END_TX_FLAGS_XMACRO)          \
    /* Universal Transaction flags */                                          \
    START_TX_FLAGS_XMACRO(Universal)                                           \
    X(tfFullyCanonicalSig, 0x80000000)                                         \
    X(tfInnerBatchTxn, 0x40000000)                                             \
    X(tfUniversal, tfFullyCanonicalSig | tfInnerBatchTxn)                      \
    X(tfUniversalMask, ~tfUniversal)                                           \
    END_TX_FLAGS_XMACRO                                                        \
    /* AccountSet flags: */                                                    \
    START_TX_FLAGS_XMACRO(AccountSet)                                          \
    X(tfRequireDestTag, 0x00010000)                                            \
    X(tfOptionalDestTag, 0x00020000)                                           \
    X(tfRequireAuth, 0x00040000)                                               \
    X(tfOptionalAuth, 0x00080000)                                              \
    X(tfDisallowXRP, 0x00100000)                                               \
    X(tfAllowXRP, 0x00200000)                                                  \
    X(tfAccountSetMask,                                                        \
      ~(tfUniversal | tfRequireDestTag | tfOptionalDestTag | tfRequireAuth |   \
        tfOptionalAuth | tfDisallowXRP | tfAllowXRP))                          \
    END_TX_FLAGS_XMACRO                                                        \
    /* AccountSet SetFlag/ClearFlag values */                                  \
    START_TX_FLAGS_XMACRO(AccountSetUpdateFlags)                               \
    X(asfRequireDest, 1)                                                       \
    X(asfRequireAuth, 2)                                                       \
    X(asfDisallowXRP, 3)                                                       \
    X(asfDisableMaster, 4)                                                     \
    X(asfAccountTxnID, 5)                                                      \
    X(asfNoFreeze, 6)                                                          \
    X(asfGlobalFreeze, 7)                                                      \
    X(asfDefaultRipple, 8)                                                     \
    X(asfDepositAuth, 9)                                                       \
    X(asfAuthorizedNFTokenMinter, 10)                                          \
    /*  // reserved for Hooks amendment */                                     \
    /* X(asfTshCollect, 11) */                                                 \
    X(asfDisallowIncomingNFTokenOffer, 12)                                     \
    X(asfDisallowIncomingCheck, 13)                                            \
    X(asfDisallowIncomingPayChan, 14)                                          \
    X(asfDisallowIncomingTrustline, 15)                                        \
    X(asfAllowTrustLineClawback, 16)                                           \
    X(asfAllowTrustLineLocking, 17)                                            \
    END_TX_FLAGS_XMACRO                                                        \
    /* OfferCreate flags: */                                                   \
    START_TX_FLAGS_XMACRO(OfferCreate)                                         \
    X(tfPassive, 0x00010000)                                                   \
    X(tfImmediateOrCancel, 0x00020000)                                         \
    X(tfFillOrKill, 0x00040000)                                                \
    X(tfSell, 0x00080000)                                                      \
    X(tfHybrid, 0x00100000)                                                    \
    X(tfOfferCreateMask,                                                       \
      ~(tfUniversal | tfPassive | tfImmediateOrCancel | tfFillOrKill |         \
        tfSell | tfHybrid))                                                    \
    END_TX_FLAGS_XMACRO                                                        \
    /* Payment flags: */                                                       \
    START_TX_FLAGS_XMACRO(Payment)                                             \
    X(tfNoRippleDirect, 0x00010000)                                            \
    X(tfPartialPayment, 0x00020000)                                            \
    X(tfLimitQuality, 0x00040000)                                              \
    X(tfPaymentMask,                                                           \
      ~(tfUniversal | tfPartialPayment | tfLimitQuality | tfNoRippleDirect))   \
    X(tfMPTPaymentMask, ~(tfUniversal | tfPartialPayment))                     \
    END_TX_FLAGS_XMACRO                                                        \
    /* TrustSet flags: */                                                      \
    START_TX_FLAGS_XMACRO(TrustSet)                                            \
    X(tfSetfAuth, 0x00010000)                                                  \
    X(tfSetNoRipple, 0x00020000)                                               \
    X(tfClearNoRipple, 0x00040000)                                             \
    X(tfSetFreeze, 0x00100000)                                                 \
    X(tfClearFreeze, 0x00200000)                                               \
    X(tfSetDeepFreeze, 0x00400000)                                             \
    X(tfClearDeepFreeze, 0x00800000)                                           \
    X(tfTrustSetMask,                                                          \
      ~(tfUniversal | tfSetfAuth | tfSetNoRipple | tfClearNoRipple |           \
        tfSetFreeze | tfClearFreeze | tfSetDeepFreeze | tfClearDeepFreeze))    \
    X(tfTrustSetPermissionMask,                                                \
      ~(tfUniversal | tfSetfAuth | tfSetFreeze | tfClearFreeze))               \
    END_TX_FLAGS_XMACRO                                                        \
    /* EnableAmendment flags: */                                               \
    START_TX_FLAGS_XMACRO(EnableAmendment)                                     \
    X(tfGotMajority, 0x00010000)                                               \
    X(tfLostMajority, 0x00020000)                                              \
    X(tfChangeMask, ~(tfUniversal | tfGotMajority | tfLostMajority))           \
    END_TX_FLAGS_XMACRO                                                        \
                                                                               \
    /* PaymentChannelClaim flags: */                                           \
    START_TX_FLAGS_XMACRO(PaymentChannelClaim)                                 \
    X(tfRenew, 0x00010000)                                                     \
    X(tfClose, 0x00020000)                                                     \
    X(tfPayChanClaimMask, ~(tfUniversal | tfRenew | tfClose))                  \
    END_TX_FLAGS_XMACRO                                                        \
                                                                               \
    /* NFTokenMint flags: */                                                   \
    START_TX_FLAGS_XMACRO(NFTokenMint)                                         \
    X_CONST_TYPE_DECL(tfBurnable, 0x00000001)                                  \
    X_CONST_TYPE_DECL(tfOnlyXRP, 0x00000002)                                   \
    X_CONST_TYPE_DECL(tfTrustLine, 0x00000004)                                 \
    X_CONST_TYPE_DECL(tfTransferable, 0x00000008)                              \
    X_CONST_TYPE_DECL(tfMutable, 0x00000010)                                   \
    END_TX_FLAGS_XMACRO                                                        \
                                                                               \
    /* MPTokenIssuanceCreate flags: */                                         \
    START_TX_FLAGS_XMACRO(MPTokenIssuanceCreate)                               \
    /* Note: tf/lsfMPTLocked is intentionally omitted, since this transaction  \
     * is not allowed to modify it. */                                         \
    X_CONST_TYPE_DECL(tfMPTCanLock, lsfMPTCanLock)                             \
    X_CONST_TYPE_DECL(tfMPTRequireAuth, lsfMPTRequireAuth)                     \
    X_CONST_TYPE_DECL(tfMPTCanEscrow, lsfMPTCanEscrow)                         \
    X_CONST_TYPE_DECL(tfMPTCanTrade, lsfMPTCanTrade)                           \
    X_CONST_TYPE_DECL(tfMPTCanTransfer, lsfMPTCanTransfer)                     \
    X_CONST_TYPE_DECL(tfMPTCanClawback, lsfMPTCanClawback)                     \
    X_CONST_TYPE_DECL(                                                         \
        tfMPTokenIssuanceCreateMask,                                           \
        ~(tfUniversal | tfMPTCanLock | tfMPTRequireAuth | tfMPTCanEscrow |     \
          tfMPTCanTrade | tfMPTCanTransfer | tfMPTCanClawback))                \
                                                                               \
    /* MPTokenIssuanceCreate MutableFlags: */                                  \
    /* Indicating specific fields or flags may be changed after issuance. */   \
    X_CONST_TYPE_DECL(tmfMPTCanMutateCanLock, lsmfMPTCanMutateCanLock)         \
    X_CONST_TYPE_DECL(tmfMPTCanMutateRequireAuth, lsmfMPTCanMutateRequireAuth) \
    X_CONST_TYPE_DECL(tmfMPTCanMutateCanEscrow, lsmfMPTCanMutateCanEscrow)     \
    X_CONST_TYPE_DECL(tmfMPTCanMutateCanTrade, lsmfMPTCanMutateCanTrade)       \
    X_CONST_TYPE_DECL(tmfMPTCanMutateCanTransfer, lsmfMPTCanMutateCanTransfer) \
    X_CONST_TYPE_DECL(tmfMPTCanMutateCanClawback, lsmfMPTCanMutateCanClawback) \
    X_CONST_TYPE_DECL(tmfMPTCanMutateMetadata, lsmfMPTCanMutateMetadata)       \
    X_CONST_TYPE_DECL(tmfMPTCanMutateTransferFee, lsmfMPTCanMutateTransferFee) \
    X_CONST_TYPE_DECL(                                                         \
        tmfMPTokenIssuanceCreateMutableMask,                                   \
        ~(tmfMPTCanMutateCanLock | tmfMPTCanMutateRequireAuth |                \
          tmfMPTCanMutateCanEscrow | tmfMPTCanMutateCanTrade |                 \
          tmfMPTCanMutateCanTransfer | tmfMPTCanMutateCanClawback |            \
          tmfMPTCanMutateMetadata | tmfMPTCanMutateTransferFee))               \
    END_TX_FLAGS_XMACRO                                                        \
                                                                               \
    /* MPTokenAuthorize flags: */                                              \
    START_TX_FLAGS_XMACRO(MPTokenAuthorize)                                    \
    X_CONST_TYPE_DECL(tfMPTUnauthorize, 0x00000001)                            \
    X_CONST_TYPE_DECL(                                                         \
        tfMPTokenAuthorizeMask, ~(tfUniversal | tfMPTUnauthorize))             \
    END_TX_FLAGS_XMACRO                                                        \
                                                                               \
    /* MPTokenIssuanceSet flags: */                                            \
    START_TX_FLAGS_XMACRO(MPTokenIssuanceSet)                                  \
    X_CONST_TYPE_DECL(tfMPTLock, 0x00000001)                                   \
    X_CONST_TYPE_DECL(tfMPTUnlock, 0x00000002)                                 \
    X_CONST_TYPE_DECL(                                                         \
        tfMPTokenIssuanceSetMask, ~(tfUniversal | tfMPTLock | tfMPTUnlock))    \
    X_CONST_TYPE_DECL(                                                         \
        tfMPTokenIssuanceSetPermissionMask,                                    \
        ~(tfUniversal | tfMPTLock | tfMPTUnlock))                              \
                                                                               \
    /* MPTokenIssuanceSet MutableFlags: */                                     \
    /* Set or Clear flags */                                                   \
    X_CONST_TYPE_DECL(tmfMPTSetCanLock, 0x00000001)                            \
    X_CONST_TYPE_DECL(tmfMPTClearCanLock, 0x00000002)                          \
    X_CONST_TYPE_DECL(tmfMPTSetRequireAuth, 0x00000004)                        \
    X_CONST_TYPE_DECL(tmfMPTClearRequireAuth, 0x00000008)                      \
    X_CONST_TYPE_DECL(tmfMPTSetCanEscrow, 0x00000010)                          \
    X_CONST_TYPE_DECL(tmfMPTClearCanEscrow, 0x00000020)                        \
    X_CONST_TYPE_DECL(tmfMPTSetCanTrade, 0x00000040)                           \
    X_CONST_TYPE_DECL(tmfMPTClearCanTrade, 0x00000080)                         \
    X_CONST_TYPE_DECL(tmfMPTSetCanTransfer, 0x00000100)                        \
    X_CONST_TYPE_DECL(tmfMPTClearCanTransfer, 0x00000200)                      \
    X_CONST_TYPE_DECL(tmfMPTSetCanClawback, 0x00000400)                        \
    X_CONST_TYPE_DECL(tmfMPTClearCanClawback, 0x00000800)                      \
    X_CONST_TYPE_DECL(                                                         \
        tmfMPTokenIssuanceSetMutableMask,                                      \
        ~(tmfMPTSetCanLock | tmfMPTClearCanLock | tmfMPTSetRequireAuth |       \
          tmfMPTClearRequireAuth | tmfMPTSetCanEscrow | tmfMPTClearCanEscrow | \
          tmfMPTSetCanTrade | tmfMPTClearCanTrade | tmfMPTSetCanTransfer |     \
          tmfMPTClearCanTransfer | tmfMPTSetCanClawback |                      \
          tmfMPTClearCanClawback))                                             \
    END_TX_FLAGS_XMACRO                                                        \
                                                                               \
    /* MPTokenIssuanceDestroy flags: */                                        \
    START_TX_FLAGS_XMACRO(MPTokenIssuanceDestroy)                              \
    X_CONST_TYPE_DECL(tfMPTokenIssuanceDestroyMask, ~tfUniversal)              \
    END_TX_FLAGS_XMACRO                                                        \
                                                                               \
    /* The below four flags are categorised under NFTokenMint transaction      \
     * flags */                                                                \
    START_TX_FLAGS_XMACRO(NFTokenMint)                                         \
    /* Prior to fixRemoveNFTokenAutoTrustLine, transfer of an NFToken between  \
     */                                                                        \
    /* accounts allowed a TrustLine to be added to the issuer of that token */ \
    /* without explicit permission from that issuer.  This was enabled by */   \
    /* minting the NFToken with the tfTrustLine flag set. */                   \
    /* That capability could be used to attack the NFToken issuer.  It */      \
    /* would be possible for two accounts to trade the NFToken back and forth  \
     */                                                                        \
    /* building up any number of TrustLines on the issuer, increasing the */   \
    /* issuer's reserve without bound. */                                      \
    /* The fixRemoveNFTokenAutoTrustLine amendment disables minting with the   \
     */                                                                        \
    /* tfTrustLine flag as a way to prevent the attack.  But until the */      \
    /* amendment passes we still need to keep the old behavior available. */   \
    X_CONST_TYPE_DECL(                                                         \
        tfNFTokenMintMask,                                                     \
        ~(tfUniversal | tfBurnable | tfOnlyXRP | tfTransferable))              \
                                                                               \
    X_CONST_TYPE_DECL(                                                         \
        tfNFTokenMintOldMask, ~(~tfNFTokenMintMask | tfTrustLine))             \
                                                                               \
    /* if featureDynamicNFT enabled then new flag allowing mutable URI         \
     * available. */                                                           \
    X_CONST_TYPE_DECL(                                                         \
        tfNFTokenMintOldMaskWithMutable, ~(~tfNFTokenMintOldMask | tfMutable)) \
                                                                               \
    X_CONST_TYPE_DECL(                                                         \
        tfNFTokenMintMaskWithMutable, ~(~tfNFTokenMintMask | tfMutable))       \
                                                                               \
    END_TX_FLAGS_XMACRO                                                        \
    /* NFTokenCreateOffer flags: */                                            \
    START_TX_FLAGS_XMACRO(NFTokenCreateOffer)                                  \
    X_CONST_TYPE_DECL(tfSellNFToken, 0x00000001)                               \
    X_CONST_TYPE_DECL(                                                         \
        tfNFTokenCreateOfferMask, ~(tfUniversal | tfSellNFToken))              \
    END_TX_FLAGS_XMACRO                                                        \
    /* NFTokenCancelOffer flags: */                                            \
    START_TX_FLAGS_XMACRO(NFTokenCancelOffer)                                  \
    X_CONST_TYPE_DECL(tfNFTokenCancelOfferMask, ~tfUniversal)                  \
    END_TX_FLAGS_XMACRO                                                        \
                                                                               \
    /* NFTokenAcceptOffer flags: */                                            \
    START_TX_FLAGS_XMACRO(NFTokenAcceptOffer)                                  \
    X_CONST_TYPE_DECL(tfNFTokenAcceptOfferMask, ~tfUniversal)                  \
    END_TX_FLAGS_XMACRO                                                        \
                                                                               \
    /* Clawback flags: */                                                      \
    START_TX_FLAGS_XMACRO(Clawback)                                            \
    X_CONST_TYPE_DECL(tfClawbackMask, ~tfUniversal)                            \
    END_TX_FLAGS_XMACRO                                                        \
                                                                               \
    /* AMM Flags: */                                                           \
    START_TX_FLAGS_XMACRO(AMM)                                                 \
    X(tfLPToken, 0x00010000)                                                   \
    X(tfWithdrawAll, 0x00020000)                                               \
    X(tfOneAssetWithdrawAll, 0x00040000)                                       \
    X(tfSingleAsset, 0x00080000)                                               \
    X(tfTwoAsset, 0x00100000)                                                  \
    X(tfOneAssetLPToken, 0x00200000)                                           \
    X(tfLimitLPToken, 0x00400000)                                              \
    X(tfTwoAssetIfEmpty, 0x00800000)                                           \
    X(tfWithdrawSubTx,                                                         \
      tfLPToken | tfSingleAsset | tfTwoAsset | tfOneAssetLPToken |             \
          tfLimitLPToken | tfWithdrawAll | tfOneAssetWithdrawAll)              \
    X(tfDepositSubTx,                                                          \
      tfLPToken | tfSingleAsset | tfTwoAsset | tfOneAssetLPToken |             \
          tfLimitLPToken | tfTwoAssetIfEmpty)                                  \
    X(tfWithdrawMask, ~(tfUniversal | tfWithdrawSubTx))                        \
    X(tfDepositMask, ~(tfUniversal | tfDepositSubTx))                          \
    END_TX_FLAGS_XMACRO                                                        \
    /* AMMClawback flags: */                                                   \
    START_TX_FLAGS_XMACRO(AMMClawback)                                         \
    X(tfClawTwoAssets, 0x00000001)                                             \
    X(tfAMMClawbackMask, ~(tfUniversal | tfClawTwoAssets))                     \
    END_TX_FLAGS_XMACRO                                                        \
    /* BridgeModify flags: */                                                  \
    START_TX_FLAGS_XMACRO(BridgeModify)                                        \
    X(tfClearAccountCreateAmount, 0x00010000)                                  \
    X(tfBridgeModifyMask, ~(tfUniversal | tfClearAccountCreateAmount))         \
    END_TX_FLAGS_XMACRO                                                        \
    /* VaultCreate flags: */                                                   \
    START_TX_FLAGS_XMACRO(VaultCreate)                                         \
    X(tfVaultPrivate, 0x00010000)                                              \
    X(tfVaultShareNonTransferable, 0x00020000)                                 \
    X(tfVaultCreateMask,                                                       \
      ~(tfUniversal | tfVaultPrivate | tfVaultShareNonTransferable))           \
    END_TX_FLAGS_XMACRO                                                        \
    /* Batch Flags: */                                                         \
    START_TX_FLAGS_XMACRO(Batch)                                               \
    X(tfAllOrNothing, 0x00010000)                                              \
    X(tfOnlyOne, 0x00020000)                                                   \
    X(tfUntilFailure, 0x00040000)                                              \
    X(tfIndependent, 0x00080000)                                               \
    /* @note If nested Batch transactions are supported in the future, the     \
     * tfInnerBatchTxn flag */                                                 \
    /*  will need to be removed from this mask to allow Batch transaction to   \
     * be inside */                                                            \
    /*  the sfRawTransactions array. */                                        \
    X_CONST_TYPE_DECL(                                                         \
        tfBatchMask,                                                           \
        ~(tfUniversal | tfAllOrNothing | tfOnlyOne | tfUntilFailure |          \
          tfIndependent) |                                                     \
            tfInnerBatchTxn)                                                   \
    END_TX_FLAGS_XMACRO

namespace ripple {

// cpp type and value declarations
#define START_TX_SECTION_NOOP(txName)
#define END_TX_SECTION_NOOP
#define DEFINE_FLAGS(flagName, flagValue) \
    constexpr std::uint32_t flagName = flagValue;
#define DEFINE_FLAGS_CONST_TYPE(flagName, flagValue) \
    constexpr std::uint32_t const flagName = flagValue;
LIST_OF_TRANSACTION_FLAGS(
    DEFINE_FLAGS,
    DEFINE_FLAGS_CONST_TYPE,
    START_TX_SECTION_NOOP,
    END_TX_SECTION_NOOP)
#undef DEFINE_FLAGS_CONST_TYPE
#undef DEFINE_FLAGS
#undef END_TX_SECTION_NOOP
#undef START_TX_SECTION_NOOP

static_assert(tfVaultPrivate == lsfVaultPrivate);

#define START_TX_SECTION(txnName)                               \
    {                                                           \
        #txnName, std::unordered_map<std::string, unsigned int> \
        {
#define EXPAND_TX_FLAGS(flagName, flagValue) {#flagName, flagValue},
#define EXPAND_TX_FLAGS_CONST_TYPE(flagName, flagValue) {#flagName, flagValue},
#define END_TX_SECTION \
    }                  \
    ,                  \
    }                  \
    ,
std::unordered_map<
    std::string,
    std::unordered_map<std::string, unsigned int>> const TXFlags = {
    LIST_OF_TRANSACTION_FLAGS(
        EXPAND_TX_FLAGS,
        EXPAND_TX_FLAGS_CONST_TYPE,
        START_TX_SECTION,
        END_TX_SECTION)};
#undef END_TX_SECTION
#undef EXPAND_TX_FLAGS_CONST_TYPE
#undef EXPAND_TX_FLAGS
#undef START_TX_SECTION

}  // namespace ripple

#endif
