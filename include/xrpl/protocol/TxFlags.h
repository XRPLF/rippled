/** @file
 *  Canonical, single-source-of-truth definitions for every transaction flag in
 *  the XRPL protocol.
 *
 *  Flag values are embedded in signed transactions and therefore form part of
 *  the consensus protocol. Altering any constant without a coordinated amendment
 *  and special handling will cause a hard fork.
 *
 *  The file uses three X-macro instantiations of a single `XMACRO` table to
 *  emit, from one authoritative list, the flag value constants, the per-type
 *  validation masks, and the Meyer's-singleton getter functions consumed by the
 *  `server_definitions` RPC endpoint.
 */

#pragma once

// NOLINTBEGIN(readability-identifier-naming)

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

/** Underlying integer type for all transaction flag bitmasks. */
using FlagValue = std::uint32_t;

// --- Universal Transaction flags ---

/** Require that the transaction signature use the canonical (low-S) ECDSA
 *  form.  The network now enforces this unconditionally, but the flag must
 *  remain defined so that historical transactions that set it remain valid. */
inline constexpr FlagValue tfFullyCanonicalSig = 0x80000000;

/** Marks a transaction as an inner member of a `Batch` transaction.
 *
 *  Set by the batch submitter on every inner transaction; the outer `Batch`
 *  wrapper must NOT carry this flag (enforced by `tfBatchMask` and the
 *  compile-time `static_assert` below). */
inline constexpr FlagValue tfInnerBatchTxn = 0x40000000;

/** Bitwise OR of all universal flags; occupies the high 8 bits of `Flags`. */
inline constexpr FlagValue tfUniversal = tfFullyCanonicalSig | tfInnerBatchTxn;

/** Complement of `tfUniversal`; ANDing an unknown `Flags` value with this mask
 *  isolates any transaction-type-specific bits. */
inline constexpr FlagValue tfUniversalMask = ~tfUniversal;

// The push/pop guards protect any caller that has its own macros with the same
// short names (XMACRO, TO_VALUE, etc.) from having them clobbered when this
// header is included.
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

/** Master X-macro table of all per-transaction-type flag groups.
 *
 *  This macro is the single source of truth for every flag in the system.
 *  It is instantiated three times with different argument bindings to produce:
 *    1. Inline `constexpr FlagValue tf*` declarations.
 *    2. Inline `constexpr FlagValue tf*Mask` validation masks.
 *    3. `inline FlagMap const& get*Flags()` Meyer's-singleton getters consumed
 *       by the `server_definitions` RPC endpoint.
 *
 *  @param TRANSACTION  Macro invoked once per transaction type; receives the
 *      type name, the expansion of its flag list, and the mask adjustment.
 *  @param TF_FLAG      Declares a new flag constant unique to this transaction
 *      type (or the first transaction that defines a shared constant).
 *  @param TF_FLAG2     References an already-declared flag constant; suppresses
 *      redeclaration. Used when two transaction types share a numeric value
 *      (e.g., `tfLPToken` is declared by `AMMDeposit` and referenced by
 *      `AMMWithdraw`).
 *  @param MASK_ADJ     Specifies additional bits to OR back into the generated
 *      mask, making those bits invalid for this transaction type even though
 *      they are otherwise universal. `Batch` uses `MASK_ADJ(tfInnerBatchTxn)`
 *      because the outer wrapper must not carry that flag; all other entries
 *      use `MASK_ADJ(0)`.
 *
 *  @note To add a new flag: add a `TF_FLAG(name, value)` row inside the
 *      appropriate `TRANSACTION(...)` block and nowhere else. The value,
 *      mask, and getter are all derived automatically.
 *
 *  @warning Flag values are protocol-stable. Changing or reusing a numeric
 *      value without an amendment causes a hard fork.
 */
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

