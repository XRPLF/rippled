//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012 - 2019 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_TER_H_INCLUDED
#define RIPPLE_PROTOCOL_TER_H_INCLUDED

#include <ripple/basics/safe_cast.h>
#include <ripple/json/json_value.h>

#include <optional>
#include <ostream>
#include <string>

namespace ripple {

// See https://xrpl.org/transaction-results.html
//
// "Transaction Engine Result"
// or Transaction ERror.
//
using TERUnderlyingType = int;

//------------------------------------------------------------------------------

enum TELcodes : TERUnderlyingType {
    // Note: Range is stable.
    // Exact numbers are used in ripple-binary-codec:
    //     https://github.com/ripple/ripple-binary-codec/blob/master/src/enums/definitions.json
    // Use tokens.

    // -399 .. -300: L Local error (transaction fee inadequate, exceeds local
    // limit) Only valid during non-consensus processing. Implications:
    // - Not forwarded
    // - No fee check
    telLOCAL_ERROR = -399,
    telBAD_DOMAIN = -398,
    telBAD_PATH_COUNT = -397,
    telBAD_PUBLIC_KEY = -396,
    telFAILED_PROCESSING = -395,
    telINSUF_FEE_P = -394,
    telNO_DST_PARTIAL = -393,
    telCAN_NOT_QUEUE = -392,
    telCAN_NOT_QUEUE_BALANCE = -391,
    telCAN_NOT_QUEUE_BLOCKS = -390,
    telCAN_NOT_QUEUE_BLOCKED = -389,
    telCAN_NOT_QUEUE_FEE = -388,
    telCAN_NOT_QUEUE_FULL = -387,
};

//------------------------------------------------------------------------------

enum TEMcodes : TERUnderlyingType {
    // Note: Range is stable.
    // Exact numbers are used in ripple-binary-codec:
    //     https://github.com/ripple/ripple-binary-codec/blob/master/src/enums/definitions.json
    // Use tokens.

    // -299 .. -200: M Malformed (bad signature)
    // Causes:
    // - Transaction corrupt.
    // Implications:
    // - Not applied
    // - Not forwarded
    // - Reject
    // - Cannot succeed in any imagined ledger.
    temMALFORMED = -299,

    temBAD_AMOUNT = -298,
    temBAD_CURRENCY = -297,
    temBAD_EXPIRATION = -296,
    temBAD_FEE = -295,
    temBAD_ISSUER = -294,
    temBAD_LIMIT = -293,
    temBAD_OFFER = -292,
    temBAD_PATH = -291,
    temBAD_PATH_LOOP = -290,
    temBAD_REGKEY = -289,
    temBAD_SEND_XRP_LIMIT = -288,
    temBAD_SEND_XRP_MAX = -287,
    temBAD_SEND_XRP_NO_DIRECT = -286,
    temBAD_SEND_XRP_PARTIAL = -285,
    temBAD_SEND_XRP_PATHS = -284,
    temBAD_SEQUENCE = -283,
    temBAD_SIGNATURE = -282,
    temBAD_SRC_ACCOUNT = -281,
    temBAD_TRANSFER_RATE = -280,
    temDST_IS_SRC = -279,
    temDST_NEEDED = -278,
    temINVALID = -277,
    temINVALID_FLAG = -276,
    temREDUNDANT = -275,
    temRIPPLE_EMPTY = -274,
    temDISABLED = -273,
    temBAD_SIGNER = -272,
    temBAD_QUORUM = -271,
    temBAD_WEIGHT = -270,
    temBAD_TICK_SIZE = -269,
    temINVALID_ACCOUNT_ID = -268,
    temCANNOT_PREAUTH_SELF = -267,
    temINVALID_COUNT = -266,

    // An internal intermediate result; should never be returned.
    temUNCERTAIN = -265,
    // An internal intermediate result; should never be returned.
    temUNKNOWN = -264,

