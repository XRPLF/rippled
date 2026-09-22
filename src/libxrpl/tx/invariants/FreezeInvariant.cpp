#include <xrpl/tx/invariants/FreezeInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/invariants/InvariantCheckPrivilege.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace xrpl {

void
TransfersNotFrozen::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    /*
     * A trust line freeze state alone doesn't determine if a transfer is
     * frozen. The transfer must be examined "end-to-end" because both sides of
     * the transfer may have different freeze states and freeze impact depends
     * on the transfer direction. This is why first we need to track the
     * transfers using IssuerChanges senders/receivers.
     *
     * Only in validateIssuerChanges, after we collected all changes can we
     * determine if the transfer is valid.
     */
    if (!isValidEntry(before, after))
    {
        return;
    }

    auto const balances = calculateEffectiveBalances(before, after, isDelete);
    auto const balanceChange = balances.after - balances.before;
    if (balanceChange.signum() == 0)
    {
        return;
    }

    recordBalanceChanges(after, balances, balanceChange);
}

bool
TransfersNotFrozen::finalize(
    STTx const& tx,
    TER const ter,
    XRPAmount const fee,
    ReadView const& view,
    beast::Journal const& j)
{
    /*
     * We check this invariant regardless of deep freeze amendment status,
     * allowing for detection and logging of potential issues even when the
     * amendment is disabled.
     *
     * If an exploit that allows moving frozen assets is discovered,
     * we can alert operators who monitor fatal messages and trigger assert in
     * debug builds for an early warning.
     *
     * In an unlikely event that an exploit is found, this early detection
     * enables encouraging the UNL to expedite deep freeze amendment activation
     * or deploy hotfixes via new amendments. In case of a new amendment, we'd
     * only have to change this line setting 'enforce' variable.
     * enforce = view.rules().enabled(featureDeepFreeze) ||
     *           view.rules().enabled(fixFreezeExploit);
     */
    [[maybe_unused]] bool const enforce = view.rules().enabled(featureDeepFreeze);
    bool const fixOverrideFreeze = view.rules().enabled(fixCleanup3_4_0);

    /*
     * XLS-0066: a broker must be able to default an already-late loan
     * regardless of the vault asset's freeze state. LoanManage::defaultLoan
     * moves First-Loss Capital from the broker to the vault pseudo-account via
     * accountSend, which transits through the issuer in two hops (see
     * getLoanDefaultFreezeExemptAccounts), so a frozen issuer would otherwise
     * trip this invariant on either hop. Gated behind fixCleanup3_4_0, and
     * scoped to exactly the issuer/broker and issuer/vault lines involved for
     * the vault's own currency, so ledgers without the amendment (or an
     * unrelated frozen currency/line touched by the same transaction) keep
     * the current (blocking) behavior.
     */
    auto const loanDefaultAccounts = getLoanDefaultFreezeExemptAccounts(view, tx);

    return std::ranges::all_of(balanceChanges_, [&](auto const& entry) {
        auto const& [issue, changes] = entry;
        auto const issuerSle = findIssuer(issue.account, view);
        // It should be impossible for the issuer to not be found, but check
        // just in case so xrpld doesn't crash in release.
        if (!issuerSle)
        {
            // The comment above starting with "assert(enforce)" explains this
            // assert.
            XRPL_ASSERT(
                enforce,
                "xrpl::TransfersNotFrozen::finalize : enforce "
                "invariant.");
            return !enforce;
        }

        return validateIssuerChanges(
            issuerSle, changes, tx, j, enforce, fixOverrideFreeze, loanDefaultAccounts);
    });
}

bool
TransfersNotFrozen::isValidEntry(SLE::const_ref before, SLE::const_ref after)
{
    // `after` can never be null, even if the trust line is deleted.
    XRPL_ASSERT(after, "xrpl::TransfersNotFrozen::isValidEntry : valid after.");
    if (!after)
    {
        return false;
    }

    if (after->getType() == ltACCOUNT_ROOT)
    {
        possibleIssuers_.emplace(after->at(sfAccount), after);
        return false;
    }

    /* While LedgerEntryTypesMatch invariant also checks types, all invariants
     * are processed regardless of previous failures.
     *
     * This type check is still necessary here because it prevents potential
     * issues in subsequent processing.
     */
    return after->getType() == ltRIPPLE_STATE && (!before || before->getType() == ltRIPPLE_STATE);
}

