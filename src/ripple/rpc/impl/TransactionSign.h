//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#ifndef RIPPLE_RPC_TRANSACTIONSIGN_H_INCLUDED
#define RIPPLE_RPC_TRANSACTIONSIGN_H_INCLUDED

#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/server/Role.h>

namespace ripple {
namespace RPC {

namespace detail {

// A class that allows these methods to be called with or without a
// real NetworkOPs instance.  This allows for unit testing.
class TxnSignApiFacade
{
private:
    NetworkOPs* const netOPs_;
    std::shared_ptr<ReadView const> ledger_;
    AccountID accountID_;
    std::shared_ptr<SLE const> sle_;

public:
    // Enum used to construct a Facade for unit tests.
    enum NoNetworkOPs{
        noNetOPs
    };

    TxnSignApiFacade () = delete;
    TxnSignApiFacade (TxnSignApiFacade const&) = delete;
    TxnSignApiFacade& operator= (TxnSignApiFacade const&) = delete;

    // For use in non unit testing circumstances.
    explicit TxnSignApiFacade (NetworkOPs& netOPs)
    : netOPs_ (&netOPs)
    { }

    // For testTransactionRPC unit tests.
    explicit TxnSignApiFacade (NoNetworkOPs noOPs)
    : netOPs_ (nullptr) { }

    // For testAutoFillFees unit tests.
    TxnSignApiFacade (NoNetworkOPs noOPs, std::shared_ptr<ReadView const> ledger)
    : netOPs_ (nullptr)
    , ledger_ (ledger)
    { }

    void snapshotAccountState (AccountID const& accountID);

    bool isValidAccount () const;

    std::uint32_t getSeq () const;

    bool findPathsForOneIssuer (
        AccountID const& dstAccountID,
        Issue const& srcIssue,
        STAmount const& dstAmount,
        int searchLevel,
        unsigned int const maxPaths,
        STPathSet& pathsOut,
        STPath& fullLiquidityPath) const;

    void processTransaction (
        Transaction::pointer& transaction,
        bool bAdmin,
        bool bLocal,
        NetworkOPs::FailHard failType);

    std::uint64_t scaleFeeBase (std::uint64_t fee) const;

    std::uint64_t scaleFeeLoad (std::uint64_t fee, bool bAdmin) const;

    bool hasAccountRoot () const;

    error_code_i singleAcctMatchesPubKey (
        RippleAddress const& publicKey) const;

    error_code_i multiAcctMatchesPubKey (
        AccountID const& acctID,
        RippleAddress const& publicKey) const;

    int getValidatedLedgerAge () const;

    bool isLoadedCluster () const;
};

// A function to auto-fill fees.
enum class AutoFill : unsigned char
{
    dont,
    might
};

Json::Value checkFee (
    Json::Value& request,
    TxnSignApiFacade& apiFacade,
    Role const role,
    AutoFill const doAutoFill);

} // namespace detail


/** Returns a Json::objectValue. */
Json::Value transactionSign (
    Json::Value params,  // Passed by value so it can be modified locally.
    NetworkOPs::FailHard failType,
    detail::TxnSignApiFacade& apiFacade,
    Role role);

/** Returns a Json::objectValue. */
inline
Json::Value transactionSign (
    Json::Value const& params,
    NetworkOPs::FailHard failType,
    NetworkOPs& netOPs,
    Role role)
{
    detail::TxnSignApiFacade apiFacade (netOPs);
    return transactionSign (params, failType, apiFacade, role);
}

/** Returns a Json::objectValue. */
Json::Value transactionSubmit (
    Json::Value params,  // Passed by value so it can be modified locally.
    NetworkOPs::FailHard failType,
    detail::TxnSignApiFacade& apiFacade,
    Role role);

/** Returns a Json::objectValue. */
inline
Json::Value transactionSubmit (
    Json::Value const& params,
    NetworkOPs::FailHard failType,
    NetworkOPs& netOPs,
    Role role)
{
    detail::TxnSignApiFacade apiFacade (netOPs);
    return transactionSubmit (params, failType, apiFacade, role);
}

/** Returns a Json::objectValue. */
Json::Value transactionSignFor (
    Json::Value params,  // Passed by value so it can be modified locally.
    NetworkOPs::FailHard failType,
    detail::TxnSignApiFacade& apiFacade,
    Role role);

/** Returns a Json::objectValue. */
inline
Json::Value transactionSignFor (
    Json::Value const& params,
    NetworkOPs::FailHard failType,
    NetworkOPs& netOPs,
    Role role)
{
    detail::TxnSignApiFacade apiFacade (netOPs);
    return transactionSignFor (params, failType, apiFacade, role);
}

/** Returns a Json::objectValue. */
Json::Value transactionSubmitMultiSigned (
    Json::Value params,  // Passed by value so it can be modified locally.
    NetworkOPs::FailHard failType,
    detail::TxnSignApiFacade& apiFacade,
    Role role);

/** Returns a Json::objectValue. */
inline
Json::Value transactionSubmitMultiSigned (
    Json::Value const& params,
    NetworkOPs::FailHard failType,
    NetworkOPs& netOPs,
    Role role)
{
    detail::TxnSignApiFacade apiFacade (netOPs);
    return transactionSubmitMultiSigned (params, failType, apiFacade, role);
}

} // RPC
} // ripple

#endif
