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

namespace ripple {
namespace RPC {

namespace RPCDetail {
// A class that allows these methods to be called with or without a
// real NetworkOPs instance.  This allows for unit testing.
class LedgerFacade
{
private:
    NetworkOPs* const netOPs_;
    Ledger::pointer ledger_;
    RippleAddress accountID_;
    AccountState::pointer accountState_;

public:
    // Enum used to construct a Facade for unit tests.
    enum NoNetworkOPs{
        noNetOPs
    };

    LedgerFacade () = delete;
    LedgerFacade (LedgerFacade const&) = delete;
    LedgerFacade& operator= (LedgerFacade const&) = delete;

    // For use in non unit testing circumstances.
    explicit LedgerFacade (NetworkOPs& netOPs)
    : netOPs_ (&netOPs)
    { }

    // For testTransactionRPC unit tests.
    explicit LedgerFacade (NoNetworkOPs noOPs)
    : netOPs_ (nullptr) { }

    // For testAutoFillFees unit tests.
    LedgerFacade (NoNetworkOPs noOPs, Ledger::pointer ledger)
    : netOPs_ (nullptr)
    , ledger_ (ledger)
    { }

    void snapshotAccountState (RippleAddress const& accountID);

    bool isValidAccount () const;

    std::uint32_t getSeq () const;

    bool findPathsForOneIssuer (
        RippleAddress const& dstAccountID,
        Issue const& srcIssue,
        STAmount const& dstAmount,
        int searchLevel,
        unsigned int const maxPaths,
        STPathSet& pathsOut,
        STPath& fullLiquidityPath) const;

    Transaction::pointer submitTransactionSync (
        Transaction::ref tpTrans,
        bool bAdmin,
        bool bLocal,
        bool bFailHard,
        bool bSubmit);

    std::uint64_t scaleFeeBase (std::uint64_t fee) const;

    std::uint64_t scaleFeeLoad (std::uint64_t fee, bool bAdmin) const;

    bool hasAccountRoot () const;

    bool accountMasterDisabled () const;

    bool accountMatchesRegularKey (Account account) const;

    int getValidatedLedgerAge () const;

    bool isLoadedCluster () const;
};

} // namespace RPCDetail


/** Returns a Json::objectValue. */
Json::Value transactionSign (
    Json::Value params,
    bool bSubmit,
    bool bFailHard,
    RPCDetail::LedgerFacade& ledgerFacade,
    Role role);

inline Json::Value transactionSign (
    Json::Value params,
    bool bSubmit,
    bool bFailHard,
    NetworkOPs& netOPs,
    Role role)
{
    RPCDetail::LedgerFacade ledgerFacade (netOPs);
    return transactionSign (params, bSubmit, bFailHard, ledgerFacade, role);
}

} // RPC
} // ripple

#endif