// --- Instantiation 1: emit `inline constexpr FlagValue tf* = 0x...;` ---
// TF_FLAG  → declares a new constant.
// TF_FLAG2 → no-op (constant already declared by a prior TRANSACTION block).
// Example output:
//   inline constexpr FlagValue tfRequireDestTag = 0x00010000;
#define TO_VALUE(name, value) inline constexpr FlagValue name = value;
#define NULL_NAME(name, values, maskAdj) values
#define NULL_OUTPUT(name, value)
#define NULL_MASK_ADJ(value)
XMACRO(NULL_NAME, TO_VALUE, NULL_OUTPUT, NULL_MASK_ADJ)

// --- Instantiation 2: emit `inline constexpr FlagValue tf*Mask` ---
// Each mask is the bitwise complement of (tfUniversal | all valid flags for
// this tx type) OR'd with any MASK_ADJ bits.  A transaction whose `Flags`
// field ANDed with the mask is non-zero is rejected as carrying unknown or
// forbidden flags.
// Example output:
//   inline constexpr FlagValue tfAccountSetMask =
//       ~(tfUniversal | tfRequireDestTag | tfOptionalDestTag | ...);
#define TO_MASK(name, values, maskAdj) \
    inline constexpr FlagValue tf##name##Mask = ~(tfUniversal values) | (maskAdj);
#define VALUE_TO_MASK(name, value) | name
#define MASK_ADJ_TO_MASK(value) value
XMACRO(TO_MASK, VALUE_TO_MASK, VALUE_TO_MASK, MASK_ADJ_TO_MASK)

// Compile-time invariants for the MASK_ADJ(tfInnerBatchTxn) mechanism:
// The outer Batch transaction rejects tfInnerBatchTxn (bit must appear in its
// mask); all other transaction types allow it so inner transactions can carry
// the flag legally.
static_assert(
    (tfBatchMask & tfInnerBatchTxn) == tfInnerBatchTxn,
    "tfBatchMask must include tfInnerBatchTxn to reject it on outer Batch");
static_assert(
    (tfPaymentMask & tfInnerBatchTxn) == 0,
    "tfPaymentMask must not reject tfInnerBatchTxn");
static_assert(
    (tfAccountSetMask & tfInnerBatchTxn) == 0,
    "tfAccountSetMask must not reject tfInnerBatchTxn");

// --- Instantiation 3: emit `inline FlagMap const& get*Flags()` getters ---
// Each function initialises a local static on first call (Meyer's singleton)
// and returns a reference to it on every subsequent call.  The map is keyed
// by flag name string and valued by the numeric FlagValue.  These are
// aggregated by getAllTxFlags() and served to clients via server_definitions.

/** Maps flag names to their numeric values for a single transaction type. */
using FlagMap = std::map<std::string, FlagValue>;
#define VALUE_TO_MAP(name, value) {#name, value},
#define TO_MAP(name, values, maskAdj)          \
    inline FlagMap const& get##name##Flags()   \
    {                                          \
        static FlagMap const flags = {values}; \
        return flags;                          \
    }
XMACRO(TO_MAP, VALUE_TO_MAP, VALUE_TO_MAP, NULL_MASK_ADJ)

/** Returns the universal transaction flags by name.
 *
 *  The returned map contains `tfFullyCanonicalSig` and `tfInnerBatchTxn`.
 *  It is initialised once (Meyer's singleton) and safe to call from any
 *  thread after static initialisation completes.
 *
 *  @return A reference to the singleton `FlagMap` for universal flags.
 */
inline FlagMap const&
getUniversalFlags()
{
    static FlagMap const flags = {
        {"tfFullyCanonicalSig", tfFullyCanonicalSig}, {"tfInnerBatchTxn", tfInnerBatchTxn}};
    return flags;
}

/** Ordered list of `{transaction-type-name, FlagMap}` pairs covering every
 *  transaction type and the universal flag group.  Consumed by the
 *  `server_definitions` RPC endpoint so clients can discover the protocol's
 *  flag vocabulary at runtime. */
