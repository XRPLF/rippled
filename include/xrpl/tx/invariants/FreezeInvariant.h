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
        SLE::const_pointer const line;
        int const balanceChangeSign;
    };

    struct IssuerChanges
    {
        std::vector<BalanceChange> senders;
        std::vector<BalanceChange> receivers;
    };

    using ByIssuer = std::map<Issue, IssuerChanges>;
    ByIssuer balanceChanges_;

    std::map<AccountID, SLE::const_pointer const> possibleIssuers_;

public:
    void
    visitEntry(bool, SLE::const_ref, SLE::const_ref);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);

private:
    bool
    isValidEntry(SLE::const_ref before, SLE::const_ref after);

    static STAmount
    calculateBalanceChange(SLE::const_ref before, SLE::const_ref after, bool isDelete);

    void
    recordBalance(Issue const& issue, BalanceChange change);

    void
    recordBalanceChanges(SLE::const_ref after, STAmount const& balanceChange);

    SLE::const_pointer
    findIssuer(AccountID const& issuerID, ReadView const& view);

    static bool
    validateIssuerChanges(
        SLE::const_ref issuer,
        IssuerChanges const& changes,
        STTx const& tx,
        beast::Journal const& j,
        bool enforce);

    static bool
    validateFrozenState(
        BalanceChange const& change,
        bool high,
        STTx const& tx,
        beast::Journal const& j,
        bool enforce,
        bool globalFreeze);
};

}  // namespace xrpl