TransfersNotFrozen::EffectiveBalances
TransfersNotFrozen::calculateEffectiveBalances(
    SLE::const_ref before,
    SLE::const_ref after,
    bool isDelete)
{
    auto const getBalance = [](auto const& line, auto const& other, bool zero) {
        STAmount const amt = line ? line->at(sfBalance) : other->at(sfBalance).zeroed();
        return zero ? amt.zeroed() : amt;
    };

    /* Trust lines can be created dynamically by other transactions such as
     * Payment and OfferCreate that cross offers. Such trust line won't be
     * created frozen, but the sender might be, so the starting balance must be
     * treated as zero.
     */
    auto const balanceBefore = getBalance(before, after, false);

    /* Same as above, trust lines can be dynamically deleted, and for frozen
     * trust lines, payments not involving the issuer must be blocked. This is
     * achieved by treating the final balance as zero when isDelete=true to
     * ensure frozen line restrictions are enforced even during deletion.
     */
    auto const balanceAfter = getBalance(after, before, isDelete);

    return {.before = balanceBefore, .after = balanceAfter};
}

void
TransfersNotFrozen::recordBalance(Issue const& issue, BalanceChange change)
{
    XRPL_ASSERT(
        change.balanceChangeSign,
        "xrpl::TransfersNotFrozen::recordBalance : valid trustline "
        "balance sign.");
    auto& changes = balanceChanges_[issue];
    if (change.balanceChangeSign < 0)
    {
        changes.senders.emplace_back(std::move(change));
    }
    else
    {
        changes.receivers.emplace_back(std::move(change));
    }
}

void
TransfersNotFrozen::recordBalanceChanges(
    SLE::const_ref after,
    EffectiveBalances const& balances,
    STAmount const& balanceChange)
{
    auto const balanceChangeSign = balanceChange.signum();
    auto const currency = after->at(sfBalance).get<Issue>().currency;

    /* Pre-fixCleanup3_5_0: this recorded every modified trust line under both
     * endpoints (high and low), treating each as if it were the issuer. That
     * conflates "party to a trust line" with "issuer of that currency": for a
     * line whose key account is a mere holder (perspective balance strictly
     * positive), the account's lsfGlobalFreeze and own-side freeze bits are
     * engine-irrelevant, but validateIssuerChanges would still apply them.
     * Combined with the fact that a same-currency cross-issuer trade (e.g.
     * USD.gwA <-> USD.gwB via a shared holder X) produces a two-sided group
     * under {USD, X} -- which the one-sided short-circuit in
     * validateIssuerChanges does not cover -- any holder that has set
     * asfGlobalFreeze or self-frozen a side of their own trust line would
     * silently and unavoidably trip tecINVARIANT_FAILED on every crossing
     * offer or routed payment, with no engine-level diagnostic. Because
     * Transactor::reset(fee)/ApplyContext::discard drop the apply view on
     * tecINVARIANT_FAILED, the offering account keeps its funding and its
     * offer, so a single attacker can grief an entire same-currency
     * cross-issuer book indefinitely at the cost of one owner reserve.
     *
     * Post-fixCleanup3_5_0: only record under an endpoint that is genuinely
     * acting as an issuer for this specific trust line during the change --
     * i.e., its perspective balance was strictly negative (owing tokens to
     * the counterparty) at some point across before/after. A pure holder
     * (perspective balance >= 0 both before and after) is not recorded on
     * that endpoint's group and its freeze flags no longer contribute
     * spurious violations. Groups keyed on genuine issuers are unchanged,
     * so pre-existing detections (issuer or authorized-privilege moving a
     * frozen holder's funds) still fire.
     */
    bool recordUnderHigh = true;
    bool recordUnderLow = true;
    if (isFeatureEnabled(fixCleanup3_5_0))
    {
        /* Trust-line balance is stored from the Low account's perspective:
         *   balance > 0  <=>  High owes Low  (Low is holder, High is issuer)
         *   balance < 0  <=>  Low owes High  (Low is issuer, High is holder)
         *   balance == 0 <=>  neither endpoint is currently an issuer
         * The High account's perspective balance is the negation, so:
         *   Low  ever an issuer  iff  min(before, after) <  0  (from Low)
         *   High ever an issuer  iff  max(before, after) >  0  (from Low)
         * balanceChange is nonzero here, so at least one endpoint is a
         * genuine issuer and at least one of the two records is emitted.
         */
        int const beforeSign = balances.before.signum();
        int const afterSign = balances.after.signum();
        recordUnderLow = beforeSign < 0 || afterSign < 0;
        recordUnderHigh = beforeSign > 0 || afterSign > 0;
    }

    if (recordUnderHigh)
    {
        // Change from low account's perspective, which is trust line default
        recordBalance(
            {currency, after->at(sfHighLimit).getIssuer()},
            {.line = after, .balanceChangeSign = balanceChangeSign});
    }

    if (recordUnderLow)
    {
        // Change from high account's perspective, which reverses the sign.
        recordBalance(
            {currency, after->at(sfLowLimit).getIssuer()},
            {.line = after, .balanceChangeSign = -balanceChangeSign});
    }
}

SLE::const_pointer
TransfersNotFrozen::findIssuer(AccountID const& issuerID, ReadView const& view)
{
    if (auto it = possibleIssuers_.find(issuerID); it != possibleIssuers_.end())
    {
        return it->second;
    }

    return view.read(keylet::account(issuerID));
}

