/** @file
 *  Transaction Engine Result (TER) code taxonomy for the XRP Ledger.
 *
 *  Defines the six result-code enumerations (tel/tem/tef/ter/tes/tec),
 *  the strongly-typed `TERSubset<Trait>` wrapper that enforces which
 *  categories are permitted in a given context, the `NotTEC` and `TER`
 *  aliases, comparison operators, and lookup utilities.
 *
 *  Every code value is part of the wire protocol: numeric values are
 *  stored in ledger metadata and consumed by `ripple-binary-codec`.
 *  @see https://xrpl.org/transaction-results.html
 */
#pragma once

// NOLINTBEGIN(readability-identifier-naming)

#include <xrpl/basics/safe_cast.h>
#include <xrpl/json/json_value.h>

#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>

namespace xrpl {

/** Underlying integer type shared by all TER code enumerations.
 *
 *  Using a named typedef allows `TERSubset` to store a plain `int`
 *  without naming a specific enum, and lets `TERtoInt` overloads share
 *  a single return type that triggers the comparison-operator SFINAE.
 *
 *  @see https://xrpl.org/transaction-results.html
 */
using TERUnderlyingType = int;

//------------------------------------------------------------------------------

/** Local-error result codes (range −399..−300).
 *
 *  A `tel` result means this node alone rejected the transaction; the
 *  decision is not propagated to the network. The transaction is not
 *  forwarded to peers and no fee check is performed. Common causes:
 *  fee below the local minimum, or path counts that exceed node-local
 *  limits. These codes are only valid during non-consensus processing.
 *
 *  @note Numeric values are stable and encoded in `ripple-binary-codec`.
 *      Never renumber or remove existing enumerators.
 */
// Protocol-critical, mixed with custom TER wrapper type, hundreds of usages
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum TELcodes : TERUnderlyingType {
    // Note: Range is stable.
    // Exact numbers are used in ripple-binary-codec:
    //     https://github.com/XRPLF/xrpl.js/blob/main/packages/ripple-binary-codec/src/enums/definitions.json
    // Use tokens.
    telLOCAL_ERROR = -399,
    telBAD_DOMAIN,
    telBAD_PATH_COUNT,
    telBAD_PUBLIC_KEY,
    telFAILED_PROCESSING,
    telINSUF_FEE_P,
    telNO_DST_PARTIAL,
    telCAN_NOT_QUEUE,
    telCAN_NOT_QUEUE_BALANCE,
    telCAN_NOT_QUEUE_BLOCKS,
    telCAN_NOT_QUEUE_BLOCKED,
    telCAN_NOT_QUEUE_FEE,
    telCAN_NOT_QUEUE_FULL,
    telWRONG_NETWORK,
    telREQUIRES_NETWORK_ID,
    telNETWORK_ID_MAKES_TX_NON_CANONICAL,
    telENV_RPC_FAILED
};

//------------------------------------------------------------------------------

/** Malformed-transaction result codes (range −299..−200).
 *
 *  A `tem` result means the transaction is structurally corrupt and
 *  cannot succeed in any possible ledger state. The transaction is
 *  rejected without being applied or forwarded, and no fee is charged.
 *
 *  @note Numeric values are stable and encoded in `ripple-binary-codec`.
 *      Never renumber or remove existing enumerators.
 */
// Protocol-critical, mixed with custom TER wrapper type, hundreds of usages
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum TEMcodes : TERUnderlyingType {
    // Note: Range is stable.
    // Exact numbers are used in ripple-binary-codec:
    //     https://github.com/XRPLF/xrpl.js/blob/main/packages/ripple-binary-codec/src/enums/definitions.json
    // Use tokens.
    temMALFORMED = -299,

    temBAD_AMOUNT,
    temBAD_CURRENCY,
    temBAD_EXPIRATION,
    temBAD_FEE,
    temBAD_ISSUER,
    temBAD_LIMIT,
    temBAD_OFFER,
    temBAD_PATH,
    temBAD_PATH_LOOP,
    temBAD_REGKEY,
    temBAD_SEND_XRP_LIMIT,
    temBAD_SEND_XRP_MAX,
    temBAD_SEND_XRP_NO_DIRECT,
    temBAD_SEND_XRP_PARTIAL,
    temBAD_SEND_XRP_PATHS,
    temBAD_SEQUENCE,
    temBAD_SIGNATURE,
    temBAD_SRC_ACCOUNT,
    temBAD_TRANSFER_RATE,
    temDST_IS_SRC,
    temDST_NEEDED,
    temINVALID,
    temINVALID_FLAG,
    temREDUNDANT,
    temRIPPLE_EMPTY,
    temDISABLED,
    temBAD_SIGNER,
    temBAD_QUORUM,
    temBAD_WEIGHT,
    temBAD_TICK_SIZE,
    temINVALID_ACCOUNT_ID,
    temCANNOT_PREAUTH_SELF,
    temINVALID_COUNT,

    temUNCERTAIN,  ///< Internal sentinel — in the process of determining a result; never returned to callers.
    temUNKNOWN,    ///< Internal sentinel — logic not yet implemented; never returned to callers.

    temSEQ_AND_TICKET,
    temBAD_NFTOKEN_TRANSFER_FEE,

    temBAD_AMM_TOKENS,

    temXCHAIN_EQUAL_DOOR_ACCOUNTS,
    temXCHAIN_BAD_PROOF,
    temXCHAIN_BRIDGE_BAD_ISSUES,
    temXCHAIN_BRIDGE_NONDOOR_OWNER,
    temXCHAIN_BRIDGE_BAD_MIN_ACCOUNT_CREATE_AMOUNT,
    temXCHAIN_BRIDGE_BAD_REWARD_AMOUNT,

    temEMPTY_DID,

    temARRAY_EMPTY,
    temARRAY_TOO_LARGE,
    temBAD_TRANSFER_FEE,
    temINVALID_INNER_BATCH,
    temBAD_MPT,
};

//------------------------------------------------------------------------------

/** Failure result codes (range −199..−100).
 *
 *  A `tef` result means the transaction cannot be applied because of the
 *  current ledger state (e.g., sequence already used, bad signature, or
 *  an unexpected C++ exception). The transaction is not applied, not
 *  forwarded, and no fee is charged. Unlike `tem`, a `tef` transaction
 *  could theoretically succeed in a different ledger state.
 *
 *  @note Numeric values are stable and encoded in `ripple-binary-codec`.
 *      Never renumber or remove existing enumerators.
 */
// Protocol-critical, mixed with custom TER wrapper type, hundreds of usages
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum TEFcodes : TERUnderlyingType {
    // Note: Range is stable.
    // Exact numbers are used in ripple-binary-codec:
    //     https://github.com/XRPLF/xrpl.js/blob/main/packages/ripple-binary-codec/src/enums/definitions.json
    // Use tokens.
    tefFAILURE = -199,
    tefALREADY,
    tefBAD_ADD_AUTH,
    tefBAD_AUTH,
    tefBAD_LEDGER,
    tefCREATED,
    tefEXCEPTION,
    tefINTERNAL,
    tefNO_AUTH_REQUIRED,  // Can't set auth if auth is not required.
    tefPAST_SEQ,
    tefWRONG_PRIOR,
    tefMASTER_DISABLED,
    tefMAX_LEDGER,
    tefBAD_SIGNATURE,
    tefBAD_QUORUM,
    tefNOT_MULTI_SIGNING,
    tefBAD_AUTH_MASTER,
    tefINVARIANT_FAILED,
    tefTOO_BIG,
    tefNO_TICKET,
    tefNFTOKEN_IS_NOT_TRANSFERABLE,
    tefINVALID_LEDGER_FIX_TYPE,
};

//------------------------------------------------------------------------------

/** Retry result codes (range −99..−1).
 *
 *  A `ter` result means the transaction cannot succeed right now, but
 *  might succeed after other transactions are applied — for example,
 *  if the sequence number is too high or there are insufficient funds
 *  for the fee. The transaction is not applied and leaves a sequence
 *  gap that can block later transactions. It may be held in the
 *  transaction queue (`terQUEUED`) to retry when fee levels drop.
 *
 *  @note Numeric values are stable and encoded in `ripple-binary-codec`.
 *      Never renumber or remove existing enumerators.
 */
// Protocol-critical, mixed with custom TER wrapper type, hundreds of usages
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum TERcodes : TERUnderlyingType {
    // Note: Range is stable.
    // Exact numbers are used in ripple-binary-codec:
    //     https://github.com/XRPLF/xrpl.js/blob/main/packages/ripple-binary-codec/src/enums/definitions.json
    // Use tokens.
    terRETRY = -99,
    terFUNDS_SPENT,             // DEPRECATED.
    terINSUF_FEE_B,             // Can't pay fee, therefore don't burden network.
    terNO_ACCOUNT,              // Can't pay fee, therefore don't burden network.
    terNO_AUTH,                 // Not authorized to hold IOUs.
    terNO_LINE,                 // Internal flag.
    terOWNERS,                  // Can't succeed with non-zero owner count.
    terPRE_SEQ,                 // Can't pay fee, no point in forwarding, so don't
                                // burden network.
    terLAST,                    // DEPRECATED.
    terNO_RIPPLE,               // Rippling not allowed
    terQUEUED,                  // Transaction is being held in TxQ until fee drops
    terPRE_TICKET,              // Ticket is not yet in ledger but might be on its way
    terNO_AMM,                  // AMM doesn't exist for the asset pair
    terADDRESS_COLLISION,       // Failed to allocate AccountID when trying to
                                // create a pseudo-account
    terNO_DELEGATE_PERMISSION,  // Delegate does not have permission
    terLOCKED,                  // MPT is locked
};

//------------------------------------------------------------------------------

/** Success result code (value 0).
 *
 *  `tesSUCCESS` is the sole member: the transaction was applied to the
 *  ledger and forwarded to peers. Its numeric value (0) is stored in
 *  ledger metadata and must never change.
 *
 *  @note `TERSubset::operator bool()` returns `false` for this code
 *      (success = falsy), mirroring the conventional C error-code idiom.
 */
// Protocol-critical, mixed with custom TER wrapper type, hundreds of usages
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum TEScodes : TERUnderlyingType {
    // Note: Exact number must stay stable.  This code is stored by value
    // in metadata for historic transactions.
    tesSUCCESS = 0
};

//------------------------------------------------------------------------------

/** Fee-claim result codes (range 100..255).
 *
 *  A `tec` result means the fee is consumed and the sequence number is
 *  spent, but no other effect is applied to the ledger. The transaction
 *  is still applied and forwarded to peers. Typical causes: a payment
 *  with no valid path, or a transaction that is logically invalid but
 *  well-formed enough to charge a fee.
 *
 *  When `tapRETRY` is set during application, `tec` codes are demoted
 *  to `terRETRY` so the transaction can be retried rather than
 *  consuming the sequence number.
 *
 *  @note **DO NOT CHANGE THESE NUMBERS.** They are stored by value in
 *      ledger metadata and parsed by external tools such as
 *      `ripple-binary-codec`. Append new codes; never renumber or remove.
 *  @note Naming convention: use `tecNO_ENTRY` when the primary ledger
 *      object targeted by the transaction is missing; use
 *      `tecOBJECT_NOT_FOUND` when an auxiliary object required to
 *      complete the transaction cannot be found.
 */
// Protocol-critical, mixed with custom TER wrapper type, hundreds of usages
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum TECcodes : TERUnderlyingType {
    // Note: Exact numbers must stay stable.  These codes are stored by
    // value in metadata for historic transactions.
    tecCLAIM = 100,
    tecPATH_PARTIAL = 101,
    tecUNFUNDED_ADD = 102,  // Unused legacy code
    tecUNFUNDED_OFFER = 103,
    tecUNFUNDED_PAYMENT = 104,
    tecFAILED_PROCESSING = 105,
    tecDIR_FULL = 121,
    tecINSUF_RESERVE_LINE = 122,
    tecINSUF_RESERVE_OFFER = 123,
    tecNO_DST = 124,
    tecNO_DST_INSUF_XRP = 125,
    tecNO_LINE_INSUF_RESERVE = 126,
    tecNO_LINE_REDUNDANT = 127,
    tecPATH_DRY = 128,
    tecUNFUNDED = 129,
    tecNO_ALTERNATIVE_KEY = 130,
    tecNO_REGULAR_KEY = 131,
    tecOWNERS = 132,
    tecNO_ISSUER = 133,
    tecNO_AUTH = 134,
    tecNO_LINE = 135,
    tecINSUFF_FEE = 136,
    tecFROZEN = 137,
    tecNO_TARGET = 138,
    tecNO_PERMISSION = 139,
    tecNO_ENTRY = 140,
    tecINSUFFICIENT_RESERVE = 141,
    tecNEED_MASTER_KEY = 142,
    tecDST_TAG_NEEDED = 143,
    tecINTERNAL = 144,
    tecOVERSIZE = 145,
    tecCRYPTOCONDITION_ERROR = 146,
    tecINVARIANT_FAILED = 147,
    tecEXPIRED = 148,
    tecDUPLICATE = 149,
    tecKILLED = 150,
    tecHAS_OBLIGATIONS = 151,
    tecTOO_SOON = 152,
    tecHOOK_REJECTED [[maybe_unused]] = 153,
    tecMAX_SEQUENCE_REACHED = 154,
    tecNO_SUITABLE_NFTOKEN_PAGE = 155,
    tecNFTOKEN_BUY_SELL_MISMATCH = 156,
    tecNFTOKEN_OFFER_TYPE_MISMATCH = 157,
    tecCANT_ACCEPT_OWN_NFTOKEN_OFFER = 158,
    tecINSUFFICIENT_FUNDS = 159,
    tecOBJECT_NOT_FOUND = 160,
    tecINSUFFICIENT_PAYMENT = 161,
    tecUNFUNDED_AMM = 162,
    tecAMM_BALANCE = 163,
    tecAMM_FAILED = 164,
    tecAMM_INVALID_TOKENS = 165,
    tecAMM_EMPTY = 166,
    tecAMM_NOT_EMPTY = 167,
    tecAMM_ACCOUNT = 168,
    tecINCOMPLETE = 169,
    tecXCHAIN_BAD_TRANSFER_ISSUE = 170,
    tecXCHAIN_NO_CLAIM_ID = 171,
    tecXCHAIN_BAD_CLAIM_ID = 172,
    tecXCHAIN_CLAIM_NO_QUORUM = 173,
    tecXCHAIN_PROOF_UNKNOWN_KEY = 174,
    tecXCHAIN_CREATE_ACCOUNT_NONXRP_ISSUE = 175,
    tecXCHAIN_WRONG_CHAIN = 176,
    tecXCHAIN_REWARD_MISMATCH = 177,
    tecXCHAIN_NO_SIGNERS_LIST = 178,
    tecXCHAIN_SENDING_ACCOUNT_MISMATCH = 179,
    tecXCHAIN_INSUFF_CREATE_AMOUNT = 180,
    tecXCHAIN_ACCOUNT_CREATE_PAST = 181,
    tecXCHAIN_ACCOUNT_CREATE_TOO_MANY = 182,
    tecXCHAIN_PAYMENT_FAILED = 183,
    tecXCHAIN_SELF_COMMIT = 184,
    tecXCHAIN_BAD_PUBLIC_KEY_ACCOUNT_PAIR = 185,
    tecXCHAIN_CREATE_ACCOUNT_DISABLED = 186,
    tecEMPTY_DID = 187,
    tecINVALID_UPDATE_TIME = 188,
    tecTOKEN_PAIR_NOT_FOUND = 189,
    tecARRAY_EMPTY = 190,
    tecARRAY_TOO_LARGE = 191,
    tecLOCKED = 192,
    tecBAD_CREDENTIALS = 193,
    tecWRONG_ASSET = 194,
    tecLIMIT_EXCEEDED = 195,
    tecPSEUDO_ACCOUNT = 196,
    tecPRECISION_LOSS = 197,
};

//------------------------------------------------------------------------------

/** Convert a TEL/TEM/TEF/TER/TES/TEC code to its underlying integer value.
 *
 *  These overloads form the single conversion point used by `TERSubset`,
 *  comparison operators, and any code that needs to inspect the raw
 *  integer. A free-function overload set is used rather than an explicit
 *  conversion operator on `TERSubset` to prevent silent implicit
 *  conversions in constructor-initialization contexts (e.g., `Status(TER)`
 *  would compile silently even with `explicit` — a named function does not).
 *
 *  A matching `friend` overload for `TERSubset<Trait>` is defined inside
 *  the class template; the six overloads below cover the raw enum types.
 *
 *  @param v The enum code to convert.
 *  @return The underlying `int` value of `v`.
 */
constexpr TERUnderlyingType
TERtoInt(TELcodes v)
{
    return safeCast<TERUnderlyingType>(v);
}

/** @copydoc TERtoInt(TELcodes) */
constexpr TERUnderlyingType
TERtoInt(TEMcodes v)
{
    return safeCast<TERUnderlyingType>(v);
}

/** @copydoc TERtoInt(TELcodes) */
constexpr TERUnderlyingType
TERtoInt(TEFcodes v)
{
    return safeCast<TERUnderlyingType>(v);
}

/** @copydoc TERtoInt(TELcodes) */
constexpr TERUnderlyingType
TERtoInt(TERcodes v)
{
    return safeCast<TERUnderlyingType>(v);
}

/** @copydoc TERtoInt(TELcodes) */
constexpr TERUnderlyingType
TERtoInt(TEScodes v)
{
    return safeCast<TERUnderlyingType>(v);
}

/** @copydoc TERtoInt(TELcodes) */
constexpr TERUnderlyingType
TERtoInt(TECcodes v)
{
    return safeCast<TERUnderlyingType>(v);
}

//------------------------------------------------------------------------------

/** Strongly-typed wrapper around a TER integer that restricts which
 *  result-code categories may be implicitly assigned or constructed.
 *
 *  The `Trait` policy class template determines which `TE*codes` enum
 *  types are accepted. A specialization of `Trait<T>` that inherits from
 *  `std::true_type` permits `T`; one inheriting from `std::false_type`
 *  rejects it at compile time via `std::enable_if`. This provides
 *  category-level type safety without runtime overhead.
 *
 *  Two concrete aliases are provided:
 *  - `NotTEC` — permits `tel`, `tem`, `tef`, `ter`, `tes`; **excludes
 *    `tec`**. Used as the return type of `preflight()` to prevent fee-theft
 *    via unsigned transactions (see `CanCvtToNotTEC`).
 *  - `TER` — permits all six categories including `tec` and `NotTEC`
 *    (widening assignment from `NotTEC` to `TER` is always valid).
 *
 *  Default-constructs to `tesSUCCESS`. Truthy (`operator bool`) when the
 *  stored code is anything other than `tesSUCCESS`.
 *
 *  @tparam Trait A class template whose specializations for each `TE*codes`
 *      type inherit from `std::true_type` (allowed) or `std::false_type`
 *      (disallowed).
 */
template <template <typename> class Trait>
class TERSubset
{
    TERUnderlyingType code_;

public:
    /** Default-constructs to `tesSUCCESS`. */
    constexpr TERSubset() : code_(tesSUCCESS)
    {
    }
    constexpr TERSubset(TERSubset const& rhs) = default;
    constexpr TERSubset(TERSubset&& rhs) = default;

private:
    constexpr explicit TERSubset(int rhs) : code_(rhs)
    {
    }

public:
    /** Construct from a raw integer, bypassing the Trait type check.
     *
     *  This escape hatch is intended for deserialization contexts (e.g.,
     *  reconstructing a `TER` from ledger metadata) where the integer
     *  originates from a validated source. Prefer enum-typed construction
     *  everywhere else.
     *
     *  @param from The raw integer code to wrap.
     *  @return A `TERSubset` holding `from`.
     */
    static constexpr TERSubset
    fromInt(int from)
    {
        return TERSubset(from);
    }

