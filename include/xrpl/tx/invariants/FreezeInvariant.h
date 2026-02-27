#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <map>
#include <vector>

namespace xrpl {

/**
 * @brief Invariant: frozen trust line balance change is not allowed.
 *
 * We iterate all affected trust lines and ensure that they don't have
 * unexpected change of balance if they're frozen.
 */
class TransfersNotFrozen
{
    struct BalanceChange
    {
        std::shared_ptr<SLE const> const line;
        int const balanceChangeSign;
    };

    struct IssuerChanges
    {
        std::vector<BalanceChange> senders;
        std::vector<BalanceChange> receivers;
    };

    using ByIssuer = std::map<Issue, IssuerChanges>;
    ByIssuer balanceChanges_;

    std::map<AccountID, std::shared_ptr<SLE const> const> possibleIssuers_;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);

private:
    bool
    isValidEntry(std::shared_ptr<SLE const> const& before, std::shared_ptr<SLE const> const& after);

    STAmount
    calculateBalanceChange(
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after,
        bool isDelete);

    void
    recordBalance(Issue const& issue, BalanceChange change);

    void
    recordBalanceChanges(std::shared_ptr<SLE const> const& after, STAmount const& balanceChange);

    std::shared_ptr<SLE const>
    findIssuer(AccountID const& issuerID, ReadView const& view);

    bool
    validateIssuerChanges(
        std::shared_ptr<SLE const> const& issuer,
        IssuerChanges const& changes,
        STTx const& tx,
        beast::Journal const& j,
        bool enforce);

    bool
    validateFrozenState(
        BalanceChange const& change,
        bool high,
        STTx const& tx,
        beast::Journal const& j,
        bool enforce,
        bool globalFreeze);
};

}  // namespace xrpl
