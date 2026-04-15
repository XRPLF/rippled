#pragma once

#include <xrpl/protocol/LedgerFormats.h>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

/** Transaction flags.

    These flags are specified in a transaction's 'Flags' field and modify
    the behavior of that transaction.

    There are two types of flags:

        (1) Universal flags: these are flags which apply to, and are interpreted the same way by,
            all transactions, except, perhaps, to special pseudo-transactions.

        (2) Tx-Specific flags: these are flags which are interpreted according to the type of the
            transaction being executed. That is, the same numerical flag value may have different
            effects, depending on the transaction being executed.

    @note The universal transaction flags occupy the high-order 8 bits.
          The tx-specific flags occupy the remaining 24 bits.

    @warning Transaction flags form part of the protocol.
             **Changing them should be avoided because without special handling, this will result in
             a hard fork.**

    @ingroup protocol
*/

using FlagValue = std::uint32_t;

// Universal Transaction flags:
inline constexpr FlagValue tfFullyCanonicalSig = 0x80000000;
inline constexpr FlagValue tfInnerBatchTxn = 0x40000000;
inline constexpr FlagValue tfUniversal = tfFullyCanonicalSig | tfInnerBatchTxn;
inline constexpr FlagValue tfUniversalMask = ~tfUniversal;

#pragma push_macro("XMACRO")
#pragma push_macro("TO_VALUE")
#pragma push_macro("VALUE_TO_MAP")
#pragma push_macro("NULL_NAME")
#pragma push_macro("NULL_OUTPUT")
#pragma push_macro("TO_MAP")
#pragma push_macro("TO_MASK")
#pragma push_macro("VALUE_TO_MASK")
#pragma push_macro("ALL_TX_FLAGS")
#pragma push_macro("NULL_MASK_ADJ")
#pragma push_macro("MASK_ADJ_TO_MASK")

#undef XMACRO
#undef TO_VALUE
#undef VALUE_TO_MAP
#undef NULL_NAME
#undef NULL_OUTPUT
#undef TO_MAP
#undef TO_MASK
#undef VALUE_TO_MASK
#undef NULL_MASK_ADJ
#undef MASK_ADJ_TO_MASK

// clang-format off
#undef ALL_TX_FLAGS

