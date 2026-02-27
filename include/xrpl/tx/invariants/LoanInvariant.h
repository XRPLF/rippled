#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <map>
#include <vector>

namespace xrpl {

/**
 * @brief Invariants: Loan brokers are internally consistent
 *
 * 1. If `LoanBroker.OwnerCount = 0` the `DirectoryNode` will have at most one
 *    node (the root), which will only hold entries for `RippleState` or
 * `MPToken` objects.
 *
 */
class ValidLoanBroker
{
    // Not all of these elements will necessarily be populated. Remaining items
    // will be looked up as needed.
    struct BrokerInfo
    {
        SLE::const_pointer brokerBefore = nullptr;
        // After is used for most of the checks, except
        // those that check changed values.
        SLE::const_pointer brokerAfter = nullptr;
    };
    // Collect all the LoanBrokers found directly or indirectly through
    // pseudo-accounts. Key is the brokerID / index. It will be used to find the
    // LoanBroker object if brokerBefore and brokerAfter are nullptr
    std::map<uint256, BrokerInfo> brokers_;
    // Collect all the modified trust lines. Their high and low accounts will be
    // loaded to look for LoanBroker pseudo-accounts.
    std::vector<SLE::const_pointer> lines_;
    // Collect all the modified MPTokens. Their accounts will be loaded to look
    // for LoanBroker pseudo-accounts.
    std::vector<SLE::const_pointer> mpts_;

    bool
    goodZeroDirectory(ReadView const& view, SLE::const_ref dir, beast::Journal const& j) const;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

/**
 * @brief Invariants: Loans are internally consistent
 *
 * 1. If `Loan.PaymentRemaining = 0` then `Loan.PrincipalOutstanding = 0`
 *
 */
class ValidLoan
{
    // Pair is <before, after>. After is used for most of the checks, except
    // those that check changed values.
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> loans_;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