using FlagMapPairList = std::vector<std::pair<std::string, FlagMap>>;
#define ALL_TX_FLAGS(name, values, maskAdj) {#name, get##name##Flags()},

/** Returns all per-transaction-type flag maps, prefixed by the universal group.
 *
 *  Initialised once (Meyer's singleton). The first entry is always
 *  `{"universal", getUniversalFlags()}`; subsequent entries follow the
 *  declaration order in `XMACRO`.
 *
 *  @return A reference to the singleton `FlagMapPairList`.
 */
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

// --- Additional composite masks ---

/** Validation mask for `Payment` transactions that involve MPTokens.
 *
 *  MPToken payments support only `tfPartialPayment`; all other
 *  transaction-type-specific bits are rejected. */
inline constexpr FlagValue tfMPTPaymentMask = ~(tfUniversal | tfPartialPayment);

/** Validation mask for `TrustSet` transactions submitted under a granular
 *  delegation permission.
 *
 *  Only `tfSetfAuth`, `tfSetFreeze`, and `tfClearFreeze` are permitted when
 *  the `TrustlineUnfreeze` permission applies; any other flags cause the
 *  transactor to return `terNO_DELEGATE_PERMISSION`. */
inline constexpr FlagValue tfTrustSetPermissionMask =
    ~(tfUniversal | tfSetfAuth | tfSetFreeze | tfClearFreeze);

// --- MPTokenIssuanceCreate mutable-flag declarations (tmf* prefix) ---
// These alias the corresponding lsmf* ledger-state mutable-flag values from
// LedgerFormats.h so that the same numeric bit can be stored verbatim on the
// MPTokenIssuance object without a translation step.  Each flag, when set on
// the creation transaction, means the named property may be updated by a
// subsequent MPTokenIssuanceSet transaction.

/** Permits the `CanLock` property to be changed after issuance. */
inline constexpr FlagValue tmfMPTCanMutateCanLock = lsmfMPTCanMutateCanLock;
/** Permits the `RequireAuth` property to be changed after issuance. */
inline constexpr FlagValue tmfMPTCanMutateRequireAuth = lsmfMPTCanMutateRequireAuth;
/** Permits the `CanEscrow` property to be changed after issuance. */
inline constexpr FlagValue tmfMPTCanMutateCanEscrow = lsmfMPTCanMutateCanEscrow;
/** Permits the `CanTrade` property to be changed after issuance. */
inline constexpr FlagValue tmfMPTCanMutateCanTrade = lsmfMPTCanMutateCanTrade;
/** Permits the `CanTransfer` property to be changed after issuance. */
inline constexpr FlagValue tmfMPTCanMutateCanTransfer = lsmfMPTCanMutateCanTransfer;
/** Permits the `CanClawback` property to be changed after issuance. */
inline constexpr FlagValue tmfMPTCanMutateCanClawback = lsmfMPTCanMutateCanClawback;
/** Permits the metadata URI to be changed after issuance. */
inline constexpr FlagValue tmfMPTCanMutateMetadata = lsmfMPTCanMutateMetadata;
/** Permits the transfer fee to be changed after issuance. */
inline constexpr FlagValue tmfMPTCanMutateTransferFee = lsmfMPTCanMutateTransferFee;

/** Validation mask for the `MutableFlags` field of `MPTokenIssuanceCreate`.
 *
 *  Any bit outside the recognised `tmfMPTCanMutate*` set causes
 *  `MPTokenIssuanceCreate::preflight` to return `temINVALID_FLAG`.
 *  A value of zero is also rejected — at least one mutable property must be
 *  declared. */
inline constexpr FlagValue tmfMPTokenIssuanceCreateMutableMask =
    ~(tmfMPTCanMutateCanLock | tmfMPTCanMutateRequireAuth | tmfMPTCanMutateCanEscrow |
      tmfMPTCanMutateCanTrade | tmfMPTCanMutateCanTransfer | tmfMPTCanMutateCanClawback |
      tmfMPTCanMutateMetadata | tmfMPTCanMutateTransferFee);