bool
TransfersNotFrozen::validateIssuerChanges(
    SLE::const_ref issuer,
    IssuerChanges const& changes,
    STTx const& tx,
    beast::Journal const& j,
    bool enforce,
    bool fixOverrideFreeze,
    std::optional<LoanDefaultFreezeExemptAccounts> const& loanDefaultAccounts)
{
    if (!issuer)
    {
        return false;
    }

    bool const globalFreeze = issuer->isFlag(lsfGlobalFreeze);
    if (changes.receivers.empty() || changes.senders.empty())
    {
        /* If there are no receivers, then the holder(s) are returning
         * their tokens to the issuer. Likewise, if there are no
         * senders, then the issuer is issuing tokens to the holder(s).
         * This is allowed regardless of the issuer's freeze flags. (The
         * holder may have contradicting freeze flags, but that will be
         * checked when the holder is treated as issuer.)
         */
        return true;
    }

    for (auto const& actors : {changes.senders, changes.receivers})
    {
        for (auto const& change : actors)
        {
            bool const high = change.line->at(sfLowLimit).getIssuer() == issuer->at(sfAccount);

            if (!validateFrozenState(
                    change,
                    high,
                    tx,
                    j,
                    enforce,
                    globalFreeze,
                    fixOverrideFreeze,
                    loanDefaultAccounts))
            {
                return false;
            }
        }
    }
    return true;
}

bool
TransfersNotFrozen::validateFrozenState(
    BalanceChange const& change,
    bool high,
    STTx const& tx,
    beast::Journal const& j,
    bool enforce,
    bool globalFreeze,
    bool fixOverrideFreeze,
    std::optional<LoanDefaultFreezeExemptAccounts> const& loanDefaultAccounts)
{
    bool const freeze =
        change.balanceChangeSign < 0 && change.line->isFlag(high ? lsfLowFreeze : lsfHighFreeze);
    bool const deepFreeze = change.line->isFlag(high ? lsfLowDeepFreeze : lsfHighDeepFreeze);
    bool const frozen = globalFreeze || deepFreeze || freeze;

    if (!frozen)
    {
        return true;
    }

    // Pre-fixCleanup3_4_0: the isAMMLine check incorrectly blocked clawback on
    // individually-frozen or deep-frozen AMM trust lines.
    // Post-fixCleanup3_4_0: AMMClawbacks are allowed to override all freeze types.
    bool const isAMMLine = change.line->isFlag(lsfAMMNode);
    if ((fixOverrideFreeze || !isAMMLine || globalFreeze) &&
        hasPrivilege(tx, Privilege::OverrideFreeze))
    {
        JLOG(j.debug()) << "Invariant check allowing funds to be moved "
                        << (change.balanceChangeSign > 0 ? "to" : "from")
                        << " a frozen trustline for a freeze privileged transaction "
                        << tx.getTransactionID();
        return true;
    }

    // XLS-0066: LoanManage::defaultLoan's transfer is exempt from freeze (see
    // finalize()). Since neither the broker nor vault pseudo-account is the
    // asset's issuer, accountSend routes it as two hops through the issuer
    // (broker -> issuer, issuer -> vault), so both the issuer/broker and
    // issuer/vault lines are exempt -- but only for the vault's own currency,
    // so an unrelated frozen line (a different currency, or one touched by
    // the same transaction for some other reason) is still caught.
    if (loanDefaultAccounts && loanDefaultAccounts->asset.holds<Issue>() &&
        loanDefaultAccounts->asset.get<Issue>().currency ==
            change.line->at(sfBalance).get<Issue>().currency)
    {
        AccountID const lowAcct = change.line->at(sfLowLimit).getIssuer();
        AccountID const highAcct = change.line->at(sfHighLimit).getIssuer();
        auto const& accts = *loanDefaultAccounts;
        auto const isPair = [&](AccountID const& a, AccountID const& b) {
            return (lowAcct == a && highAcct == b) || (lowAcct == b && highAcct == a);
        };
        if (isPair(accts.issuer, accts.broker) || isPair(accts.issuer, accts.vault))
        {
            JLOG(j.debug()) << "Invariant check allowing funds to be moved "
                            << (change.balanceChangeSign > 0 ? "to" : "from")
                            << " a frozen trustline for LoanManage default "
                            << tx.getTransactionID();
            return true;
        }
    }

    JLOG(j.fatal()) << "Invariant failed: Attempting to move frozen funds for "
                    << tx.getTransactionID();
    // The comment above starting with "assert(enforce)" explains this assert.
    XRPL_ASSERT(
        enforce,
        "xrpl::TransfersNotFrozen::validateFrozenState : enforce "
        "invariant.");

    return !enforce;
}

}  // namespace xrpl