// XMACRO parameters:
// - TRANSACTION: handles the transaction name, its flags, and mask adjustment
// - TF_FLAG: defines a new flag constant
// - TF_FLAG2: references an existing flag constant (no new definition)
// - MASK_ADJ: specifies flags to add back to the mask (making them invalid for this tx type)
//
// Note: MASK_ADJ is used when a universal flag should be invalid for a specific transaction.
// For example, Batch uses MASK_ADJ(tfInnerBatchTxn) because the outer Batch transaction
// must not have tfInnerBatchTxn set (only inner transactions should have it).
//
// TODO: Consider rewriting this using reflection in C++26 or later. Alternatively this could be a DSL processed by a script at build time.
#define XMACRO(TRANSACTION, TF_FLAG, TF_FLAG2, MASK_ADJ)                                                                                                       \
    TRANSACTION(AccountSet,                                                                                                                                    \
        TF_FLAG(tfRequireDestTag, 0x00010000)                                                                                                                  \
        TF_FLAG(tfOptionalDestTag, 0x00020000)                                                                                                                 \
        TF_FLAG(tfRequireAuth, 0x00040000)                                                                                                                     \
        TF_FLAG(tfOptionalAuth, 0x00080000)                                                                                                                    \
        TF_FLAG(tfDisallowXRP, 0x00100000)                                                                                                                     \
        TF_FLAG(tfAllowXRP, 0x00200000),                                                                                                                       \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(OfferCreate,                                                                                                                                   \
        TF_FLAG(tfPassive, 0x00010000)                                                                                                                         \
        TF_FLAG(tfImmediateOrCancel, 0x00020000)                                                                                                               \
        TF_FLAG(tfFillOrKill, 0x00040000)                                                                                                                      \
        TF_FLAG(tfSell, 0x00080000)                                                                                                                            \
        TF_FLAG(tfHybrid, 0x00100000),                                                                                                                         \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(Payment,                                                                                                                                       \
        TF_FLAG(tfNoRippleDirect, 0x00010000)                                                                                                                  \
        TF_FLAG(tfPartialPayment, 0x00020000)                                                                                                                  \
        TF_FLAG(tfLimitQuality, 0x00040000),                                                                                                                   \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(TrustSet,                                                                                                                                      \
        TF_FLAG(tfSetfAuth, 0x00010000)                                                                                                                        \
        TF_FLAG(tfSetNoRipple, 0x00020000)                                                                                                                     \
        TF_FLAG(tfClearNoRipple, 0x00040000)                                                                                                                   \
        TF_FLAG(tfSetFreeze, 0x00100000)                                                                                                                       \
        TF_FLAG(tfClearFreeze, 0x00200000)                                                                                                                     \
        TF_FLAG(tfSetDeepFreeze, 0x00400000)                                                                                                                   \
        TF_FLAG(tfClearDeepFreeze, 0x00800000),                                                                                                                \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(EnableAmendment,                                                                                                                               \
        TF_FLAG(tfGotMajority, 0x00010000)                                                                                                                     \
        TF_FLAG(tfLostMajority, 0x00020000),                                                                                                                   \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(PaymentChannelClaim,                                                                                                                           \
        TF_FLAG(tfRenew, 0x00010000)                                                                                                                           \
        TF_FLAG(tfClose, 0x00020000),                                                                                                                          \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(NFTokenMint,                                                                                                                                   \
        TF_FLAG(tfBurnable, 0x00000001)                                                                                                                        \
        TF_FLAG(tfOnlyXRP, 0x00000002)                                                                                                                         \
        /* deprecated TF_FLAG(tfTrustLine, 0x00000004) */                                                                                                      \
        TF_FLAG(tfTransferable, 0x00000008)                                                                                                                    \
        TF_FLAG(tfMutable, 0x00000010),                                                                                                                        \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(MPTokenIssuanceCreate,                                                                                                                         \
        /* Note: tf/lsfMPTLocked is intentionally omitted since this transaction is not allowed to modify it. */                                               \
        TF_FLAG(tfMPTCanLock, lsfMPTCanLock)                                                                                                                   \
        TF_FLAG(tfMPTRequireAuth, lsfMPTRequireAuth)                                                                                                           \
        TF_FLAG(tfMPTCanEscrow, lsfMPTCanEscrow)                                                                                                               \
        TF_FLAG(tfMPTCanTrade, lsfMPTCanTrade)                                                                                                                 \
        TF_FLAG(tfMPTCanTransfer, lsfMPTCanTransfer)                                                                                                           \
        TF_FLAG(tfMPTCanClawback, lsfMPTCanClawback),                                                                                                          \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(MPTokenAuthorize,                                                                                                                              \
        TF_FLAG(tfMPTUnauthorize, 0x00000001),                                                                                                                 \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(MPTokenIssuanceSet,                                                                                                                            \
        TF_FLAG(tfMPTLock, 0x00000001)                                                                                                                         \
        TF_FLAG(tfMPTUnlock, 0x00000002),                                                                                                                      \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(NFTokenCreateOffer,                                                                                                                            \
        TF_FLAG(tfSellNFToken, 0x00000001),                                                                                                                    \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(AMMDeposit,                                                                                                                                    \
        TF_FLAG(tfLPToken, 0x00010000)                                                                                                                         \
        TF_FLAG(tfSingleAsset, 0x00080000)                                                                                                                     \
        TF_FLAG(tfTwoAsset, 0x00100000)                                                                                                                        \
        TF_FLAG(tfOneAssetLPToken, 0x00200000)                                                                                                                 \
        TF_FLAG(tfLimitLPToken, 0x00400000)                                                                                                                    \
        TF_FLAG(tfTwoAssetIfEmpty, 0x00800000),                                                                                                                \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(AMMWithdraw,                                                                                                                                   \
        TF_FLAG2(tfLPToken, 0x00010000)                                                                                                                        \
        TF_FLAG(tfWithdrawAll, 0x00020000)                                                                                                                     \
        TF_FLAG(tfOneAssetWithdrawAll, 0x00040000)                                                                                                             \
        TF_FLAG2(tfSingleAsset, 0x00080000)                                                                                                                    \
        TF_FLAG2(tfTwoAsset, 0x00100000)                                                                                                                       \
        TF_FLAG2(tfOneAssetLPToken, 0x00200000)                                                                                                                \
        TF_FLAG2(tfLimitLPToken, 0x00400000),                                                                                                                  \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(AMMClawback,                                                                                                                                   \
        TF_FLAG(tfClawTwoAssets, 0x00000001),                                                                                                                  \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(XChainModifyBridge,                                                                                                                            \
        TF_FLAG(tfClearAccountCreateAmount, 0x00010000),                                                                                                       \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(VaultCreate,                                                                                                                                   \
        TF_FLAG(tfVaultPrivate, lsfVaultPrivate)                                                                                                               \
        TF_FLAG(tfVaultShareNonTransferable, 0x00020000),                                                                                                      \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(Batch,                                                                                                                                         \
        TF_FLAG(tfAllOrNothing, 0x00010000)                                                                                                                    \
        TF_FLAG(tfOnlyOne, 0x00020000)                                                                                                                         \
        TF_FLAG(tfUntilFailure, 0x00040000)                                                                                                                    \
        TF_FLAG(tfIndependent, 0x00080000),                                                                                                                    \
        MASK_ADJ(tfInnerBatchTxn))                      /* Batch must reject tfInnerBatchTxn - only inner transactions should have this flag */                \
                                                                                                                                                               \
    TRANSACTION(LoanSet,                                /* True indicates the loan supports overpayments */                                                    \
        TF_FLAG(tfLoanOverpayment, 0x00010000),                                                                                                                \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(LoanPay,                                /* True indicates any excess in this payment can be used as an overpayment. */                         \
                                                        /* False: no overpayments will be taken. */                                                            \
        TF_FLAG2(tfLoanOverpayment, 0x00010000)                                                                                                                \
        TF_FLAG(tfLoanFullPayment, 0x00020000)          /* True indicates that the payment is an early full payment. */                                        \
                                                        /* It must pay the entire loan including close interest and fees, or it will fail. */                  \
                                                        /* False: Not a full payment. */                                                                       \
        TF_FLAG(tfLoanLatePayment, 0x00040000),         /* True indicates that the payment is late, and includes late interest and fees. */                    \
                                                        /* If the loan is not late, it will fail. */                                                           \
                                                        /* False: not a late payment. If the current payment is overdue, the transaction will fail.*/          \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(LoanManage,                                                                                                                                    \
        TF_FLAG(tfLoanDefault, 0x00010000)                                                                                                                     \
        TF_FLAG(tfLoanImpair, 0x00020000)                                                                                                                      \
        TF_FLAG(tfLoanUnimpair, 0x00040000),                                                                                                                   \
        MASK_ADJ(0))

// clang-format on

// Create all the flag values.
//
// example:
// inline constexpr FlagValue tfAccountSetRequireDestTag = 0x00010000;
#define TO_VALUE(name, value) inline constexpr FlagValue name = value;
#define NULL_NAME(name, values, maskAdj) values
#define NULL_OUTPUT(name, value)
#define NULL_MASK_ADJ(value)
XMACRO(NULL_NAME, TO_VALUE, NULL_OUTPUT, NULL_MASK_ADJ)

// Create masks for each transaction type that has flags.
//
// example:
// inline constexpr FlagValue tfAccountSetMask = ~(tfUniversal | tfRequireDestTag |
//     tfOptionalDestTag | tfRequireAuth | tfOptionalAuth | tfDisallowXRP | tfAllowXRP);
//
// The mask adjustment (maskAdj) allows adding flags back to the mask, making them invalid.
// For example, Batch uses MASK_ADJ(tfInnerBatchTxn) to reject tfInnerBatchTxn on outer Batch.
#define TO_MASK(name, values, maskAdj) \
    inline constexpr FlagValue tf##name##Mask = ~(tfUniversal values) | (maskAdj);
#define VALUE_TO_MASK(name, value) | name
#define MASK_ADJ_TO_MASK(value) value
XMACRO(TO_MASK, VALUE_TO_MASK, VALUE_TO_MASK, MASK_ADJ_TO_MASK)

// Verify that tfBatchMask correctly rejects tfInnerBatchTxn.
// The outer Batch transaction must NOT have tfInnerBatchTxn set; only inner transactions should
// have it.
static_assert(
    (tfBatchMask & tfInnerBatchTxn) == tfInnerBatchTxn,
    "tfBatchMask must include tfInnerBatchTxn to reject it on outer Batch");

// Verify that other transaction masks correctly allow tfInnerBatchTxn.
// Inner transactions need tfInnerBatchTxn to be valid, so these masks must not reject it.
static_assert(
    (tfPaymentMask & tfInnerBatchTxn) == 0,
    "tfPaymentMask must not reject tfInnerBatchTxn");
static_assert(
    (tfAccountSetMask & tfInnerBatchTxn) == 0,
    "tfAccountSetMask must not reject tfInnerBatchTxn");

// Create getter functions for each set of flags using Meyer's singleton pattern.
// This avoids static initialization order fiasco while still providing efficient access.
// This is used below in `getAllTxFlags()` to generate the server_definitions RPC
// output.
//
// example:
// inline FlagMap const& getAccountSetFlags() {
//     static FlagMap const flags = {
//         {"tfRequireDestTag", 0x00010000},
//         {"tfOptionalDestTag", 0x00020000},
//     ...};
//     return flags;
// }
using FlagMap = std::map<std::string, FlagValue>;
#define VALUE_TO_MAP(name, value) {#name, value},
#define TO_MAP(name, values, maskAdj)          \
    inline FlagMap const& get##name##Flags()   \
    {                                          \
        static FlagMap const flags = {values}; \
        return flags;                          \
    }
XMACRO(TO_MAP, VALUE_TO_MAP, VALUE_TO_MAP, NULL_MASK_ADJ)

inline FlagMap const&
getUniversalFlags()
{
    static FlagMap const flags = {
        {"tfFullyCanonicalSig", tfFullyCanonicalSig}, {"tfInnerBatchTxn", tfInnerBatchTxn}};
    return flags;
}

// Create a getter function for all transaction flag maps using Meyer's singleton pattern.
// This is used to generate the server_definitions RPC output.
//
// example:
// inline FlagMapPairList const& getAllTxFlags() {
//     static FlagMapPairList const flags = {
//         {"AccountSet", getAccountSetFlags()},
//     ...};
//     return flags;
// }
using FlagMapPairList = std::vector<std::pair<std::string, FlagMap>>;
#define ALL_TX_FLAGS(name, values, maskAdj) {#name, get##name##Flags()},
inline FlagMapPairList const&
getAllTxFlags()
{
    static FlagMapPairList const flags = {
        {"universal", getUniversalFlags()},
        XMACRO(ALL_TX_FLAGS, NULL_OUTPUT, NULL_OUTPUT, NULL_MASK_ADJ)};
    return flags;
}

#undef XMACRO
#undef TO_VALUE
#undef VALUE_TO_MAP
#undef NULL_NAME
#undef NULL_OUTPUT
#undef TO_MAP
#undef TO_MASK
#undef VALUE_TO_MASK
#undef ALL_TX_FLAGS
#undef NULL_MASK_ADJ
#undef MASK_ADJ_TO_MASK

#pragma pop_macro("XMACRO")
#pragma pop_macro("TO_VALUE")
#pragma pop_macro("VALUE_TO_MAP")
#pragma pop_macro("NULL_NAME")
#pragma pop_macro("NULL_OUTPUT")
#pragma pop_macro("TO_MAP")
#pragma pop_macro("TO_MASK")
#pragma pop_macro("VALUE_TO_MASK")
#pragma pop_macro("ALL_TX_FLAGS")
#pragma pop_macro("NULL_MASK_ADJ")
#pragma pop_macro("MASK_ADJ_TO_MASK")

// Additional transaction masks and combos
inline constexpr FlagValue tfMPTPaymentMask = ~(tfUniversal | tfPartialPayment);
inline constexpr FlagValue tfTrustSetPermissionMask =
    ~(tfUniversal | tfSetfAuth | tfSetFreeze | tfClearFreeze);

// MPTokenIssuanceCreate MutableFlags:
// Indicating specific fields or flags may be changed after issuance.
inline constexpr FlagValue tmfMPTCanMutateCanLock = lsmfMPTCanMutateCanLock;
inline constexpr FlagValue tmfMPTCanMutateRequireAuth = lsmfMPTCanMutateRequireAuth;
inline constexpr FlagValue tmfMPTCanMutateCanEscrow = lsmfMPTCanMutateCanEscrow;
inline constexpr FlagValue tmfMPTCanMutateCanTrade = lsmfMPTCanMutateCanTrade;
inline constexpr FlagValue tmfMPTCanMutateCanTransfer = lsmfMPTCanMutateCanTransfer;
inline constexpr FlagValue tmfMPTCanMutateCanClawback = lsmfMPTCanMutateCanClawback;
inline constexpr FlagValue tmfMPTCanMutateMetadata = lsmfMPTCanMutateMetadata;
inline constexpr FlagValue tmfMPTCanMutateTransferFee = lsmfMPTCanMutateTransferFee;
inline constexpr FlagValue tmfMPTokenIssuanceCreateMutableMask =
    ~(tmfMPTCanMutateCanLock | tmfMPTCanMutateRequireAuth | tmfMPTCanMutateCanEscrow |
      tmfMPTCanMutateCanTrade | tmfMPTCanMutateCanTransfer | tmfMPTCanMutateCanClawback |
      tmfMPTCanMutateMetadata | tmfMPTCanMutateTransferFee);

// MPTokenIssuanceSet MutableFlags:
// Set or Clear flags.

inline constexpr FlagValue tmfMPTSetCanLock = 0x00000001;
inline constexpr FlagValue tmfMPTClearCanLock = 0x00000002;
inline constexpr FlagValue tmfMPTSetRequireAuth = 0x00000004;
inline constexpr FlagValue tmfMPTClearRequireAuth = 0x00000008;
inline constexpr FlagValue tmfMPTSetCanEscrow = 0x00000010;
inline constexpr FlagValue tmfMPTClearCanEscrow = 0x00000020;
inline constexpr FlagValue tmfMPTSetCanTrade = 0x00000040;
inline constexpr FlagValue tmfMPTClearCanTrade = 0x00000080;
inline constexpr FlagValue tmfMPTSetCanTransfer = 0x00000100;
inline constexpr FlagValue tmfMPTClearCanTransfer = 0x00000200;
inline constexpr FlagValue tmfMPTSetCanClawback = 0x00000400;
inline constexpr FlagValue tmfMPTClearCanClawback = 0x00000800;
inline constexpr FlagValue tmfMPTokenIssuanceSetMutableMask = ~(
    tmfMPTSetCanLock | tmfMPTClearCanLock | tmfMPTSetRequireAuth | tmfMPTClearRequireAuth |
    tmfMPTSetCanEscrow | tmfMPTClearCanEscrow | tmfMPTSetCanTrade | tmfMPTClearCanTrade |
    tmfMPTSetCanTransfer | tmfMPTClearCanTransfer | tmfMPTSetCanClawback | tmfMPTClearCanClawback);

// Prior to fixRemoveNFTokenAutoTrustLine, transfer of an NFToken between accounts allowed a
// TrustLine to be added to the issuer of that token without explicit permission from that issuer.
// This was enabled by minting the NFToken with the tfTrustLine flag set.
//
// That capability could be used to attack the NFToken issuer.
// It would be possible for two accounts to trade the NFToken back and forth building up any number
// of TrustLines on the issuer, increasing the issuer's reserve without bound.
//
// The fixRemoveNFTokenAutoTrustLine amendment disables minting with the tfTrustLine flag as a way
// to prevent the attack. But until the amendment passes we still need to keep the old behavior
// available.
inline constexpr FlagValue tfTrustLine = 0x00000004;  // needed for backwards compatibility
inline constexpr FlagValue tfNFTokenMintMaskWithoutMutable =
    ~(tfUniversal | tfBurnable | tfOnlyXRP | tfTransferable);

inline constexpr FlagValue tfNFTokenMintOldMask = ~(~tfNFTokenMintMaskWithoutMutable | tfTrustLine);

// if featureDynamicNFT enabled then new flag allowing mutable URI available.
inline constexpr FlagValue tfNFTokenMintOldMaskWithMutable = ~(~tfNFTokenMintOldMask | tfMutable);

inline constexpr FlagValue tfWithdrawSubTx = tfLPToken | tfSingleAsset | tfTwoAsset |
    tfOneAssetLPToken | tfLimitLPToken | tfWithdrawAll | tfOneAssetWithdrawAll;
inline constexpr FlagValue tfDepositSubTx =
    tfLPToken | tfSingleAsset | tfTwoAsset | tfOneAssetLPToken | tfLimitLPToken | tfTwoAssetIfEmpty;

#pragma push_macro("ACCOUNTSET_FLAGS")
#pragma push_macro("ACCOUNTSET_FLAG_TO_VALUE")
#pragma push_macro("ACCOUNTSET_FLAG_TO_MAP")

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
    /*  11 is reserved for Hooks amendment */     \
    /* ASF_FLAG(asfTshCollect, 11) */             \
    ASF_FLAG(asfDisallowIncomingNFTokenOffer, 12) \
    ASF_FLAG(asfDisallowIncomingCheck, 13)        \
    ASF_FLAG(asfDisallowIncomingPayChan, 14)      \
    ASF_FLAG(asfDisallowIncomingTrustline, 15)    \
    ASF_FLAG(asfAllowTrustLineClawback, 16)       \
    ASF_FLAG(asfAllowTrustLineLocking, 17)

#define ACCOUNTSET_FLAG_TO_VALUE(name, value) inline constexpr FlagValue name = value;
#define ACCOUNTSET_FLAG_TO_MAP(name, value) {#name, value},

ACCOUNTSET_FLAGS(ACCOUNTSET_FLAG_TO_VALUE)

inline std::map<std::string, FlagValue> const&
getAsfFlagMap()
{
    static std::map<std::string, FlagValue> const flags = {
        ACCOUNTSET_FLAGS(ACCOUNTSET_FLAG_TO_MAP)};
    return flags;
}

#undef ACCOUNTSET_FLAG_TO_VALUE
#undef ACCOUNTSET_FLAG_TO_MAP
#undef ACCOUNTSET_FLAGS

#pragma pop_macro("ACCOUNTSET_FLAG_TO_VALUE")
#pragma pop_macro("ACCOUNTSET_FLAG_TO_MAP")
#pragma pop_macro("ACCOUNTSET_FLAGS")

}  // namespace xrpl