// --- MPTokenIssuanceSet mutable-flag Set/Clear pairs ---
// Each property has two complementary flags: one to enable and one to disable
// the property in an existing MPTokenIssuance object.  Setting both bits in the
// same transaction is a logical error and is rejected by the transactor.

/** Enable the `CanLock` property on an existing MPTokenIssuance. */
inline constexpr FlagValue tmfMPTSetCanLock = 0x00000001;
/** Disable the `CanLock` property on an existing MPTokenIssuance. */
inline constexpr FlagValue tmfMPTClearCanLock = 0x00000002;
/** Enable the `RequireAuth` property on an existing MPTokenIssuance. */
inline constexpr FlagValue tmfMPTSetRequireAuth = 0x00000004;
/** Disable the `RequireAuth` property on an existing MPTokenIssuance. */
inline constexpr FlagValue tmfMPTClearRequireAuth = 0x00000008;
/** Enable the `CanEscrow` property on an existing MPTokenIssuance. */
inline constexpr FlagValue tmfMPTSetCanEscrow = 0x00000010;
/** Disable the `CanEscrow` property on an existing MPTokenIssuance. */
inline constexpr FlagValue tmfMPTClearCanEscrow = 0x00000020;
/** Enable the `CanTrade` property on an existing MPTokenIssuance. */
inline constexpr FlagValue tmfMPTSetCanTrade = 0x00000040;
/** Disable the `CanTrade` property on an existing MPTokenIssuance. */
inline constexpr FlagValue tmfMPTClearCanTrade = 0x00000080;
/** Enable the `CanTransfer` property on an existing MPTokenIssuance. */
inline constexpr FlagValue tmfMPTSetCanTransfer = 0x00000100;
/** Disable the `CanTransfer` property on an existing MPTokenIssuance. */
inline constexpr FlagValue tmfMPTClearCanTransfer = 0x00000200;
/** Enable the `CanClawback` property on an existing MPTokenIssuance. */
inline constexpr FlagValue tmfMPTSetCanClawback = 0x00000400;
/** Disable the `CanClawback` property on an existing MPTokenIssuance. */
inline constexpr FlagValue tmfMPTClearCanClawback = 0x00000800;

/** Validation mask for the `MutableFlags` field of `MPTokenIssuanceSet`.
 *
 *  Any bit outside the recognised `tmfMPTSet*` / `tmfMPTClear*` set causes
 *  `MPTokenIssuanceSet::preflight` to return `temINVALID_FLAG`.
 *  A zero value is also rejected. */
inline constexpr FlagValue tmfMPTokenIssuanceSetMutableMask = ~(
    tmfMPTSetCanLock | tmfMPTClearCanLock | tmfMPTSetRequireAuth | tmfMPTClearRequireAuth |
    tmfMPTSetCanEscrow | tmfMPTClearCanEscrow | tmfMPTSetCanTrade | tmfMPTClearCanTrade |
    tmfMPTSetCanTransfer | tmfMPTClearCanTransfer | tmfMPTSetCanClawback | tmfMPTClearCanClawback);

// --- NFTokenMint backward-compatibility mask variants ---
// Three mask variants exist to accommodate two amendment-gated changes to the
// set of valid NFTokenMint flags:
//
//   1. fixRemoveNFTokenAutoTrustLine — closed a reserve-exhaustion attack where
//      two accounts could endlessly trade an NFToken, forcing unbounded trust
//      lines onto the issuer. After this amendment, tfTrustLine is forbidden.
//   2. featureDynamicNFT — adds tfMutable, allowing the token URI to be updated
//      after minting.
//
// Nodes processing historical ledger data must still accept tfTrustLine on pre-
// amendment mints, which is why the constant and the old mask remain defined.

