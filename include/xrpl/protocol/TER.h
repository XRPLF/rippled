#pragma once

// NOLINTBEGIN(readability-identifier-naming)

#include <xrpl/basics/safe_cast.h>
#include <xrpl/json/json_value.h>

#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>

namespace xrpl {

// See https://xrpl.org/transaction-results.html
//
// "Transaction Engine Result"
// or Transaction ERror.
//
using TERUnderlyingType = int;

//------------------------------------------------------------------------------

// Protocol-critical, mixed with custom TER wrapper type, hundreds of usages
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum TELcodes : TERUnderlyingType {
    // Note: Range is stable.
    // Exact numbers are used in ripple-binary-codec:
    //     https://github.com/XRPLF/xrpl.js/blob/main/packages/ripple-binary-codec/src/enums/definitions.json
    // Use tokens.

    // -399 .. -300: L Local error (transaction fee inadequate, exceeds local
    // limit) Only valid during non-consensus processing. Implications:
    // - Not forwarded
    // - No fee check
    TelLocalError = -399,
    TelBadDomain,
    TelBadPathCount,
    TelBadPublicKey,
    TelFailedProcessing,
    TelInsufFeeP,
    TelNoDstPartial,
    TelCanNotQueue,
    TelCanNotQueueBalance,
    TelCanNotQueueBlocks,
    TelCanNotQueueBlocked,
    TelCanNotQueueFee,
    TelCanNotQueueFull,
    TelWrongNetwork,
    TelRequiresNetworkId,
    TelNetworkIdMakesTxNonCanonical,
    TelEnvRpcFailed
};

//------------------------------------------------------------------------------

// Protocol-critical, mixed with custom TER wrapper type, hundreds of usages
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum TEMcodes : TERUnderlyingType {
    // Note: Range is stable.
    // Exact numbers are used in ripple-binary-codec:
    //     https://github.com/XRPLF/xrpl.js/blob/main/packages/ripple-binary-codec/src/enums/definitions.json
    // Use tokens.

    // -299 .. -200: M Malformed (bad signature)
    // Causes:
    // - Transaction corrupt.
    // Implications:
    // - Not applied
    // - Not forwarded
    // - Reject
    // - Cannot succeed in any imagined ledger.
    TemMalformed = -299,

    TemBadAmount,
    TemBadCurrency,
    TemBadExpiration,
    TemBadFee,
    TemBadIssuer,
    TemBadLimit,
    TemBadOffer,
    TemBadPath,
    TemBadPathLoop,
    TemBadRegkey,
    TemBadSendXrpLimit,
    TemBadSendXrpMax,
    TemBadSendXrpNoDirect,
    TemBadSendXrpPartial,
    TemBadSendXrpPaths,
    TemBadSequence,
    TemBadSignature,
    TemBadSrcAccount,
    TemBadTransferRate,
    TemDstIsSrc,
    TemDstNeeded,
    TemInvalid,
    TemInvalidFlag,
    TemRedundant,
    TemRippleEmpty,
    TemDisabled,
    TemBadSigner,
    TemBadQuorum,
    TemBadWeight,
    TemBadTickSize,
    TemInvalidAccountId,
    TemCannotPreauthSelf,
    TemInvalidCount,

    TemUncertain,  // An internal intermediate result; should never be returned.
    TemUnknown,    // An internal intermediate result; should never be returned.

    TemSeqAndTicket,
    TemBadNftokenTransferFee,

    TemBadAmmTokens,

    TemXchainEqualDoorAccounts,
    TemXchainBadProof,
    TemXchainBridgeBadIssues,
    TemXchainBridgeNondoorOwner,
    TemXchainBridgeBadMinAccountCreateAmount,
    TemXchainBridgeBadRewardAmount,

    TemEmptyDid,

    TemArrayEmpty,
    TemArrayTooLarge,
    TemBadTransferFee,
    TemInvalidInnerBatch,
    TemBadMpt,
};

//------------------------------------------------------------------------------

// Protocol-critical, mixed with custom TER wrapper type, hundreds of usages
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum TEFcodes : TERUnderlyingType {
    // Note: Range is stable.
    // Exact numbers are used in ripple-binary-codec:
    //     https://github.com/XRPLF/xrpl.js/blob/main/packages/ripple-binary-codec/src/enums/definitions.json
    // Use tokens.

    // -199 .. -100: F
    //    Failure (sequence number previously used)
    //
    // Causes:
    // - Transaction cannot succeed because of ledger state.
    // - Unexpected ledger state.
    // - C++ exception.
    //
    // Implications:
    // - Not applied
    // - Not forwarded
    // - Could succeed in an imagined ledger.
    TefFailure = -199,
    TefAlready,
    TefBadAddAuth,
    TefBadAuth,
    TefBadLedger,
    TefCreated,
    TefException,
    TefInternal,
    TefNoAuthRequired,  // Can't set auth if auth is not required.
    TefPastSeq,
    TefWrongPrior,
    TefMasterDisabled,
    TefMaxLedger,
    TefBadSignature,
    TefBadQuorum,
    TefNotMultiSigning,
    TefBadAuthMaster,
    TefInvariantFailed,
    TefTooBig,
    TefNoTicket,
    TefNftokenIsNotTransferable,
    TefInvalidLedgerFixType,
};

//------------------------------------------------------------------------------

// Protocol-critical, mixed with custom TER wrapper type, hundreds of usages
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum TERcodes : TERUnderlyingType {
    // Note: Range is stable.
    // Exact numbers are used in ripple-binary-codec:
    //     https://github.com/XRPLF/xrpl.js/blob/main/packages/ripple-binary-codec/src/enums/definitions.json
    // Use tokens.

    // -99 .. -1: R Retry
    //   sequence too high, no funds for txn fee, originating -account
    //   non-existent
    //
    // Cause:
    //   Prior application of another, possibly non-existent, transaction could
    //   allow this transaction to succeed.
    //
    // Implications:
    // - Not applied
    // - May be forwarded
    //   - Results indicating the txn was forwarded: terQUEUED
    //   - All others are not forwarded.
    // - Might succeed later
    // - Hold
    // - Makes hole in sequence which jams transactions.
    TerRetry = -99,
    TerFundsSpent,            // DEPRECATED.
    TerInsufFeeB,             // Can't pay fee, therefore don't burden network.
    TerNoAccount,             // Can't pay fee, therefore don't burden network.
    TerNoAuth,                // Not authorized to hold IOUs.
    TerNoLine,                // Internal flag.
    TerOwners,                // Can't succeed with non-zero owner count.
    TerPreSeq,                // Can't pay fee, no point in forwarding, so don't
                              // burden network.
    TerLast,                  // DEPRECATED.
    TerNoRipple,              // Rippling not allowed
    TerQueued,                // Transaction is being held in TxQ until fee drops
    TerPreTicket,             // Ticket is not yet in ledger but might be on its way
    TerNoAmm,                 // AMM doesn't exist for the asset pair
    TerAddressCollision,      // Failed to allocate AccountID when trying to
                              // create a pseudo-account
    TerNoDelegatePermission,  // Delegate does not have permission
    TerLocked,                // MPT is locked
};

//------------------------------------------------------------------------------

// Protocol-critical, mixed with custom TER wrapper type, hundreds of usages
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum TEScodes : TERUnderlyingType {
    // Note: Exact number must stay stable.  This code is stored by value
    // in metadata for historic transactions.

    // 0: S Success (success)
    // Causes:
    // - Success.
    // Implications:
    // - Applied
    // - Forwarded
    TesSuccess = 0
};

//------------------------------------------------------------------------------

// Protocol-critical, mixed with custom TER wrapper type, hundreds of usages
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum TECcodes : TERUnderlyingType {
    // Note: Exact numbers must stay stable.  These codes are stored by
    // value in metadata for historic transactions.

    // 100 .. 255 C
    //   Claim fee only (ripple transaction with no good paths, pay to
    //   non-existent account, no path)
    //
    // Causes:
    // - Success, but does not achieve optimal result.
    // - Invalid transaction or no effect, but claim fee to use the sequence
    //   number.
    //
    // Implications:
    // - Applied
    // - Forwarded
    //
    // Only allowed as a return code of appliedTransaction when !tapRETRY.
    // Otherwise, treated as terRETRY.
    //
    // DO NOT CHANGE THESE NUMBERS: They appear in ledger meta data.
    //
    // Note:
    //   tecNO_ENTRY is often used interchangeably with tecOBJECT_NOT_FOUND.
    //   While there does not seem to be a clear rule which to use when, the
    //   following guidance will help to keep errors consistent with the
    //   majority of (but not all) transaction types:
    // - tecNO_ENTRY : cannot find the primary ledger object on which the
    //   transaction is being attempted
    // - tecOBJECT_NOT_FOUND : cannot find the additional object(s) needed to
    //   complete the transaction

    TecClaim = 100,
    TecPathPartial = 101,
    TecUnfundedAdd = 102,  // Unused legacy code
    TecUnfundedOffer = 103,
    TecUnfundedPayment = 104,
    TecFailedProcessing = 105,
    TecDirFull = 121,
    TecInsufReserveLine = 122,
    TecInsufReserveOffer = 123,
    TecNoDst = 124,
    TecNoDstInsufXrp = 125,
    TecNoLineInsufReserve = 126,
    TecNoLineRedundant = 127,
    TecPathDry = 128,
    TecUnfunded = 129,
    TecNoAlternativeKey = 130,
    TecNoRegularKey = 131,
    TecOwners = 132,
    TecNoIssuer = 133,
    TecNoAuth = 134,
    TecNoLine = 135,
    TecInsuffFee = 136,
    TecFrozen = 137,
    TecNoTarget = 138,
    TecNoPermission = 139,
    TecNoEntry = 140,
    TecInsufficientReserve = 141,
    TecNeedMasterKey = 142,
    TecDstTagNeeded = 143,
    TecInternal = 144,
    TecOversize = 145,
    TecCryptoconditionError = 146,
    TecInvariantFailed = 147,
    TecExpired = 148,
    TecDuplicate = 149,
    TecKilled = 150,
    TecHasObligations = 151,
    TecTooSoon = 152,
    TecHookRejected [[maybe_unused]] = 153,
    TecMaxSequenceReached = 154,
    TecNoSuitableNftokenPage = 155,
    TecNftokenBuySellMismatch = 156,
    TecNftokenOfferTypeMismatch = 157,
    TecCantAcceptOwnNftokenOffer = 158,
    TecInsufficientFunds = 159,
    TecObjectNotFound = 160,
    TecInsufficientPayment = 161,
    TecUnfundedAmm = 162,
    TecAmmBalance = 163,
    TecAmmFailed = 164,
    TecAmmInvalidTokens = 165,
    TecAmmEmpty = 166,
    TecAmmNotEmpty = 167,
    TecAmmAccount = 168,
    TecIncomplete = 169,
    TecXchainBadTransferIssue = 170,
    TecXchainNoClaimId = 171,
    TecXchainBadClaimId = 172,
    TecXchainClaimNoQuorum = 173,
    TecXchainProofUnknownKey = 174,
    TecXchainCreateAccountNonxrpIssue = 175,
    TecXchainWrongChain = 176,
    TecXchainRewardMismatch = 177,
    TecXchainNoSignersList = 178,
    TecXchainSendingAccountMismatch = 179,
    TecXchainInsuffCreateAmount = 180,
    TecXchainAccountCreatePast = 181,
    TecXchainAccountCreateTooMany = 182,
    TecXchainPaymentFailed = 183,
    TecXchainSelfCommit = 184,
    TecXchainBadPublicKeyAccountPair = 185,
    TecXchainCreateAccountDisabled = 186,
    TecEmptyDid = 187,
    TecInvalidUpdateTime = 188,
    TecTokenPairNotFound = 189,
    TecArrayEmpty = 190,
    TecArrayTooLarge = 191,
    TecLocked = 192,
    TecBadCredentials = 193,
    TecWrongAsset = 194,
    TecLimitExceeded = 195,
    TecPseudoAccount = 196,
    TecPrecisionLoss = 197,
};

//------------------------------------------------------------------------------

// For generic purposes, a free function that returns the value of a TE*codes.
constexpr TERUnderlyingType
TERtoInt(TELcodes v)
{
    return safeCast<TERUnderlyingType>(v);
}

constexpr TERUnderlyingType
teRtoInt(TEMcodes v)
{
    return safeCast<TERUnderlyingType>(v);
}

constexpr TERUnderlyingType
TERtoInt(TEFcodes v)
{
    return safeCast<TERUnderlyingType>(v);
}

constexpr TERUnderlyingType
TERtoInt(TERcodes v)
{
    return safeCast<TERUnderlyingType>(v);
}

constexpr TERUnderlyingType
TERtoInt(TEScodes v)
{
    return safeCast<TERUnderlyingType>(v);
}

constexpr TERUnderlyingType
TERtoInt(TECcodes v)
{
    return safeCast<TERUnderlyingType>(v);
}

//------------------------------------------------------------------------------
// Template class that is specific to selected ranges of error codes.  The
// Trait tells std::enable_if which ranges are allowed.
template <template <typename> class Trait>
class TERSubset
{
    TERUnderlyingType code_;

public:
    // Constructors
    constexpr TERSubset() : code_(TesSuccess)
    {
    }
    constexpr TERSubset(TERSubset const& rhs) = default;
    constexpr TERSubset(TERSubset&& rhs) = default;

private:
    constexpr explicit TERSubset(int rhs) : code_(rhs)
    {
    }

public:
    static constexpr TERSubset
    fromInt(int from)
    {
        return TERSubset(from);
    }