    /** Construct from any `TE*codes` enum type permitted by `Trait`.
     *
     *  The constructor is disabled via `std::enable_if_t` for enum types
     *  whose `Trait` specialization inherits from `std::false_type`, turning
     *  category violations into hard compile errors.
     *
     *  @param rhs The source enum value.
     */
    template <
        typename T,
        typename = std::enable_if_t<Trait<std::remove_cv_t<std::remove_reference_t<T>>>::value>>
    constexpr TERSubset(T rhs) : code_(TERtoInt(rhs))
    {
    }

    constexpr TERSubset&
    operator=(TERSubset const& rhs) = default;
    constexpr TERSubset&
    operator=(TERSubset&& rhs) = default;

    /** Assign from any `TE*codes` enum type permitted by `Trait`.
     *
     *  Disabled via `std::enable_if_t` for categories not allowed by
     *  `Trait`, matching the construction constraint.
     *
     *  @param rhs The source enum value.
     *  @return `*this`.
     */
    template <typename T>
    constexpr auto
    operator=(T rhs) -> std::enable_if_t<Trait<T>::value, TERSubset&>
    {
        code_ = TERtoInt(rhs);
        return *this;
    }

    /** Return `true` when the code is anything other than `tesSUCCESS`.
     *
     *  Mirrors conventional C error-code semantics: a falsy result is
     *  success, a truthy result means something went wrong. Use
     *  `isTesSuccess()` for the positive sense.
     */
    explicit
    operator bool() const
    {
        return code_ != tesSUCCESS;
    }

