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
inline constexpr FlagValue kTF_FULLY_CANONICAL_SIG = 0x80000000;
inline constexpr FlagValue kTF_INNER_BATCH_TXN = 0x40000000;
inline constexpr FlagValue tfUniversal = kTF_FULLY_CANONICAL_SIG | kTF_INNER_BATCH_TXN;
inline constexpr FlagValue kTF_UNIVERSAL_MASK = ~tfUniversal;

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
        TF_FLAG(kTF_REQUIRE_DEST_TAG, 0x00010000)                                                                                                                  \
        TF_FLAG(kTF_OPTIONAL_DEST_TAG, 0x00020000)                                                                                                                 \
        TF_FLAG(kTF_REQUIRE_AUTH, 0x00040000)                                                                                                                     \
        TF_FLAG(kTF_OPTIONAL_AUTH, 0x00080000)                                                                                                                    \
        TF_FLAG(kTF_DISALLOW_XRP, 0x00100000)                                                                                                                     \
        TF_FLAG(kTF_ALLOW_XRP, 0x00200000),                                                                                                                       \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(OfferCreate,                                                                                                                                   \
        TF_FLAG(kTF_PASSIVE, 0x00010000)                                                                                                                         \
        TF_FLAG(kTF_IMMEDIATE_OR_CANCEL, 0x00020000)                                                                                                               \
        TF_FLAG(kTF_FILL_OR_KILL, 0x00040000)                                                                                                                      \
        TF_FLAG(kTF_SELL, 0x00080000)                                                                                                                            \
        TF_FLAG(kTF_HYBRID, 0x00100000),                                                                                                                         \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(Payment,                                                                                                                                       \
        TF_FLAG(kTF_NO_RIPPLE_DIRECT, 0x00010000)                                                                                                                  \
        TF_FLAG(kTF_PARTIAL_PAYMENT, 0x00020000)                                                                                                                  \
        TF_FLAG(kTF_LIMIT_QUALITY, 0x00040000),                                                                                                                   \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(TrustSet,                                                                                                                                      \
        TF_FLAG(kTF_SETF_AUTH, 0x00010000)                                                                                                                        \
        TF_FLAG(kTF_SET_NO_RIPPLE, 0x00020000)                                                                                                                     \
        TF_FLAG(kTF_CLEAR_NO_RIPPLE, 0x00040000)                                                                                                                   \
        TF_FLAG(kTF_SET_FREEZE, 0x00100000)                                                                                                                       \
        TF_FLAG(kTF_CLEAR_FREEZE, 0x00200000)                                                                                                                     \
        TF_FLAG(kTF_SET_DEEP_FREEZE, 0x00400000)                                                                                                                   \
        TF_FLAG(kTF_CLEAR_DEEP_FREEZE, 0x00800000),                                                                                                                \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(EnableAmendment,                                                                                                                               \
        TF_FLAG(kTF_GOT_MAJORITY, 0x00010000)                                                                                                                     \
        TF_FLAG(kTF_LOST_MAJORITY, 0x00020000),                                                                                                                   \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(PaymentChannelClaim,                                                                                                                           \
        TF_FLAG(kTF_RENEW, 0x00010000)                                                                                                                           \
        TF_FLAG(kTF_CLOSE, 0x00020000),                                                                                                                          \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(NFTokenMint,                                                                                                                                   \
        TF_FLAG(kTF_BURNABLE, 0x00000001)                                                                                                                        \
        TF_FLAG(kTF_ONLY_XRP, 0x00000002)                                                                                                                         \
        /* deprecated TF_FLAG(tfTrustLine, 0x00000004) */                                                                                                      \
        TF_FLAG(kTF_TRANSFERABLE, 0x00000008)                                                                                                                    \
        TF_FLAG(kTF_MUTABLE, 0x00000010),                                                                                                                        \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(MPTokenIssuanceCreate,                                                                                                                         \
        /* Note: tf/lsfMPTLocked is intentionally omitted since this transaction is not allowed to modify it. */                                               \
        TF_FLAG(kTF_MPT_CAN_LOCK, LsfMptCanLock)                                                                                                                   \
        TF_FLAG(kTF_MPT_REQUIRE_AUTH, LsfMptRequireAuth)                                                                                                           \
        TF_FLAG(kTF_MPT_CAN_ESCROW, LsfMptCanEscrow)                                                                                                               \
        TF_FLAG(kTF_MPT_CAN_TRADE, LsfMptCanTrade)                                                                                                                 \
        TF_FLAG(kTF_MPT_CAN_TRANSFER, LsfMptCanTransfer)                                                                                                           \
        TF_FLAG(kTF_MPT_CAN_CLAWBACK, LsfMptCanClawback),                                                                                                          \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(MPTokenAuthorize,                                                                                                                              \
        TF_FLAG(kTF_MPT_UNAUTHORIZE, 0x00000001),                                                                                                                 \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(MPTokenIssuanceSet,                                                                                                                            \
        TF_FLAG(kTF_MPT_LOCK, 0x00000001)                                                                                                                         \
        TF_FLAG(kTF_MPT_UNLOCK, 0x00000002),                                                                                                                      \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(NFTokenCreateOffer,                                                                                                                            \
        TF_FLAG(kTF_SELL_NF_TOKEN, 0x00000001),                                                                                                                    \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(AMMDeposit,                                                                                                                                    \
        TF_FLAG(kTF_LP_TOKEN, 0x00010000)                                                                                                                         \
        TF_FLAG(kTF_SINGLE_ASSET, 0x00080000)                                                                                                                     \
        TF_FLAG(kTF_TWO_ASSET, 0x00100000)                                                                                                                        \
        TF_FLAG(kTF_ONE_ASSET_LP_TOKEN, 0x00200000)                                                                                                                 \
        TF_FLAG(kTF_LIMIT_LP_TOKEN, 0x00400000)                                                                                                                    \
        TF_FLAG(kTF_TWO_ASSET_IF_EMPTY, 0x00800000),                                                                                                                \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(AMMWithdraw,                                                                                                                                   \
        TF_FLAG2(kTF_LP_TOKEN, 0x00010000)                                                                                                                        \
        TF_FLAG(kTF_WITHDRAW_ALL, 0x00020000)                                                                                                                     \
        TF_FLAG(kTF_ONE_ASSET_WITHDRAW_ALL, 0x00040000)                                                                                                             \
        TF_FLAG2(kTF_SINGLE_ASSET, 0x00080000)                                                                                                                    \
        TF_FLAG2(kTF_TWO_ASSET, 0x00100000)                                                                                                                       \
        TF_FLAG2(kTF_ONE_ASSET_LP_TOKEN, 0x00200000)                                                                                                                \
        TF_FLAG2(kTF_LIMIT_LP_TOKEN, 0x00400000),                                                                                                                  \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(AMMClawback,                                                                                                                                   \
        TF_FLAG(kTF_CLAW_TWO_ASSETS, 0x00000001),                                                                                                                  \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(XChainModifyBridge,                                                                                                                            \
        TF_FLAG(kTF_CLEAR_ACCOUNT_CREATE_AMOUNT, 0x00010000),                                                                                                       \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(VaultCreate,                                                                                                                                   \
        TF_FLAG(kTF_VAULT_PRIVATE, LsfVaultPrivate)                                                                                                               \
        TF_FLAG(kTF_VAULT_SHARE_NON_TRANSFERABLE, 0x00020000),                                                                                                      \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(Batch,                                                                                                                                         \
        TF_FLAG(kTF_ALL_OR_NOTHING, 0x00010000)                                                                                                                    \
        TF_FLAG(kTF_ONLY_ONE, 0x00020000)                                                                                                                         \
        TF_FLAG(kTF_UNTIL_FAILURE, 0x00040000)                                                                                                                    \
        TF_FLAG(kTF_INDEPENDENT, 0x00080000),                                                                                                                    \
        MASK_ADJ(kTF_INNER_BATCH_TXN))                      /* Batch must reject tfInnerBatchTxn - only inner transactions should have this flag */                \
                                                                                                                                                               \
    TRANSACTION(LoanSet,                                /* True indicates the loan supports overpayments */                                                    \
        TF_FLAG(kTF_LOAN_OVERPAYMENT, 0x00010000),                                                                                                                \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(LoanPay,                                /* True indicates any excess in this payment can be used as an overpayment. */                         \
                                                        /* False: no overpayments will be taken. */                                                            \
        TF_FLAG2(kTF_LOAN_OVERPAYMENT, 0x00010000)                                                                                                                \
        TF_FLAG(kTF_LOAN_FULL_PAYMENT, 0x00020000)          /* True indicates that the payment is an early full payment. */                                        \
                                                        /* It must pay the entire loan including close interest and fees, or it will fail. */                  \
                                                        /* False: Not a full payment. */                                                                       \
        TF_FLAG(kTF_LOAN_LATE_PAYMENT, 0x00040000),         /* True indicates that the payment is late, and includes late interest and fees. */                    \
                                                        /* If the loan is not late, it will fail. */                                                           \
                                                        /* False: not a late payment. If the current payment is overdue, the transaction will fail.*/          \
        MASK_ADJ(0))                                                                                                                                           \
                                                                                                                                                               \
    TRANSACTION(LoanManage,                                                                                                                                    \
        TF_FLAG(kTF_LOAN_DEFAULT, 0x00010000)                                                                                                                     \
        TF_FLAG(kTF_LOAN_IMPAIR, 0x00020000)                                                                                                                      \
        TF_FLAG(kTF_LOAN_UNIMPAIR, 0x00040000),                                                                                                                   \
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
    (tfBatchMask & kTF_INNER_BATCH_TXN) == kTF_INNER_BATCH_TXN,
    "tfBatchMask must include tfInnerBatchTxn to reject it on outer Batch");

// Verify that other transaction masks correctly allow tfInnerBatchTxn.
// Inner transactions need tfInnerBatchTxn to be valid, so these masks must not reject it.
static_assert(
    (tfPaymentMask & kTF_INNER_BATCH_TXN) == 0,
    "tfPaymentMask must not reject tfInnerBatchTxn");
static_assert(
    (tfAccountSetMask & kTF_INNER_BATCH_TXN) == 0,
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
    static FlagMap const kFLAGS = {
        {"tfFullyCanonicalSig", kTF_FULLY_CANONICAL_SIG}, {"tfInnerBatchTxn", kTF_INNER_BATCH_TXN}};
    return kFLAGS;
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
    static FlagMapPairList const kFLAGS = {
        {"universal", getUniversalFlags()},
        XMACRO(ALL_TX_FLAGS, NULL_OUTPUT, NULL_OUTPUT, NULL_MASK_ADJ)};
    return kFLAGS;
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
inline constexpr FlagValue kTF_MPT_PAYMENT_MASK = ~(tfUniversal | kTF_PARTIAL_PAYMENT);
inline constexpr FlagValue kTF_TRUST_SET_PERMISSION_MASK =
    ~(tfUniversal | kTF_SETF_AUTH | kTF_SET_FREEZE | kTF_CLEAR_FREEZE);

// MPTokenIssuanceCreate MutableFlags:
// Indicating specific fields or flags may be changed after issuance.
inline constexpr FlagValue kTMF_MPT_CAN_MUTATE_CAN_LOCK = LsmfMptCanMutateCanLock;
inline constexpr FlagValue kTMF_MPT_CAN_MUTATE_REQUIRE_AUTH = LsmfMptCanMutateRequireAuth;
inline constexpr FlagValue kTMF_MPT_CAN_MUTATE_CAN_ESCROW = LsmfMptCanMutateCanEscrow;
inline constexpr FlagValue kTMF_MPT_CAN_MUTATE_CAN_TRADE = LsmfMptCanMutateCanTrade;
inline constexpr FlagValue kTMF_MPT_CAN_MUTATE_CAN_TRANSFER = LsmfMptCanMutateCanTransfer;
inline constexpr FlagValue kTMF_MPT_CAN_MUTATE_CAN_CLAWBACK = LsmfMptCanMutateCanClawback;
inline constexpr FlagValue kTMF_MPT_CAN_MUTATE_METADATA = LsmfMptCanMutateMetadata;
inline constexpr FlagValue kTMF_MPT_CAN_MUTATE_TRANSFER_FEE = LsmfMptCanMutateTransferFee;
inline constexpr FlagValue kTMF_MP_TOKEN_ISSUANCE_CREATE_MUTABLE_MASK =
    ~(kTMF_MPT_CAN_MUTATE_CAN_LOCK | kTMF_MPT_CAN_MUTATE_REQUIRE_AUTH |
      kTMF_MPT_CAN_MUTATE_CAN_ESCROW | kTMF_MPT_CAN_MUTATE_CAN_TRADE |
      kTMF_MPT_CAN_MUTATE_CAN_TRANSFER | kTMF_MPT_CAN_MUTATE_CAN_CLAWBACK |
      kTMF_MPT_CAN_MUTATE_METADATA | kTMF_MPT_CAN_MUTATE_TRANSFER_FEE);

// MPTokenIssuanceSet MutableFlags:
// Set or Clear flags.

inline constexpr FlagValue kTMF_MPT_SET_CAN_LOCK = 0x00000001;
inline constexpr FlagValue kTMF_MPT_CLEAR_CAN_LOCK = 0x00000002;
inline constexpr FlagValue kTMF_MPT_SET_REQUIRE_AUTH = 0x00000004;
inline constexpr FlagValue kTMF_MPT_CLEAR_REQUIRE_AUTH = 0x00000008;
inline constexpr FlagValue kTMF_MPT_SET_CAN_ESCROW = 0x00000010;
inline constexpr FlagValue kTMF_MPT_CLEAR_CAN_ESCROW = 0x00000020;
inline constexpr FlagValue kTMF_MPT_SET_CAN_TRADE = 0x00000040;
inline constexpr FlagValue kTMF_MPT_CLEAR_CAN_TRADE = 0x00000080;
inline constexpr FlagValue kTMF_MPT_SET_CAN_TRANSFER = 0x00000100;
inline constexpr FlagValue kTMF_MPT_CLEAR_CAN_TRANSFER = 0x00000200;
inline constexpr FlagValue kTMF_MPT_SET_CAN_CLAWBACK = 0x00000400;
inline constexpr FlagValue kTMF_MPT_CLEAR_CAN_CLAWBACK = 0x00000800;
inline constexpr FlagValue kTMF_MP_TOKEN_ISSUANCE_SET_MUTABLE_MASK =
    ~(kTMF_MPT_SET_CAN_LOCK | kTMF_MPT_CLEAR_CAN_LOCK | kTMF_MPT_SET_REQUIRE_AUTH |
      kTMF_MPT_CLEAR_REQUIRE_AUTH | kTMF_MPT_SET_CAN_ESCROW | kTMF_MPT_CLEAR_CAN_ESCROW |
      kTMF_MPT_SET_CAN_TRADE | kTMF_MPT_CLEAR_CAN_TRADE | kTMF_MPT_SET_CAN_TRANSFER |
      kTMF_MPT_CLEAR_CAN_TRANSFER | kTMF_MPT_SET_CAN_CLAWBACK | kTMF_MPT_CLEAR_CAN_CLAWBACK);

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
inline constexpr FlagValue kTF_TRUST_LINE = 0x00000004;  // needed for backwards compatibility
inline constexpr FlagValue kTF_NF_TOKEN_MINT_MASK_WITHOUT_MUTABLE =
    ~(tfUniversal | kTF_BURNABLE | kTF_ONLY_XRP | kTF_TRANSFERABLE);

inline constexpr FlagValue kTF_NF_TOKEN_MINT_OLD_MASK =
    ~(~kTF_NF_TOKEN_MINT_MASK_WITHOUT_MUTABLE | kTF_TRUST_LINE);

// if featureDynamicNFT enabled then new flag allowing mutable URI available.
inline constexpr FlagValue kTF_NF_TOKEN_MINT_OLD_MASK_WITH_MUTABLE =
    ~(~kTF_NF_TOKEN_MINT_OLD_MASK | kTF_MUTABLE);

inline constexpr FlagValue kTF_WITHDRAW_SUB_TX = kTF_LP_TOKEN | kTF_SINGLE_ASSET | kTF_TWO_ASSET |
    kTF_ONE_ASSET_LP_TOKEN | kTF_LIMIT_LP_TOKEN | kTF_WITHDRAW_ALL | kTF_ONE_ASSET_WITHDRAW_ALL;
inline constexpr FlagValue kTF_DEPOSIT_SUB_TX = kTF_LP_TOKEN | kTF_SINGLE_ASSET | kTF_TWO_ASSET |
    kTF_ONE_ASSET_LP_TOKEN | kTF_LIMIT_LP_TOKEN | kTF_TWO_ASSET_IF_EMPTY;

#pragma push_macro("ACCOUNTSET_FLAGS")
#pragma push_macro("ACCOUNTSET_FLAG_TO_VALUE")
#pragma push_macro("ACCOUNTSET_FLAG_TO_MAP")

// AccountSet SetFlag/ClearFlag values
#define ACCOUNTSET_FLAGS(ASF_FLAG)                      \
    ASF_FLAG(kASF_REQUIRE_DEST, 1)                      \
    ASF_FLAG(kASF_REQUIRE_AUTH, 2)                      \
    ASF_FLAG(kASF_DISALLOW_XRP, 3)                      \
    ASF_FLAG(kASF_DISABLE_MASTER, 4)                    \
    ASF_FLAG(kASF_ACCOUNT_TXN_ID, 5)                    \
    ASF_FLAG(kASF_NO_FREEZE, 6)                         \
    ASF_FLAG(kASF_GLOBAL_FREEZE, 7)                     \
    ASF_FLAG(kASF_DEFAULT_RIPPLE, 8)                    \
    ASF_FLAG(kASF_DEPOSIT_AUTH, 9)                      \
    ASF_FLAG(kASF_AUTHORIZED_NF_TOKEN_MINTER, 10)       \
    /*  11 is reserved for Hooks amendment */           \
    /* ASF_FLAG(asfTshCollect, 11) */                   \
    ASF_FLAG(kASF_DISALLOW_INCOMING_NF_TOKEN_OFFER, 12) \
    ASF_FLAG(kASF_DISALLOW_INCOMING_CHECK, 13)          \
    ASF_FLAG(kASF_DISALLOW_INCOMING_PAY_CHAN, 14)       \
    ASF_FLAG(kASF_DISALLOW_INCOMING_TRUSTLINE, 15)      \
    ASF_FLAG(kASF_ALLOW_TRUST_LINE_CLAWBACK, 16)        \
    ASF_FLAG(kASF_ALLOW_TRUST_LINE_LOCKING, 17)

#define ACCOUNTSET_FLAG_TO_VALUE(name, value) inline constexpr FlagValue name = value;
#define ACCOUNTSET_FLAG_TO_MAP(name, value) {#name, value},

ACCOUNTSET_FLAGS(ACCOUNTSET_FLAG_TO_VALUE)

inline std::map<std::string, FlagValue> const&
getAsfFlagMap()
{
    static std::map<std::string, FlagValue> const kFLAGS = {
        ACCOUNTSET_FLAGS(ACCOUNTSET_FLAG_TO_MAP)};
    return kFLAGS;
}

#undef ACCOUNTSET_FLAG_TO_VALUE
#undef ACCOUNTSET_FLAG_TO_MAP
#undef ACCOUNTSET_FLAGS

#pragma pop_macro("ACCOUNTSET_FLAG_TO_VALUE")
#pragma pop_macro("ACCOUNTSET_FLAG_TO_MAP")
#pragma pop_macro("ACCOUNTSET_FLAGS")

}  // namespace xrpl