    temSEQ_AND_TICKET = -263,
    temBAD_NFTOKEN_TRANSFER_FEE = -262,
};

//------------------------------------------------------------------------------

enum TEFcodes : TERUnderlyingType {
    // Note: Range is stable.
    // Exact numbers are used in ripple-binary-codec:
    //     https://github.com/ripple/ripple-binary-codec/blob/master/src/enums/definitions.json
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
    tefFAILURE = -199,
    tefALREADY = -198,
    tefBAD_ADD_AUTH = -197,
    tefBAD_AUTH = -196,
    tefBAD_LEDGER = -195,
    tefCREATED = -194,
    tefEXCEPTION = -193,
    tefINTERNAL = -192,
    tefNO_AUTH_REQUIRED = -191,  // Can't set auth if auth is not required.
    tefPAST_SEQ = -190,
    tefWRONG_PRIOR = -189,
    tefMASTER_DISABLED = -188,
    tefMAX_LEDGER = -187,
    tefBAD_SIGNATURE = -186,
    tefBAD_QUORUM = -185,
    tefNOT_MULTI_SIGNING = -184,
    tefBAD_AUTH_MASTER = -183,
    tefINVARIANT_FAILED = -182,
    tefTOO_BIG = -181,
    tefNO_TICKET = -180,
    tefNFTOKEN_IS_NOT_TRANSFERABLE = -179,
};

//------------------------------------------------------------------------------

enum TERcodes : TERUnderlyingType {
    // Note: Range is stable.
    // Exact numbers are used in ripple-binary-codec:
    //     https://github.com/ripple/ripple-binary-codec/blob/master/src/enums/definitions.json
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
    terRETRY = -99,
    terFUNDS_SPENT = -98,  // DEPRECATED.
    terINSUF_FEE_B = -97,  // Can't pay fee, therefore don't burden network.
    terNO_ACCOUNT = -96,   // Can't pay fee, therefore don't burden network.
    terNO_AUTH = -95,      // Not authorized to hold IOUs.
    terNO_LINE = -94,      // Internal flag.
    terOWNERS = -93,       // Can't succeed with non-zero owner count.
    terPRE_SEQ = -92,      // Can't pay fee, no point in forwarding, so don't
                           // burden network.
    terLAST = -91,         // DEPRECATED.
    terNO_RIPPLE = -90,    // Rippling not allowed
    terQUEUED = -89,       // Transaction is being held in TxQ until fee drops
    terPRE_TICKET = -88,  // Ticket is not yet in ledger but might be on its way
};

//------------------------------------------------------------------------------

enum TEScodes : TERUnderlyingType {
    // Note: Exact number must stay stable.  This code is stored by value
    // in metadata for historic transactions.

    // 0: S Success (success)
    // Causes:
    // - Success.
    // Implications:
    // - Applied
    // - Forwarded
    tesSUCCESS = 0
};

//------------------------------------------------------------------------------

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
    tecHOOK_ERROR [[maybe_unused]] = 153,
    tecMAX_SEQUENCE_REACHED = 154,
    tecNO_SUITABLE_NFTOKEN_PAGE = 155,
    tecNFTOKEN_BUY_SELL_MISMATCH = 156,
    tecNFTOKEN_OFFER_TYPE_MISMATCH = 157,
    tecCANT_ACCEPT_OWN_NFTOKEN_OFFER = 158,
    tecINSUFFICIENT_FUNDS = 159,
    tecOBJECT_NOT_FOUND = 160,
    tecINSUFFICIENT_PAYMENT = 161,
};

//------------------------------------------------------------------------------

// For generic purposes, a free function that returns the value of a TE*codes.
constexpr TERUnderlyingType
TERtoInt(TELcodes v)
{
    return safe_cast<TERUnderlyingType>(v);
}

constexpr TERUnderlyingType
TERtoInt(TEMcodes v)
{
    return safe_cast<TERUnderlyingType>(v);
}

constexpr TERUnderlyingType
TERtoInt(TEFcodes v)
{
    return safe_cast<TERUnderlyingType>(v);
}

constexpr TERUnderlyingType
TERtoInt(TERcodes v)
{
    return safe_cast<TERUnderlyingType>(v);
}