/** NFTokenMint flag that once allowed automatic trust-line creation on the
 *  issuer.  Forbidden after the `fixRemoveNFTokenAutoTrustLine` amendment;
 *  retained only for historical ledger replay. */
inline constexpr FlagValue tfTrustLine = 0x00000004;

/** Baseline `NFTokenMint` validation mask (post `fixRemoveNFTokenAutoTrustLine`,
 *  pre `featureDynamicNFT`).  Rejects `tfTrustLine` and `tfMutable`. */
inline constexpr FlagValue tfNFTokenMintMaskWithoutMutable =
    ~(tfUniversal | tfBurnable | tfOnlyXRP | tfTransferable);

/** `NFTokenMint` validation mask for ledgers before `fixRemoveNFTokenAutoTrustLine`.
 *  Allows `tfTrustLine` in addition to the standard flags. */
inline constexpr FlagValue tfNFTokenMintOldMask = ~(~tfNFTokenMintMaskWithoutMutable | tfTrustLine);

/** `NFTokenMint` validation mask for ledgers before `fixRemoveNFTokenAutoTrustLine`
 *  but after `featureDynamicNFT` — allows both `tfTrustLine` and `tfMutable`. */
inline constexpr FlagValue tfNFTokenMintOldMaskWithMutable = ~(~tfNFTokenMintOldMask | tfMutable);

/** Union of all mutually-exclusive `AMMWithdraw` mode flags.
 *
 *  The transactor checks `std::popcount(flags & tfWithdrawSubTx) == 1` to
 *  ensure exactly one withdrawal mode is selected; zero or more than one
 *  causes `temMALFORMED`. */
inline constexpr FlagValue tfWithdrawSubTx = tfLPToken | tfSingleAsset | tfTwoAsset |
    tfOneAssetLPToken | tfLimitLPToken | tfWithdrawAll | tfOneAssetWithdrawAll;

/** Union of all mutually-exclusive `AMMDeposit` mode flags.
 *
 *  The transactor checks `std::popcount(flags & tfDepositSubTx) == 1` to
 *  ensure exactly one deposit mode is selected; zero or more than one
 *  causes `temMALFORMED`. */
inline constexpr FlagValue tfDepositSubTx =
    tfLPToken | tfSingleAsset | tfTwoAsset | tfOneAssetLPToken | tfLimitLPToken | tfTwoAssetIfEmpty;

#pragma push_macro("ACCOUNTSET_FLAGS")
#pragma push_macro("ACCOUNTSET_FLAG_TO_VALUE")
#pragma push_macro("ACCOUNTSET_FLAG_TO_MAP")

/** X-macro table of `AccountSet` `SetFlag`/`ClearFlag` integer values.
 *
 *  These are **small integers** (1–17), not bitmasks, and are carried in the
 *  `SetFlag` or `ClearFlag` field of an `AccountSet` transaction rather than
 *  in the `Flags` bitmask.  Value 11 is reserved for the Hooks amendment
 *  (`asfTshCollect`) and is intentionally absent here.
 *
 *  The macro is instantiated twice: once to declare inline constants and once
 *  to build the map returned by `getAsfFlagMap()`.
 *
 *  @param ASF_FLAG  Receives `(name, integer_value)` for each `asf*` constant.
 *
 *  @warning These values are protocol-stable; changing them breaks existing
 *      signed `AccountSet` transactions.
 */
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

/** Returns all `AccountSet` `SetFlag`/`ClearFlag` values by name.
 *
 *  The map keys are the `asf*` constant names; values are the corresponding
 *  small integers.  Initialised once (Meyer's singleton) and consumed by the
 *  `server_definitions` RPC endpoint alongside `getAllTxFlags()`.
 *
 *  @return A reference to the singleton `asf*` flag map.
 */
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

// NOLINTEND(readability-identifier-naming)