    // Trait tells enable_if which types are allowed for construction.
    template <
        typename T,
        typename = std::enable_if_t<Trait<std::remove_cv_t<std::remove_reference_t<T>>>::value>>
    constexpr TERSubset(T rhs) : code_(teRtoInt(rhs))
    {
    }

    // Assignment
    constexpr TERSubset&
    operator=(TERSubset const& rhs) = default;
    constexpr TERSubset&
    operator=(TERSubset&& rhs) = default;

    // Trait tells enable_if which types are allowed for assignment.
    template <typename T>
    constexpr auto
    operator=(T rhs) -> std::enable_if_t<Trait<T>::value, TERSubset&>
    {
        code_ = TERtoInt(rhs);
        return *this;
    }

    // Conversion to bool.
    explicit
    operator bool() const
    {
        return code_ != TesSuccess;
    }

    // Conversion to json::Value allows assignment to json::Objects
    // without casting.
    operator json::Value() const
    {
        return json::Value{code_};
    }

    // Streaming operator.
    friend std::ostream&
    operator<<(std::ostream& os, TERSubset const& rhs)
    {
        return os << rhs.code_;
    }

    // Return the underlying value.  Not a member so similarly named free
    // functions can do the same work for the enums.
    //
    // It's worth noting that an explicit conversion operator was considered
    // and rejected.  Consider this case, taken from Status.h
    //
    // class Status {
    //     int code_;
    // public:
    //     Status (TER ter)
    //     : code_ (ter) {}
    // }
    //
    // This code compiles with no errors or warnings if TER has an explicit
    // (unnamed) conversion to int.  To avoid silent conversions like these
    // we provide (only) a named conversion.
    friend constexpr TERUnderlyingType
    teRtoInt(TERSubset v)
    {
        return v.code_;
    }
};

// Comparison operators.
// Only enabled if both arguments return int if TERtiInt is called with them.
template <typename L, typename R>
constexpr auto
operator==(L const& lhs, R const& rhs) -> std::enable_if_t<
    std::is_same_v<decltype(teRtoInt(lhs)), int> && std::is_same_v<decltype(TERtoInt(rhs)), int>,
    bool>
{
    return teRtoInt(lhs) == TERtoInt(rhs);
}

template <typename L, typename R>
constexpr auto
operator!=(L const& lhs, R const& rhs) -> std::enable_if_t<
    std::is_same_v<decltype(teRtoInt(lhs)), int> && std::is_same_v<decltype(TERtoInt(rhs)), int>,
    bool>
{
    return teRtoInt(lhs) != TERtoInt(rhs);
}

template <typename L, typename R>
constexpr auto
operator<(L const& lhs, R const& rhs) -> std::enable_if_t<
    std::is_same_v<decltype(teRtoInt(lhs)), int> && std::is_same_v<decltype(teRtoInt(rhs)), int>,
    bool>
{
    return teRtoInt(lhs) < teRtoInt(rhs);
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
    std::is_same_v<decltype(teRtoInt(lhs)), int> && std::is_same_v<decltype(teRtoInt(rhs)), int>,
    bool>
{
    return teRtoInt(lhs) >= teRtoInt(rhs);
}

//------------------------------------------------------------------------------

// Use traits to build a TERSubset that can convert from any of the TE*codes
// enums *except* TECcodes: NotTEC

// NOTE: NotTEC is useful for codes returned by preflight in transactors.
// Preflight checks occur prior to signature checking.  If preflight returned
// a tec code, then a malicious user could submit a transaction with a very
// large fee and have that fee charged against an account without using that
// account's valid signature.
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

using NotTEC = TERSubset<CanCvtToNotTEC>;

//------------------------------------------------------------------------------

// Use traits to build a TERSubset that can convert from any of the TE*codes
// enums as well as from NotTEC.
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

// TER allows all of the subsets.
using TER = TERSubset<CanCvtToTER>;

//------------------------------------------------------------------------------

inline bool
isTelLocal(TER x) noexcept
{
    return (x >= TelLocalError && x < TemMalformed);
}

inline bool
isTemMalformed(TER x) noexcept
{
    return (x >= TemMalformed && x < TefFailure);
}

inline bool
isTefFailure(TER x) noexcept
{
    return (x >= TefFailure && x < TerRetry);
}

inline bool
isTerRetry(TER x) noexcept
{
    return (x >= TerRetry && x < TesSuccess);
}

inline bool
isTesSuccess(TER x) noexcept
{
    // Makes use of TERSubset::operator bool()
    return !(x);
}

inline bool
isTecClaim(TER x) noexcept
{
    return ((x) >= TecClaim);
}

std::unordered_map<TERUnderlyingType, std::pair<char const* const, char const* const>> const&
transResults();

bool
transResultInfo(TER code, std::string& token, std::string& text);

std::string
transToken(TER code);

std::string
transHuman(TER code);

std::optional<TER>
transCode(std::string const& token);

}  // namespace xrpl

// NOLINTEND(readability-identifier-naming)