    /** Implicit conversion to `json::Value` for use in JSON object assembly.
     *
     *  Allows `jsonObj["result"] = ter;` without an explicit cast.
     */
    operator json::Value() const
    {
        return json::Value{code_};
    }

    /** Stream the raw integer code to `os`. */
    friend std::ostream&
    operator<<(std::ostream& os, TERSubset const& rhs)
    {
        return os << rhs.code_;
    }

    /** Return the underlying integer value of this result code.
     *
     *  Implemented as a named `friend` free function rather than an
     *  `explicit` conversion operator. An explicit operator would still
     *  allow silent conversion in constructor-initialization contexts
     *  (e.g., `Status(TER ter) : code_(ter) {}` compiles without warning
     *  even with `explicit`). A named function forces the conversion to
     *  be visible at every call site.
     *
     *  @param v The `TERSubset` to extract from.
     *  @return The raw `int` code.
     */
    friend constexpr TERUnderlyingType
    TERtoInt(TERSubset v)
    {
        return v.code_;
    }
};

/** @name TER comparison operators
 *
 *  Heterogeneous comparisons across any combination of raw `TE*codes`
 *  enum types and `TERSubset` wrappers. Each operator is enabled only
 *  when both operands have a `TERtoInt` overload returning `int`, so
 *  unrelated types (e.g., plain `int`) are excluded by SFINAE — no
 *  accidental numeric comparisons.
 *
 *  @{
 */
template <typename L, typename R>
constexpr auto
operator==(L const& lhs, R const& rhs) -> std::enable_if_t<
    std::is_same_v<decltype(TERtoInt(lhs)), int> && std::is_same_v<decltype(TERtoInt(rhs)), int>,
    bool>
{
    return TERtoInt(lhs) == TERtoInt(rhs);
}

template <typename L, typename R>
constexpr auto
operator!=(L const& lhs, R const& rhs) -> std::enable_if_t<
    std::is_same_v<decltype(TERtoInt(lhs)), int> && std::is_same_v<decltype(TERtoInt(rhs)), int>,
    bool>
{
    return TERtoInt(lhs) != TERtoInt(rhs);
}

template <typename L, typename R>
constexpr auto
operator<(L const& lhs, R const& rhs) -> std::enable_if_t<
    std::is_same_v<decltype(TERtoInt(lhs)), int> && std::is_same_v<decltype(TERtoInt(rhs)), int>,
    bool>
{
    return TERtoInt(lhs) < TERtoInt(rhs);
}

template <typename L, typename R>
constexpr auto
operator<=(L const& lhs, R const& rhs) -> std::enable_if_t<
    std::is_same_v<decltype(TERtoInt(lhs)), int> && std::is_same_v<decltype(TERtoInt(rhs)), int>,
    bool>
{
    return TERtoInt(lhs) <= TERtoInt(rhs);
}

template <typename L, typename R>
constexpr auto
operator>(L const& lhs, R const& rhs) -> std::enable_if_t<
    std::is_same_v<decltype(TERtoInt(lhs)), int> && std::is_same_v<decltype(TERtoInt(rhs)), int>,
    bool>
{
    return TERtoInt(lhs) > TERtoInt(rhs);
}

template <typename L, typename R>
constexpr auto
operator>=(L const& lhs, R const& rhs) -> std::enable_if_t<
    std::is_same_v<decltype(TERtoInt(lhs)), int> && std::is_same_v<decltype(TERtoInt(rhs)), int>,
    bool>
{
    return TERtoInt(lhs) >= TERtoInt(rhs);
}
/** @} */

//------------------------------------------------------------------------------

/** Trait that permits `tel`, `tem`, `tef`, `ter`, and `tes` but
 *  explicitly **excludes** `TECcodes` for use with `NotTEC`.
 *
 *  The exclusion of `tec` is a security invariant. `preflight()`
 *  executes before signature verification. If it could return a `tec`
 *  code, a malicious actor could craft a transaction with a very large
 *  fee and have that fee deducted from an account without supplying a
 *  valid signature. Restricting `preflight` return types to `NotTEC`
 *  makes this class of fee-theft structurally impossible at compile time.
 *
 *  @tparam FROM The candidate source type; only the five non-tec enums
 *      yield `std::true_type`.
 */
template <typename FROM>
class CanCvtToNotTEC : public std::false_type
{
};
template <>
class CanCvtToNotTEC<TELcodes> : public std::true_type
{
};
template <>
class CanCvtToNotTEC<TEMcodes> : public std::true_type
{
};
template <>
class CanCvtToNotTEC<TEFcodes> : public std::true_type
{
};
template <>
class CanCvtToNotTEC<TERcodes> : public std::true_type
{
};
template <>
class CanCvtToNotTEC<TEScodes> : public std::true_type
{
};

/** A `TERSubset` restricted to non-`tec` result categories.
 *
 *  Use as the return type of `preflight()` and any function that must
 *  not be allowed to claim a fee. Assigning a `TECcodes` value to a
 *  `NotTEC` variable is a compile-time error. A `NotTEC` can be
 *  widened to a `TER` without a cast.
 */
using NotTEC = TERSubset<CanCvtToNotTEC>;

//------------------------------------------------------------------------------

/** Trait that permits all six result-code categories plus `NotTEC` for
 *  use with the `TER` alias.
 *
 *  The `NotTEC` specialization enables the widening assignment from
 *  `NotTEC` to `TER` that is required in contexts where a function
 *  returning `NotTEC` passes its result to code expecting `TER`.
 *
 *  @tparam FROM The candidate source type; all six `TE*codes` enums
 *      and `NotTEC` yield `std::true_type`.
 */
template <typename FROM>
class CanCvtToTER : public std::false_type
{
};
template <>
class CanCvtToTER<TELcodes> : public std::true_type
{
};
template <>
class CanCvtToTER<TEMcodes> : public std::true_type
{
};
template <>
class CanCvtToTER<TEFcodes> : public std::true_type
{
};
template <>
class CanCvtToTER<TERcodes> : public std::true_type
{
};
template <>
class CanCvtToTER<TEScodes> : public std::true_type
{
};
template <>
class CanCvtToTER<TECcodes> : public std::true_type
{
};
template <>
class CanCvtToTER<NotTEC> : public std::true_type
{
};

/** A `TERSubset` that accepts all six result-code categories.
 *
 *  This is the general result type used throughout the transaction engine
 *  for `doApply()` and most ledger-application code. Use `NotTEC` for
 *  `preflight()` return types where `tec` codes must be excluded.
 */
using TER = TERSubset<CanCvtToTER>;

//------------------------------------------------------------------------------

/** Return `true` if `x` is a local-error (`tel`) code (−399..−300).
 *
 *  @param x The code to test; accepts any `TER`-compatible value.
 *  @return `true` iff `x` falls in the `tel` range.
 */
inline bool
isTelLocal(TER x) noexcept
{
    return (x >= telLOCAL_ERROR && x < temMALFORMED);
}

/** Return `true` if `x` is a malformed-transaction (`tem`) code (−299..−200).
 *
 *  @param x The code to test.
 *  @return `true` iff `x` falls in the `tem` range.
 */
inline bool
isTemMalformed(TER x) noexcept
{
    return (x >= temMALFORMED && x < tefFAILURE);
}

/** Return `true` if `x` is a failure (`tef`) code (−199..−100).
 *
 *  @param x The code to test.
 *  @return `true` iff `x` falls in the `tef` range.
 */
inline bool
isTefFailure(TER x) noexcept
{
    return (x >= tefFAILURE && x < terRETRY);
}

/** Return `true` if `x` is a retry (`ter`) code (−99..−1).
 *
 *  @param x The code to test.
 *  @return `true` iff `x` falls in the `ter` range.
 */
inline bool
isTerRetry(TER x) noexcept
{
    return (x >= terRETRY && x < tesSUCCESS);
}

/** Return `true` if `x` is `tesSUCCESS` (0).
 *
 *  Relies on `TERSubset::operator bool()` returning `false` for
 *  `tesSUCCESS` (the only falsy value), so this is equivalent to `!x`.
 *
 *  @param x The code to test.
 *  @return `true` iff `x == tesSUCCESS`.
 */
inline bool
isTesSuccess(TER x) noexcept
{
    // Makes use of TERSubset::operator bool()
    return !(x);
}

/** Return `true` if `x` is a fee-claim (`tec`) code (≥ 100).
 *
 *  Any value at or above `tecCLAIM` (100) is treated as a fee-claim
 *  result regardless of whether it is a recognized code.
 *
 *  @param x The code to test.
 *  @return `true` iff `x >= tecCLAIM`.
 */
inline bool
isTecClaim(TER x) noexcept
{
    return ((x) >= tecCLAIM);
}

/** Return the complete registry mapping every known TER code to its
 *  symbolic token and human-readable description.
 *
 *  The map is keyed by the underlying integer value and maps to a pair
 *  of C-string literals: `{token, description}`. Token strings match
 *  the enum identifier exactly (generated via preprocessor stringification).
 *  The registry is a Meyers singleton — initialized once on first call,
 *  thread-safe per C++11, and immutable thereafter.
 *
 *  @return A `const` reference to the singleton map, valid for the
 *      lifetime of the process.
 */
std::unordered_map<TERUnderlyingType, std::pair<char const* const, char const* const>> const&
transResults();

/** Look up the token and human-readable description for a TER code.
 *
 *  @param code  The TER result code to look up.
 *  @param token On success, populated with the symbolic token string
 *      (e.g., `"tecNO_DST"`). Unchanged on failure.
 *  @param text  On success, populated with the English description.
 *      Unchanged on failure.
 *  @return `true` if `code` is a registered code; `false` otherwise.
 */
bool
transResultInfo(TER code, std::string& token, std::string& text);

/** Return the symbolic token string for a TER code.
 *
 *  @param code The TER result code to look up.
 *  @return The token string (e.g., `"tecNO_DST"`) for known codes, or
 *      `"-"` if `code` is not in the registry.
 */
std::string
transToken(TER code);

/** Return the human-readable description for a TER code.
 *
 *  @param code The TER result code to look up.
 *  @return The English description string for known codes, or `"-"` if
 *      `code` is not in the registry.
 */
std::string
transHuman(TER code);

/** Convert a symbolic token string to the corresponding TER code.
 *
 *  Provides the reverse direction of `transToken()`. The reverse map is
 *  built lazily as a function-local static on the first call by inverting
 *  the primary registry; the two maps stay in sync automatically.
 *
 *  @param token The symbolic token string to look up (e.g., `"tecNO_DST"`).
 *  @return The matching `TER` wrapped in `std::optional`, or
 *      `std::nullopt` if `token` is not a recognized code name.
 */
std::optional<TER>
transCode(std::string const& token);

}  // namespace xrpl

// NOLINTEND(readability-identifier-naming)