constexpr TERUnderlyingType
TERtoInt(TEScodes v)
{
    return safe_cast<TERUnderlyingType>(v);
}

constexpr TERUnderlyingType
TERtoInt(TECcodes v)
{
    return safe_cast<TERUnderlyingType>(v);
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
    static constexpr TERSubset
    fromInt(int from)
    {
        return TERSubset(from);
    }

    // Trait tells enable_if which types are allowed for construction.
    template <
        typename T,
        typename = std::enable_if_t<
            Trait<std::remove_cv_t<std::remove_reference_t<T>>>::value>>
    constexpr TERSubset(T rhs) : code_(TERtoInt(rhs))
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
    explicit operator bool() const
    {
        return code_ != tesSUCCESS;
    }

    // Conversion to Json::Value allows assignment to Json::Objects
    // without casting.
    operator Json::Value() const
    {
        return Json::Value{code_};
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
    TERtoInt(TERSubset v)
    {
        return v.code_;
    }
};

// Comparison operators.
// Only enabled if both arguments return int if TERtiInt is called with them.
template <typename L, typename R>
constexpr auto
operator==(L const& lhs, R const& rhs) -> std::enable_if_t<
    std::is_same<decltype(TERtoInt(lhs)), int>::value &&
        std::is_same<decltype(TERtoInt(rhs)), int>::value,
    bool>
{
    return TERtoInt(lhs) == TERtoInt(rhs);
}

template <typename L, typename R>
constexpr auto
operator!=(L const& lhs, R const& rhs) -> std::enable_if_t<
    std::is_same<decltype(TERtoInt(lhs)), int>::value &&
        std::is_same<decltype(TERtoInt(rhs)), int>::value,
    bool>
{
    return TERtoInt(lhs) != TERtoInt(rhs);
}

template <typename L, typename R>
constexpr auto
operator<(L const& lhs, R const& rhs) -> std::enable_if_t<
    std::is_same<decltype(TERtoInt(lhs)), int>::value &&
        std::is_same<decltype(TERtoInt(rhs)), int>::value,
    bool>
{
    return TERtoInt(lhs) < TERtoInt(rhs);
}

template <typename L, typename R>
constexpr auto
operator<=(L const& lhs, R const& rhs) -> std::enable_if_t<
    std::is_same<decltype(TERtoInt(lhs)), int>::value &&
        std::is_same<decltype(TERtoInt(rhs)), int>::value,
    bool>
{
    return TERtoInt(lhs) <= TERtoInt(rhs);
}

template <typename L, typename R>
constexpr auto
operator>(L const& lhs, R const& rhs) -> std::enable_if_t<
    std::is_same<decltype(TERtoInt(lhs)), int>::value &&
        std::is_same<decltype(TERtoInt(rhs)), int>::value,
    bool>
{
    return TERtoInt(lhs) > TERtoInt(rhs);
}

template <typename L, typename R>
constexpr auto
operator>=(L const& lhs, R const& rhs) -> std::enable_if_t<
    std::is_same<decltype(TERtoInt(lhs)), int>::value &&
        std::is_same<decltype(TERtoInt(rhs)), int>::value,
    bool>
{
    return TERtoInt(lhs) >= TERtoInt(rhs);
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
isTelLocal(TER x)
{
    return ((x) >= telLOCAL_ERROR && (x) < temMALFORMED);
}

inline bool
isTemMalformed(TER x)
{
    return ((x) >= temMALFORMED && (x) < tefFAILURE);
}

inline bool
isTefFailure(TER x)
{
    return ((x) >= tefFAILURE && (x) < terRETRY);
}

inline bool
isTerRetry(TER x)
{
    return ((x) >= terRETRY && (x) < tesSUCCESS);
}

inline bool
isTesSuccess(TER x)
{
    return ((x) == tesSUCCESS);
}

inline bool
isTecClaim(TER x)
{
    return ((x) >= tecCLAIM);
}

bool
transResultInfo(TER code, std::string& token, std::string& text);

std::string
transToken(TER code);

std::string
transHuman(TER code);

std::optional<TER>
transCode(std::string const& token);

}  // namespace ripple

#endif
