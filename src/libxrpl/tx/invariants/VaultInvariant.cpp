#include <xrpl/tx/invariants/VaultInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/invariants/InvariantCheckPrivilege.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace xrpl {

namespace {

/*
 * True iff the recorded sfVaultKind identifies a closed-ended vault.
 * Centralizes the presence + enum-value check used by the phase-gate
 * invariants below.
 */
[[nodiscard]] bool
isClosedEnded(std::optional<std::uint8_t> const& vaultKind)
{
    return vaultKind && *vaultKind == std::to_underlying(VaultKind::ClosedEnded);
}

}  // namespace

ValidVault::Vault
ValidVault::Vault::make(SLE const& from)
{
    XRPL_ASSERT(from.getType() == ltVAULT, "ValidVault::Vault::make : from Vault object");

    ValidVault::Vault self;
    self.key = from.key();
    self.asset = from.at(sfAsset);
    self.pseudoId = from.getAccountID(sfAccount);
    self.owner = from.at(sfOwner);
    self.shareMPTID = from.getFieldH192(sfShareMPTID);
    self.assetsTotal = from.at(sfAssetsTotal);
    self.assetsAvailable = from.at(sfAssetsAvailable);
    self.assetsMaximum = from.at(sfAssetsMaximum);
    self.lossUnrealized = from.at(sfLossUnrealized);
    // Mirrors getVaultVersion: an absent or unrecognised sfLEVersion resolves
    // to the legacy, accrual-basis model.
    self.version = from[~sfLEVersion] == std::to_underlying(VaultVersion::CashBasis)
        ? VaultVersion::CashBasis
        : VaultVersion::Legacy;
    self.vaultKind = from[~sfVaultKind];
    self.subscriptionDate = from[~sfSubscriptionDate];
    self.redemptionDate = from[~sfRedemptionDate];
    return self;
}

ValidVault::Shares
ValidVault::Shares::make(SLE const& from)
{
    XRPL_ASSERT(
        from.getType() == ltMPTOKEN_ISSUANCE,
        "ValidVault::Shares::make : from MPTokenIssuance object");

    ValidVault::Shares self;
    self.share = MPTIssue(makeMptID(from.getFieldU32(sfSequence), from.getAccountID(sfIssuer)));
    self.sharesTotal = from.at(sfOutstandingAmount);
    self.sharesMaximum = from[~sfMaximumAmount].value_or(kMaxMpTokenAmount);
    self.flags = from.getFlags();
    return self;
}

Number
ValidVault::Loan::ownedToVault(VaultVersion version) const
{
    if (version == VaultVersion::CashBasis)
        return principalOutstanding;
    return totalValueOutstanding - managementFeeOutstanding;
}

ValidVault::Loan
ValidVault::Loan::make(SLE const& from)
{
    XRPL_ASSERT(from.getType() == ltLOAN, "ValidVault::Loan::make : from Loan object");

    ValidVault::Loan self;
    self.key = from.key();
    self.loanBrokerID = from.at(sfLoanBrokerID);
    self.borrower = from.at(sfBorrower);
    // sfLoanOriginationFee is optional on the ledger entry (absent when zero);
    // normalize to Number{0} so callers can read it unconditionally.
    self.originationFee = from[~sfLoanOriginationFee].value_or(Number{});
    self.principalOutstanding = from.at(sfPrincipalOutstanding);
    self.totalValueOutstanding = from.at(sfTotalValueOutstanding);
    self.managementFeeOutstanding = from.at(sfManagementFeeOutstanding);
    self.impaired = from.isFlag(lsfLoanImpaired);
    return self;
}

ValidVault::Broker
ValidVault::Broker::make(SLE const& from)
{
    XRPL_ASSERT(
        from.getType() == ltLOAN_BROKER, "ValidVault::Broker::make : from LoanBroker object");

    ValidVault::Broker self;
    self.key = from.key();
    self.owner = from.at(sfOwner);
    self.vaultID = from.at(sfVaultID);
    self.debtTotal = from.at(sfDebtTotal);
    self.coverAvailable = from.at(sfCoverAvailable);
    return self;
}

void
ValidVault::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    // If `before` is empty, this means an object is being created, in which
    // case `isDelete` must be false. Otherwise `before` and `after` are set and
    // `isDelete` indicates whether an object is being deleted or modified.
    XRPL_ASSERT(
        after != nullptr && (before != nullptr || !isDelete),
        "xrpl::ValidVault::visitEntry : some object is available");

    // Number balanceDelta will capture the difference (delta) between "before"
    // state (zero if created) and "after" state (zero if destroyed), and
    // preserves value scale (exponent) to round values to the same scale during
    // validation. It is used to validate that the change in account
    // balances matches the change in vault balances, stored to deltas_ at the
    // end of this function.
    DeltaInfo balanceDelta{.delta = kNumZero, .scale = std::nullopt};

    std::int8_t sign = 0;
    if (before)
    {
        switch (before->getType())
        {
            case ltVAULT:
                beforeVault_.push_back(Vault::make(*before));
                break;
            case ltMPTOKEN_ISSUANCE:
                // At this moment we have no way of telling if this object holds
                // vault shares or something else. Save it for finalize.
                beforeMPTs_.push_back(Shares::make(*before));
                balanceDelta.delta =
                    static_cast<std::int64_t>(before->getFieldU64(sfOutstandingAmount));
                // MPTs are ints, so the scale is always 0.
                balanceDelta.scale = 0;
                sign = 1;
                break;
            case ltMPTOKEN:
                balanceDelta.delta = static_cast<std::int64_t>(before->getFieldU64(sfMPTAmount));
                // MPTs are ints, so the scale is always 0.
                balanceDelta.scale = 0;
                sign = -1;
                break;
            case ltACCOUNT_ROOT:
                balanceDelta.delta = before->getFieldAmount(sfBalance);
                // Account balance is XRP, which is an int, so the scale is
                // always 0.
                balanceDelta.scale = 0;
                sign = -1;
                break;
            case ltRIPPLE_STATE: {
                auto const amount = before->getFieldAmount(sfBalance);
                balanceDelta.delta = amount;
                // Trust Line balances are STAmounts, so we can use the exponent
                // directly to get the scale.
                balanceDelta.scale = amount.exponent();
                sign = -1;
                break;
            }
            case ltLOAN:
                // A loan carries no vault balance of its own; capture its prior
                // state so the loan pay invariant can verify the change in the
                // amount this loan owes to the vault.
                beforeLoan_.push_back(Loan::make(*before));
                break;
            case ltLOAN_BROKER:
                // Snapshot the broker so the lending-side finalizers can compute
                // deltas on DebtTotal, CoverAvailable and OwnerCount without a
                // separate read of the after-state.
                beforeBroker_.push_back(Broker::make(*before));
                break;
            default:;
        }
    }

    if (!isDelete && after)
    {
        switch (after->getType())
        {
            case ltVAULT:
                afterVault_.push_back(Vault::make(*after));
                break;
            case ltMPTOKEN_ISSUANCE:
                // At this moment we have no way of telling if this object holds
                // vault shares or something else. Save it for finalize.
                afterMPTs_.push_back(Shares::make(*after));
                balanceDelta.delta -=
                    Number(static_cast<std::int64_t>(after->getFieldU64(sfOutstandingAmount)));
                // MPTs are ints, so the scale is always 0.
                balanceDelta.scale = 0;
                sign = 1;
                break;
            case ltMPTOKEN:
                balanceDelta.delta -=
                    Number(static_cast<std::int64_t>(after->getFieldU64(sfMPTAmount)));
                // MPTs are ints, so the scale is always 0.
                balanceDelta.scale = 0;
                sign = -1;
                break;
            case ltACCOUNT_ROOT:
                balanceDelta.delta -= Number(after->getFieldAmount(sfBalance));
                // Account balance is XRP, which is an int, so the scale is
                // always 0.
                balanceDelta.scale = 0;
                sign = -1;
                break;
            case ltRIPPLE_STATE: {
                auto const amount = after->getFieldAmount(sfBalance);
                balanceDelta.delta -= Number(amount);
                // Trust Line balances are STAmounts, so we can use the exponent
                // directly to get the scale.
                if (amount.exponent() > balanceDelta.scale)
                    balanceDelta.scale = amount.exponent();
                sign = -1;
                break;
            }
            case ltLOAN:
                // A loan carries no vault balance of its own; capture it so the
                // loan set and loan pay invariants can verify the interest
                // booked to the vault and the change in the amount this loan
                // owes to the vault.
                afterLoan_.push_back(Loan::make(*after));
                break;
            case ltLOAN_BROKER:
                // See beforeBroker_; captured on both sides so the funding
                // invariants can read `Δ DebtTotal` directly.
                afterBroker_.push_back(Broker::make(*after));
                break;
            default:;
        }
    }

    uint256 const key = (before ? before->key() : after->key());
    // Append to deltas if sign is non-zero, i.e. an object of an interesting
    // type has been updated. A transaction may update an object even when
    // its balance has not changed, e.g. transaction fee equals the amount
    // transferred to the account. We intentionally do not compare balanceDelta
    // against zero, to avoid missing such updates.
    if (sign != 0)
    {
        XRPL_ASSERT_PARTS(balanceDelta.scale, "xrpl::ValidVault::visitEntry", "scale initialized");
        balanceDelta.delta *= sign;
        deltas_[key] = balanceDelta;
    }

    // Record every touched MPToken's issuance so the non-transferable
    // vault-shares check in finalize can tell whether any holder of the
    // issuance was moved. Uses whichever of before/after carries the
    // identifying field.
    auto const isMPToken = [](SLE::const_ref sle) { return sle && sle->getType() == ltMPTOKEN; };
    if (isMPToken(before) || isMPToken(after))
    {
        auto const& identity = before ? before : after;
        touchedShareIssuances_.insert(identity->getFieldH192(sfMPTokenIssuanceID));
    }
}

std::optional<ValidVault::DeltaInfo>
ValidVault::deltaAssets(AccountID const& id) const
{
    auto const& vaultAsset = afterVault_[0].asset;
    auto const lookup = [&](uint256 const& key) -> std::optional<DeltaInfo> {
        auto const it = deltas_.find(key);
        if (it == deltas_.end())
            return std::nullopt;
        return it->second;
    };

    return std::visit(
        [&]<typename TIss>(TIss const& issue) -> std::optional<DeltaInfo> {
            if constexpr (std::is_same_v<TIss, Issue>)
            {
                if (isXRP(issue))
                    return lookup(keylet::account(id).key);
                auto result = lookup(keylet::trustLine(id, issue).key);
                // Trust-line balance is stored from the low-account's perspective;
                // negate if id is the high account so the delta is in id's terms.
                if (result && id > issue.getIssuer())
                    result->delta = -result->delta;
                return result;
            }
            else if constexpr (std::is_same_v<TIss, MPTIssue>)
            {
                return lookup(keylet::mptoken(issue.getMptID(), id).key);
            }
        },
        vaultAsset.value());
}

std::optional<ValidVault::DeltaInfo>
ValidVault::deltaAssetsTxAccount(STTx const& tx, XRPAmount fee) const
{
    auto const& vaultAsset = afterVault_[0].asset;
    auto ret = deltaAssets(tx[sfAccount]);
    if (!ret.has_value() || !vaultAsset.native())
        return ret;

    // Only add the fee back if tx[sfAccount] actually paid it. When the fee is
    // paid by someone else (a delegate or a fee sponsor), the
    // account's XRP balance moved only by the vault amount.
    if (tx.getFeePayerID() != tx[sfAccount])
        return ret;

    ret->delta += fee.drops();
    if (ret->delta == kZero)
        return std::nullopt;

    return ret;
}

std::optional<ValidVault::DeltaInfo>
ValidVault::deltaShares(AccountID const& id) const
{
    auto const& afterVault = afterVault_[0];
    auto const it = [&]() {
        if (id == afterVault.pseudoId)
            return deltas_.find(keylet::mptokenIssuance(afterVault.shareMPTID).key);
        return deltas_.find(keylet::mptoken(afterVault.shareMPTID, id).key);
    }();

    return it != deltas_.end() ? std::optional<DeltaInfo>(it->second) : std::nullopt;
}

bool
ValidVault::isVaultEmpty(Vault const& vault)
{
    return vault.assetsAvailable == 0 && vault.assetsTotal == 0;
}

bool
ValidVault::exactlyOneLoan(LoanOp op, beast::Journal const& j) const
{
    if (afterLoan_.size() != 1)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: lending transaction must touch exactly one loan";
        return false;
    }

    bool const isCreate = op == LoanOp::Create;
    // A created loan has no prior state; a modified one must be the very loan
    // whose prior state was captured, otherwise the deltas computed from the
    // two snapshots are meaningless.
    if (isCreate ? !beforeLoan_.empty()
                 : (beforeLoan_.size() != 1 || beforeLoan_[0].key != afterLoan_[0].key))
    {
        JLOG(j.fatal()) <<  //
            (isCreate ? "Invariant failed: lending transaction must not modify an existing loan"
                      : "Invariant failed: lending transaction must modify exactly one loan");
        return false;
    }

    return true;
}

bool
ValidVault::checkLoanFunding(
    STTx const& tx,
    XRPAmount const fee,
    ReadView const& view,
    beast::Journal const& j) const
{
    XRPL_ASSERT(
        !beforeVault_.empty(), "xrpl::ValidVault::checkLoanFunding : loan set updated a vault");
    XRPL_ASSERT(
        !afterLoan_.empty(), "xrpl::ValidVault::checkLoanFunding : loan cardinality enforced");

    auto const& beforeVault = beforeVault_[0];
    auto const& afterVault = afterVault_[0];
    auto const& vaultAsset = afterVault.asset;
    auto const& loan = afterLoan_[0];

    // Funding a loan moves the requested principal out of the vault
    // pseudo-account to the borrower (and, if any, the origination
    // fee to the broker owner).
    auto const maybeVaultDeltaAssets = deltaAssets(afterVault.pseudoId);
    if (!maybeVaultDeltaAssets)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: loan set must change vault balance";
        return false;  // That's all we can do
    }

    // Get the posterior scale to round calculations to
    auto const minScale = computeVaultMinScale(*maybeVaultDeltaAssets, view.rules());

    // The vault only releases the requested principal, which must
    // match the reduction of both the vault (pseudo-account) balance
    // and the assets available.
    auto const principalDelta = roundToAsset(vaultAsset, -tx[sfPrincipalRequested], minScale);

    bool result = true;
    auto const vaultDeltaAssets = roundToAsset(vaultAsset, maybeVaultDeltaAssets->delta, minScale);
    if (vaultDeltaAssets != principalDelta)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: loan set must decrease vault balance by the principal requested";
        result = false;
    }

    // The created loan must record exactly the principal the vault
    // released. Otherwise the amount the loan owes to the vault (and
    // thus the assets booked back to the vault on repayment) is
    // decoupled from the assets actually lent, which would skew the
    // vault's share price.
    if (loan.principalOutstanding != tx[sfPrincipalRequested])
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: loan set principal outstanding must equal principal requested";
        result = false;
    }

    // The remaining participant-side checks are new under featureLendingProtocolV1_1
    // and use the basis-aware ownedToVault() accessor; keep them behind that gate
    // so pre-V1_1 behaviour is unchanged.
    if (!view.rules().enabled(featureLendingProtocolV1_1))
        return result;

    // The broker whose DebtTotal must reflect the newly-originated loan is the
    // one the loan points at. Verify the snapshot corresponds and is populated
    // on both sides - a loan set that failed to modify the referenced broker
    // is itself an invariant violation.
    if (beforeBroker_.size() != 1 || afterBroker_.size() != 1 ||
        beforeBroker_[0].key != afterBroker_[0].key || afterBroker_[0].key != loan.loanBrokerID)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: loan set must modify exactly the loan's broker";
        return false;  // That's all we can do
    }

    auto const& beforeBroker = beforeBroker_[0];
    auto const& afterBroker = afterBroker_[0];

    // The broker's DebtTotal tracks the aggregate amount its loans owe to the
    // vault (basis-aware). A new loan's contribution is what it owes to the
    // vault at origination, so `Δ DebtTotal == loan.ownedToVault(version)`.
    // DebtTotal is written via adjustImpreciseNumber (rounded to the vault
    // scale), so compare via a once-rounded residual to avoid a false failure
    // from independently-rounded operands.
    {
        auto const expected = loan.ownedToVault(afterVault.version);
        auto const residual = roundToAsset(
            vaultAsset, (afterBroker.debtTotal - beforeBroker.debtTotal) - expected, minScale);
        if (residual != kZero)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: loan set must increase broker debt total by the amount the "
                "new loan owes to the vault";
            result = false;
        }
    }

    // Value routing: the vault pseudo-account paid out `principalRequested`,
    // split between the borrower (`principalRequested - originationFee`) and
    // the broker owner (`originationFee`). Verify each participant received
    // their portion. `deltaAssets` reads the raw balance change; when the
    // participant also paid the transaction fee (XRP vaults only) that fee
    // must be added back so the observed delta reflects the vault-side flow
    // alone.
    auto const adjustForFee = [&](std::optional<DeltaInfo>& d, AccountID const& id) {
        if (d && vaultAsset.native() && tx.getFeePayerID() == id)
            d->delta += fee.drops();
    };

    auto maybeBorrowerDelta = deltaAssets(loan.borrower);
    adjustForFee(maybeBorrowerDelta, loan.borrower);

    auto const borrowerExpected =
        roundToAsset(vaultAsset, tx[sfPrincipalRequested] - loan.originationFee, minScale);
    auto const borrowerReceived =
        maybeBorrowerDelta ? roundToAsset(vaultAsset, maybeBorrowerDelta->delta, minScale) : kZero;
    if (borrowerReceived != borrowerExpected)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: loan set must credit the borrower with the principal net of "
            "origination fee";
        result = false;
    }

    // The broker owner is only touched when a non-zero origination fee is
    // routed - a zero-fee loan set leaves them unchanged, so a missing delta
    // is consistent with `originationFee == 0`.
    auto maybeBrokerOwnerDelta = deltaAssets(afterBroker.owner);
    adjustForFee(maybeBrokerOwnerDelta, afterBroker.owner);

    auto const brokerOwnerExpected = roundToAsset(vaultAsset, loan.originationFee, minScale);
    auto const brokerOwnerReceived = maybeBrokerOwnerDelta
        ? roundToAsset(vaultAsset, maybeBrokerOwnerDelta->delta, minScale)
        : kZero;
    if (brokerOwnerReceived != brokerOwnerExpected)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: loan set must credit the broker owner with the origination fee";
        result = false;
    }

    // Vault-side accounting identity at origination, mirroring the one enforced
    // for loan payments: `Δ AssetsTotal - Δ AssetsAvailable == Δ ownedToVault`.
    // Before the transaction the loan did not exist, so
    // `Δ ownedToVault == loan.ownedToVault(version)`. Basis-aware via
    // ownedToVault(): under accrual it books the interest into AssetsTotal,
    // under cash-basis it does not. The residual is rounded once for the same
    // reason as in finalizeLoanPay - the underlying identity holds exactly, but
    // a term-wise comparison of independently-rounded operands can drift by a
    // ULP.
    {
        auto const residual = roundToAsset(
            vaultAsset,
            (afterVault.assetsTotal - beforeVault.assetsTotal) -
                (afterVault.assetsAvailable - beforeVault.assetsAvailable) -
                loan.ownedToVault(afterVault.version),
            minScale);
        if (residual != kZero)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: loan set assets outstanding must match the principal released "
                "and the amount the new loan owes to the vault";
            result = false;
        }
    }

    return result;
}

bool
ValidVault::finalizeLoanSet(
    STTx const& tx,
    XRPAmount const fee,
    ReadView const& view,
    beast::Journal const& j) const
{
    if (!view.rules().enabled(featureLendingProtocolV1_1))
        return true;

    if (afterVault_.empty())
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::ValidVault::finalizeLoanSet : vault exists");
        return false;
        // LCOV_EXCL_STOP
    }

    auto const& afterVault = afterVault_[0];

    // Loan origination against a closed-ended vault is only permitted while the vault is in the
    // Investment phase - strictly past SubscriptionDate and before RedemptionDate. Open-ended
    // vaults have NoPhase and are unaffected by the phase gate, but the funding checks below
    // apply to every vault kind, so NoPhase must fall through rather than return early.
    auto const phase = getVaultPhase(
        view, afterVault.vaultKind, afterVault.subscriptionDate, afterVault.redemptionDate);
    if (phase != VaultPhase::NoPhase && phase != VaultPhase::Investment)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: loan origination only allowed in Investment phase";
        return false;
    }

    // A loan set must create exactly one loan object; the interest
    // it books is the only permitted change to assets outstanding.
    if (!exactlyOneLoan(LoanOp::Create, j))
    {
        return false;
    }

    return checkLoanFunding(tx, fee, view, j);
}

std::int32_t
ValidVault::computeVaultMinScale(DeltaInfo const& vaultDelta, Rules const& rules) const
{
    // Returns the posterior `assetsTotal` scale.
    //
    // 1. Because STAmounts are normalized, `assetsTotal` (being >= `assetsAvailable`)
    // safely represents the coarsest exponent needed for both fields.
    //
    // 2. The scale may decrease (withdraw/clawback) or increase (deposit). In both cases
    // we ensure the vault is in a legitimate state in the post-transaction scale.
    auto const& afterVault = afterVault_[0];
    auto const& vaultAsset = afterVault.asset;
    if (rules.enabled(fixCleanup3_2_0))
    {
        NumberRoundModeGuard const roundGuard(Number::RoundingMode::ToNearest);
        return scale(afterVault.assetsTotal, vaultAsset);
    }

    auto const& beforeVault = beforeVault_[0];
    auto const totalDelta =
        DeltaInfo::makeDelta(beforeVault.assetsTotal, afterVault.assetsTotal, vaultAsset);
    auto const availableDelta =
        DeltaInfo::makeDelta(beforeVault.assetsAvailable, afterVault.assetsAvailable, vaultAsset);
    return computeCoarsestScale({vaultDelta, totalDelta, availableDelta});
}

bool
ValidVault::finalizeLoanManage(STTx const& tx, ReadView const& view, beast::Journal const& j) const
{
    if (!view.rules().enabled(featureLendingProtocolV1_1))
        return true;

    bool result = true;

    XRPL_ASSERT(
        !beforeVault_.empty(),
        "xrpl::ValidVault::finalizeLoanManage : loan manage updated a vault");
    auto const& beforeVault = beforeVault_[0];
    auto const& afterVault = afterVault_[0];
    auto const& vaultAsset = afterVault.asset;

    // Every sub-operation acts on the single loan named by the transaction. The
    // vault-only checks below do not read the loan, so they are still performed
    // when this fails; only the checks which need the loan are skipped.
    bool const oneLoan = exactlyOneLoan(LoanOp::Modify, j);
    result = result && oneLoan;

    // Loan management (impair / unimpair / default) never removes
    // assets from the vault. Only a default returns first-loss
    // capital from the broker to the vault pseudo-account; impair
    // and unimpair merely adjust the paper (unrealized) loss and
    // touch no balances. A missing vault-balance delta is a legitimate
    // outcome for impair / unimpair (they move no funds), but on default
    // the broker returns first-loss capital, so the vault balance ledger
    // entry must have moved - a missing delta indicates a real accounting
    // bug and is called out below rather than silently absorbed by the
    // value_or fallback.
    auto const maybeVaultDeltaAssets = deltaAssets(afterVault.pseudoId);
    if (tx.isFlag(tfLoanDefault) && !maybeVaultDeltaAssets)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: loan default must change vault balance";
        result = false;
    }
    auto const vaultDelta = maybeVaultDeltaAssets.value_or(
        DeltaInfo{.delta = kZero, .scale = scale(afterVault.assetsTotal, vaultAsset)});

    // Get the posterior scale to round calculations to
    auto const minScale = computeVaultMinScale(vaultDelta, view.rules());

    auto const assetAvailableDelta = roundToAsset(
        vaultAsset, afterVault.assetsAvailable - beforeVault.assetsAvailable, minScale);
    auto const assetTotalDelta =
        roundToAsset(vaultAsset, afterVault.assetsTotal - beforeVault.assetsTotal, minScale);
    // Loss unrealized is maintained at the vault scale, so the delta is rounded
    // the same way as the asset fields above.
    auto const lossUnrealizedDelta =
        roundToAsset(vaultAsset, afterVault.lossUnrealized - beforeVault.lossUnrealized, minScale);

    // --- Checks specific to each loan manage sub-operation ---

    if (tx.isFlag(tfLoanImpair) || tx.isFlag(tfLoanUnimpair))
    {
        // Impair / unimpair only move the paper (unrealized) loss;
        // they touch neither balances nor assets.
        if (assetAvailableDelta != kZero)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: loan impair/unimpair must not "
                "change assets available";
            result = false;
        }

        if (assetTotalDelta != kZero)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: loan impair/unimpair must not "
                "change assets outstanding";
            result = false;
        }

        // Impairing records the amount the loan owes to the vault as a paper
        // loss, unimpairing reverses it. The bounds are not strict because
        // either adjustment can round to nothing at the vault scale.
        if (tx.isFlag(tfLoanImpair) ? lossUnrealizedDelta < kZero : lossUnrealizedDelta > kZero)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: loan impair must not decrease, and loan "
                "unimpair must not increase, loss unrealized";
            result = false;
        }

        // Magnitude: LossUnrealized must move by exactly the amount the loan
        // owed to the vault snapshotted before this transaction. Impair grows
        // it, unimpair shrinks it. The residual is rounded once (mirrors the
        // LoanPay conservation identity in finalizeLoanPay) so that comparing
        // the two independently-scaled operands cannot drift by a ULP and
        // produce a false failure.
        if (oneLoan)
        {
            auto const owed = beforeLoan_[0].ownedToVault(afterVault.version);
            auto const expectedDelta = tx.isFlag(tfLoanImpair) ? owed : -owed;
            auto const residual = roundToAsset(
                vaultAsset,
                (afterVault.lossUnrealized - beforeVault.lossUnrealized) - expectedDelta,
                minScale);
            if (residual != kZero)
            {
                JLOG(j.fatal()) <<  //
                    (tx.isFlag(tfLoanImpair)
                         ? "Invariant failed: loan impair must increase loss unrealized "
                           "by exactly the amount the loan owes to the vault"
                         : "Invariant failed: loan unimpair must decrease loss unrealized "
                           "by exactly the amount the loan owes to the vault");
                result = false;
            }
        }
    }
    else if (tx.isFlag(tfLoanDefault))
    {
        // A default returns first-loss capital to the vault, so
        // assets available (and the vault balance) may only grow.
        if (assetAvailableDelta < kZero)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: loan default must not decrease "
                "assets available";
            result = false;
        }

        // A default realizes (writes off) the uncovered portion of
        // the loan, so assets outstanding may only shrink.
        if (assetTotalDelta > kZero)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: loan default must not increase "
                "assets outstanding";
            result = false;
        }

        // Vault-side conservation identity, mirroring the ones enforced for
        // loan origination (checkLoanFunding) and loan payment
        // (finalizeLoanPay):
        // `Δ AssetsTotal - Δ AssetsAvailable - Δ ownedToVault(version) == 0`.
        // On default the loan zeroes, so
        // `Δ ownedToVault == -beforeLoan.ownedToVault(version)`, which under
        // cash-basis is `-PrincipalOutstanding` and under accrual is
        // `-(TotalValueOutstanding - ManagementFeeOutstanding)`, matching
        // XLS-66 §3.10.5 / §3.10.5.1. The residual is rounded once for the same
        // reason as the other two identities - term-wise comparison of
        // independently-rounded operands can drift by a ULP. Basis-aware via
        // ownedToVault(). Depends on the loan cardinality, so guarded by
        // oneLoan.
        if (oneLoan)
        {
            auto const residual = roundToAsset(
                vaultAsset,
                (afterVault.assetsTotal - beforeVault.assetsTotal) -
                    (afterVault.assetsAvailable - beforeVault.assetsAvailable) -
                    (afterLoan_[0].ownedToVault(afterVault.version) -
                     beforeLoan_[0].ownedToVault(afterVault.version)),
                minScale);
            if (residual != kZero)
            {
                JLOG(j.fatal()) <<  //
                    "Invariant failed: loan default assets outstanding must "
                    "match the first-loss capital received and the change in "
                    "the amount the loan owes to the vault";
                result = false;
            }
        }

        // A default realizes the loss: any paper loss carried for this loan is
        // released, and no new paper loss may be recorded. As above, the bound
        // is not strict - the loan need not have been impaired, in which case
        // there was no paper loss to release.
        if (lossUnrealizedDelta > kZero)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: loan default must not increase loss "
                "unrealized";
            result = false;
        }

        // Magnitude: if the loan was impaired before this transaction the
        // amount it owed to the vault (pre-tx) was carried as an unrealized
        // loss, and default releases exactly that amount (the loss transitions
        // from paper to realized). If the loan was not impaired there is
        // nothing to release and LossUnrealized is unchanged. As with
        // impair/unimpair the residual is rounded once.
        if (oneLoan)
        {
            Number const expectedDelta = beforeLoan_[0].impaired
                ? -beforeLoan_[0].ownedToVault(afterVault.version)
                : kZero;
            auto const residual = roundToAsset(
                vaultAsset,
                (afterVault.lossUnrealized - beforeVault.lossUnrealized) - expectedDelta,
                minScale);
            if (residual != kZero)
            {
                JLOG(j.fatal()) <<  //
                    "Invariant failed: loan default must decrease loss "
                    "unrealized by the pre-transaction amount the loan owed to "
                    "the vault when impaired, or leave it unchanged otherwise";
                result = false;
            }
        }

        // The first-loss capital the vault receives comes out of the
        // loan-broker pseudo-account, so the two balances must move by exactly
        // opposite amounts. A default that credited the vault from anywhere
        // else would manufacture value. The broker can only be located through
        // the defaulted loan, so this is skipped when that loan is unknown.
        if (oneLoan)
        {
            auto const brokerSle = view.read(keylet::loanBroker(afterLoan_[0].loanBrokerID));
            if (!brokerSle)
            {
                JLOG(j.fatal()) <<  //
                    "Invariant failed: loan default loan broker must exist";
                result = false;
            }
            else
            {
                auto const maybeBrokerDelta = deltaAssets(brokerSle->at(sfAccount));
                auto const brokerDelta = maybeBrokerDelta.value_or(
                    DeltaInfo{.delta = kZero, .scale = scale(afterVault.assetsTotal, vaultAsset)});
                auto const coverScale =
                    std::max(minScale, computeCoarsestScale({vaultDelta, brokerDelta}));
                auto const coverResidual =
                    roundToAsset(vaultAsset, vaultDelta.delta + brokerDelta.delta, coverScale);
                if (coverResidual != kZero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: loan default must move the first-loss "
                        "capital from the loan broker to the vault";
                    result = false;
                }
            }
        }

        // Under featureLendingProtocolV1_1 the broker snapshot lets us tie the
        // vault accounting field, the vault balance, and the broker's cover
        // together as a single identity. The pre-V1_1 cover-residual above only
        // catches asymmetric movements; a default that touched neither side
        // would slip through it because both deltas fall back to zero.
        if (oneLoan)
        {
            if (beforeBroker_.size() != 1 || afterBroker_.size() != 1 ||
                afterBroker_[0].key != afterLoan_[0].loanBrokerID)
            {
                JLOG(j.fatal()) <<  //
                    "Invariant failed: loan default must modify exactly the loan's broker";
                result = false;
            }
            else
            {
                auto const& beforeBroker = beforeBroker_[0];
                auto const& afterBroker = afterBroker_[0];

                // If the broker returned first-loss capital, the vault balance
                // ledger entry must reflect it. Redundant with the identity
                // below when combined with the universal AssetsAvailable / vault
                // balance check, but stated explicitly to catch the specific
                // "touched neither" bug.
                if (beforeBroker.coverAvailable != afterBroker.coverAvailable &&
                    !maybeVaultDeltaAssets)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: loan default must change vault balance when first-loss "
                        "capital is returned";
                    result = false;
                }

                // Δ AssetsAvailable == DefaultCovered: the vault credits its
                // available assets by exactly what the broker released. Rounded
                // once to avoid a false failure from independently-rounded
                // operands - beforeBroker.coverAvailable/afterBroker.coverAvailable
                // are stored at vault scale (adjustImpreciseNumber in LoanManage),
                // and defaultCovered itself is rounded at loan scale.
                auto const defaultCovered =
                    beforeBroker.coverAvailable - afterBroker.coverAvailable;
                auto const availableResidual = roundToAsset(
                    vaultAsset,
                    (afterVault.assetsAvailable - beforeVault.assetsAvailable) - defaultCovered,
                    minScale);
                if (availableResidual != kZero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: loan default must increase assets available by the "
                        "default covered amount";
                    result = false;
                }

                // Broker debt tracks the aggregate amount the broker's loans
                // owe to the vault: on default the loan's amount owed drops to
                // zero (all balance fields are zeroed), and DebtTotal drops by
                // the same amount (LoanManage.cpp:244 decrements by
                // loanVaultExposure). Same delta identity as finalizeLoanPay
                // (items 22/23), specialised to the default sub-op.
                auto const brokerResidual = roundToAsset(
                    vaultAsset,
                    (afterBroker.debtTotal - beforeBroker.debtTotal) -
                        (afterLoan_[0].ownedToVault(afterVault.version) -
                         beforeLoan_[0].ownedToVault(afterVault.version)),
                    minScale);
                if (brokerResidual != kZero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: loan default broker debt total must "
                        "track the change in the amount the loan owes to the "
                        "vault";
                    result = false;
                }
            }
        }
    }
    else
    {
        // A loan manage with none of the sub-operation flags
        // (impair, unimpair, default) is a no-op and must not
        // modify the vault.
        JLOG(j.fatal()) <<  //
            "Invariant failed: loan manage without a sub-operation "
            "must not modify the vault";
        result = false;
    }

    return result;
}

bool
ValidVault::finalizeLoanPay(STTx const& tx, ReadView const& view, beast::Journal const& j) const
{
    if (!view.rules().enabled(featureLendingProtocolV1_1))
        return true;

    bool result = true;

    XRPL_ASSERT(
        !beforeVault_.empty(), "xrpl::ValidVault::finalizeLoanPay : loan pay updated a vault");
    auto const& beforeVault = beforeVault_[0];
    auto const& afterVault = afterVault_[0];
    auto const& vaultAsset = afterVault.asset;

    // A payment is made against the single loan named by the transaction. The
    // cash-flow checks immediately below do not read the loan, so they are still
    // performed when this fails; the loan-dependent ones are then skipped.
    bool const oneLoan = exactlyOneLoan(LoanOp::Modify, j);

    // A loan payment moves the paid principal and interest into the
    // vault pseudo-account (fees go to the broker), so the vault
    // balance and the assets available both increase by that amount.
    auto const maybeVaultDeltaAssets = deltaAssets(afterVault.pseudoId);
    if (!maybeVaultDeltaAssets)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: loan pay must change vault balance";
        return false;  // That's all we can do
    }

    // Get the posterior scale to round calculations to
    auto const minScale = computeVaultMinScale(*maybeVaultDeltaAssets, view.rules());

    auto const assetAvailableDelta = roundToAsset(
        vaultAsset, afterVault.assetsAvailable - beforeVault.assetsAvailable, minScale);

    // A payment always adds funds to the vault, so assets available
    // (and the vault balance) must increase.
    if (assetAvailableDelta <= kZero)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: loan pay must increase assets "
            "available";
        result = false;
    }

    // The vault only receives the principal and interest portion of
    // the borrower's payment (the fee goes to the broker), so it can
    // never grow by more than the amount paid.
    auto const amountPaid = roundToAsset(vaultAsset, tx[sfAmount], minScale);
    if (assetAvailableDelta > amountPaid)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: loan pay must not increase assets "
            "available by more than the amount paid";
        result = false;
    }

    if (!oneLoan)
        return false;  // That's all we can do

    // The vault, the broker pseudo-account and the broker owner are
    // the only three destinations of a loan payment (the fee goes to
    // either the pseudo-account or the owner, never both).  Their
    // combined inflow can never exceed the amount actually paid by
    // the borrower — anything more would mean the transaction
    // manufactured value.
    //
    // If the broker owner is also the borrower, its delta captures a
    // net movement rather than a pure inflow, and the sum degrades
    // to a trivially-satisfied comparison.  That is safe (no
    // false-positives); the split correctness in that corner case
    // is still policed by the assets-outstanding balance check
    // below.
    auto const brokerSle = view.read(keylet::loanBroker(afterLoan_[0].loanBrokerID));
    if (!brokerSle)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: loan pay loan broker must exist";
        result = false;
    }
    else
    {
        auto const brokerPseudoDelta = deltaAssets(brokerSle->at(sfAccount));
        auto const brokerOwnerDelta = deltaAssets(brokerSle->at(sfOwner));

        std::vector<DeltaInfo> deltas{*maybeVaultDeltaAssets};
        if (brokerPseudoDelta)
            deltas.push_back(*brokerPseudoDelta);
        if (brokerOwnerDelta)
            deltas.push_back(*brokerOwnerDelta);
        auto const totalScale = std::max(minScale, computeCoarsestScale(deltas));

        Number totalReceivedRaw = maybeVaultDeltaAssets->delta;
        if (brokerPseudoDelta)
            totalReceivedRaw += brokerPseudoDelta->delta;
        if (brokerOwnerDelta)
            totalReceivedRaw += brokerOwnerDelta->delta;

        auto const totalReceived = roundToAsset(vaultAsset, totalReceivedRaw, totalScale);
        auto const totalAmount = roundToAsset(vaultAsset, tx[sfAmount], totalScale);
        if (totalReceived > totalAmount)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: loan pay vault and broker must not "
                "receive more than the amount paid";
            result = false;
        }
    }

    // The amount the loan owes to the vault is accounting-basis dependent, so
    // both checks below are evaluated with the value the vault actually
    // recognises. Under accrual it carries the interest, which assets
    // outstanding already booked at origination; under cash-basis it is
    // principal only and assets outstanding grow by the interest as it is
    // received.
    auto const version = afterVault.version;
    auto const owedDelta = roundToAsset(
        vaultAsset,
        afterLoan_[0].ownedToVault(version) - beforeLoan_[0].ownedToVault(version),
        minScale);

    // A payment services the loan, so the amount the loan owes to the vault
    // can only shrink. Penalties and fees charged on a late payment or an
    // overpayment are settled from the same payment rather than added to the
    // loan, so they cannot grow the amount owed either.
    if (owedDelta > kZero)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: loan pay must not increase the amount the loan "
            "owes to the vault";
        result = false;
    }

    // LoanPay::doApply calls LoanManage::unimpairLoan before applying the
    // payment, so a payment on a pre-impaired loan legitimately releases the
    // paper loss the impairment recorded - LossUnrealized falls by exactly
    // the pre-transaction amount the loan owed to the vault. A payment on a
    // non-impaired loan does not touch LossUnrealized. Mirrors item 12 in
    // finalizeLoanManage; the residual is rounded once for the same reason.
    {
        Number const expectedDelta =
            beforeLoan_[0].impaired ? -beforeLoan_[0].ownedToVault(version) : kZero;
        auto const residual = roundToAsset(
            vaultAsset,
            (afterVault.lossUnrealized - beforeVault.lossUnrealized) - expectedDelta,
            minScale);
        if (residual != kZero)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: loan pay must decrease loss unrealized by "
                "the pre-transaction amount the loan owed to the vault when "
                "impaired, or leave it unchanged otherwise";
            result = false;
        }
    }

    // The vault's total assets equal its available cash plus the amount its
    // outstanding loans owe to it (each loan's total value owed, less the
    // broker's management fee, which belongs to the broker). A payment only
    // moves value between those two pools, so the change in assets
    // outstanding must equal the cash received plus the change in the amount
    // the paid loan owes to the vault. This is an independent check that the
    // borrower's payment was split correctly between principal and interest.
    //
    // The residual is rounded once, rather than comparing three independently
    // rounded terms: each rounding can move a term by up to one unit in the
    // last place, so the rounded-terms form can differ by several ULP even when
    // the underlying identity holds exactly.
    auto const residual = roundToAsset(
        vaultAsset,
        (afterVault.assetsTotal - beforeVault.assetsTotal) -
            (afterVault.assetsAvailable - beforeVault.assetsAvailable) -
            (afterLoan_[0].ownedToVault(version) - beforeLoan_[0].ownedToVault(version)),
        minScale);
    if (residual != kZero)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: loan pay assets outstanding must "
            "match the cash received and the change in the amount "
            "the loan owes to the vault";
        result = false;
    }

    // Under featureLendingProtocolV1_1, tie the broker's aggregate amount owed
    // to the vault (DebtTotal) to the touched loan's `ownedToVault` delta:
    // since a LoanPay touches exactly one loan,
    // `Δ DebtTotal == Δ ownedToVault(loan)`. The universal
    // `DebtTotal == Σ ownedToVault` (item 22) reduces to this delta check
    // because loans are modified one at a time. Basis-aware via
    // ownedToVault(); the residual is rounded once for the same reason as
    // the identity above.
    if (beforeBroker_.size() != 1 || afterBroker_.size() != 1 ||
        afterBroker_[0].key != afterLoan_[0].loanBrokerID)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: loan pay must modify exactly the loan's broker";
        result = false;
    }
    else
    {
        auto const brokerResidual = roundToAsset(
            vaultAsset,
            (afterBroker_[0].debtTotal - beforeBroker_[0].debtTotal) -
                (afterLoan_[0].ownedToVault(version) - beforeLoan_[0].ownedToVault(version)),
            minScale);
        if (brokerResidual != kZero)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: loan pay broker debt total must "
                "track the change in the amount the loan owes to "
                "the vault";
            result = false;
        }
    }

    return result;
}

bool
ValidVault::finalize(
    STTx const& tx,
    TER const ret,
    XRPAmount const fee,
    ReadView const& view,
    beast::Journal const& j)
{
    bool const enforce = view.rules().enabled(featureSingleAssetVault);

    if (!isTesSuccess(ret))
        return true;  // Do not perform checks

    if (afterVault_.empty() && beforeVault_.empty())
    {
        if (hasPrivilege(tx, Privilege::MustModifyVault))
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: vault operation succeeded without modifying "
                "a vault";
            XRPL_ASSERT(enforce, "xrpl::ValidVault::finalize : vault noop invariant");
            return !enforce;
        }

        return true;  // Not a vault operation
    }
    if (!(hasPrivilege(tx, Privilege::MustModifyVault) ||
          hasPrivilege(tx, Privilege::MayModifyVault)))
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: vault updated by a wrong transaction type";
        XRPL_ASSERT(
            enforce,
            "xrpl::ValidVault::finalize : illegal vault transaction "
            "invariant");
        return !enforce;  // Also not a vault operation
    }

    if (beforeVault_.size() > 1 || afterVault_.size() > 1)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: vault operation updated more than single vault";
        XRPL_ASSERT(enforce, "xrpl::ValidVault::finalize : single vault invariant");
        return !enforce;  // That's all we can do here
    }

    auto const txnType = tx.getTxnType();

    // We do special handling for ttVAULT_DELETE first, because it's the only
    // vault-modifying transaction without an "after" state of the vault
    if (afterVault_.empty())
    {
        if (txnType != ttVAULT_DELETE)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: vault deleted by a wrong transaction type";
            XRPL_ASSERT(
                enforce,
                "xrpl::ValidVault::finalize : illegal vault deletion "
                "invariant");
            return !enforce;  // That's all we can do here
        }

        // Note, if afterVault_ is empty then we know that beforeVault_ is not
        // empty, as enforced at the top of this function
        auto const& beforeVault = beforeVault_[0];

        // At this moment we only know a vault is being deleted and there
        // might be some MPTokenIssuance objects which are deleted in the
        // same transaction. Find the one matching this vault.
        auto const deletedShares = [&]() -> std::optional<Shares> {
            for (auto const& e : beforeMPTs_)
            {
                if (e.share.getMptID() == beforeVault.shareMPTID)
                    return e;
            }
            return std::nullopt;
        }();

        if (!deletedShares)
        {
            JLOG(j.fatal()) << "Invariant failed: deleted vault must also "
                               "delete shares";
            XRPL_ASSERT(enforce, "xrpl::ValidVault::finalize : shares deletion invariant");
            return !enforce;  // That's all we can do here
        }

        bool result = true;
        if (deletedShares->sharesTotal != 0)
        {
            JLOG(j.fatal()) << "Invariant failed: deleted vault must have no "
                               "shares outstanding";
            result = false;
        }
        if (beforeVault.assetsTotal != kZero)
        {
            JLOG(j.fatal()) << "Invariant failed: deleted vault must have no "
                               "assets outstanding";
            result = false;
        }
        if (beforeVault.assetsAvailable != kZero)
        {
            JLOG(j.fatal()) << "Invariant failed: deleted vault must have no "
                               "assets available";
            result = false;
        }

        return result;
    }
    if (txnType == ttVAULT_DELETE)
    {
        JLOG(j.fatal()) << "Invariant failed: vault deletion succeeded without "
                           "deleting a vault";
        XRPL_ASSERT(enforce, "xrpl::ValidVault::finalize : vault deletion invariant");
        return !enforce;  // That's all we can do here
    }

    // Note, `afterVault_.empty()` is handled above
    auto const& afterVault = afterVault_[0];
    XRPL_ASSERT(
        beforeVault_.empty() || beforeVault_[0].key == afterVault.key,
        "xrpl::ValidVault::finalize : single vault operation");

    auto const updatedShares = [&]() -> std::optional<Shares> {
        // At this moment we only know that a vault is being updated and there
        // might be some MPTokenIssuance objects which are also updated in the
        // same transaction. Find the one matching the shares to this vault.
        // Note, we expect updatedMPTs collection to be extremely small. For
        // such collections linear search is faster than lookup.
        for (auto const& e : afterMPTs_)
        {
            if (e.share.getMptID() == afterVault.shareMPTID)
                return e;
        }

        auto const sleShares = view.read(keylet::mptokenIssuance(afterVault.shareMPTID));

        return sleShares ? std::optional<Shares>(Shares::make(*sleShares)) : std::nullopt;
    }();

    bool result = true;

    // Universal transaction checks
    // From LendingProtocolV1_1 onwards, vault immutability check is moved to InvariantCheck.cpp
    if (!beforeVault_.empty() && !view.rules().enabled(featureLendingProtocolV1_1))
    {
        auto const& beforeVault = beforeVault_[0];
        if (afterVault.asset != beforeVault.asset || afterVault.pseudoId != beforeVault.pseudoId ||
            afterVault.shareMPTID != beforeVault.shareMPTID)
        {
            JLOG(j.fatal()) << "Invariant failed: violation of vault immutable data";
            result = false;
        }
    }

    if (!updatedShares)
    {
        JLOG(j.fatal()) << "Invariant failed: updated vault must have shares";
        XRPL_ASSERT(enforce, "xrpl::ValidVault::finalize : vault has shares invariant");
        return !enforce;  // That's all we can do here
    }

    if (updatedShares->sharesTotal == 0)
    {
        if (afterVault.assetsTotal != kZero)
        {
            JLOG(j.fatal()) << "Invariant failed: updated zero sized "
                               "vault must have no assets outstanding";
            result = false;
        }
        if (afterVault.assetsAvailable != kZero)
        {
            JLOG(j.fatal()) << "Invariant failed: updated zero sized "
                               "vault must have no assets available";
            result = false;
        }
    }
    else if (updatedShares->sharesTotal > updatedShares->sharesMaximum)
    {
        JLOG(j.fatal())  //
            << "Invariant failed: updated shares must not exceed maximum "
            << updatedShares->sharesMaximum;
        result = false;
    }

    if (afterVault.assetsAvailable < kZero)
    {
        JLOG(j.fatal()) << "Invariant failed: assets available must not be negative";
        result = false;
    }

    if (afterVault.assetsAvailable > afterVault.assetsTotal)
    {
        JLOG(j.fatal()) << "Invariant failed: assets available must "
                           "not be greater than assets outstanding";
        result = false;
    }
    else if (afterVault.lossUnrealized > afterVault.assetsTotal - afterVault.assetsAvailable)
    {
        JLOG(j.fatal())  //
            << "Invariant failed: loss unrealized must not exceed "
               "the difference between assets outstanding and available";
        result = false;
    }

    if (view.rules().enabled(fixCleanup3_4_0) && afterVault.lossUnrealized < kZero)
    {
        JLOG(j.fatal()) << "Invariant failed: loss unrealized must not be negative";
        result = false;
    }

    if (afterVault.assetsTotal < kZero)
    {
        JLOG(j.fatal()) << "Invariant failed: assets outstanding must not be negative";
        result = false;
    }

    if (afterVault.assetsMaximum < kZero)
    {
        JLOG(j.fatal()) << "Invariant failed: assets maximum must not be negative";
        result = false;
    }

    // Thanks to this check we can simply do `assert(!beforeVault_.empty()` when
    // enforcing invariants on transaction types other than ttVAULT_CREATE
    if (beforeVault_.empty() && txnType != ttVAULT_CREATE)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: vault created by a wrong transaction type";
        XRPL_ASSERT(enforce, "xrpl::ValidVault::finalize : vault creation invariant");
        return !enforce;  // That's all we can do here
    }

    if (!beforeVault_.empty() && afterVault.lossUnrealized != beforeVault_[0].lossUnrealized &&
        txnType != ttLOAN_MANAGE && txnType != ttLOAN_PAY)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: vault transaction must not change loss "
            "unrealized";
        result = false;
    }

    // Immutability of VaultKind, SubscriptionDate and RedemptionDate is enforced by
    // NoModifiedUnmodifiableFields in InvariantCheck.cpp.

    auto const beforeShares = [&]() -> std::optional<Shares> {
        if (beforeVault_.empty())
            return std::nullopt;
        auto const& beforeVault = beforeVault_[0];

        for (auto const& e : beforeMPTs_)
        {
            if (e.share.getMptID() == beforeVault.shareMPTID)
                return e;
        }
        return std::nullopt;
    }();

    if (!beforeShares &&
        (tx.getTxnType() == ttVAULT_DEPOSIT ||   //
         tx.getTxnType() == ttVAULT_WITHDRAW ||  //
         tx.getTxnType() == ttVAULT_CLAWBACK))
    {
        JLOG(j.fatal()) << "Invariant failed: vault operation succeeded "
                           "without updating shares";
        XRPL_ASSERT(enforce, "xrpl::ValidVault::finalize : shares noop invariant");
        return !enforce;  // That's all we can do here
    }

    auto const& vaultAsset = afterVault.asset;

    if (view.rules().enabled(featureLendingProtocolV1_1))
    {
        // ttLOAN_* transactions only exist under featureLendingProtocol, and their
        // vault-side checks are otherwise gated by featureLendingProtocolV1_1;
        // keep the same gate here so pre-V1_1 behaviour is unchanged.
        bool const isLoanTxn = txnType == ttLOAN_SET ||  //
            txnType == ttLOAN_MANAGE ||                  //
            txnType == ttLOAN_PAY;
        bool const sharesCheckActive = !isLoanTxn;

        if (sharesCheckActive && beforeShares &&
            beforeShares->sharesTotal != updatedShares->sharesTotal && txnType != ttVAULT_DEPOSIT &&
            txnType != ttVAULT_WITHDRAW && txnType != ttVAULT_CLAWBACK)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: shares outstanding must only change by "
                "deposit, withdraw, or clawback";
            result = false;
        }

        // (XLS-65 §3.1.6.1.3, §3.6): shares of a non-transferable vault
        // (tfVaultShareNonTransferable → lsfMPTCanTransfer absent on the share
        // issuance) may only be issued or burned by the vault's own
        // deposit/withdraw/clawback flow. Any other transaction (notably Payment)
        // that touches share MPTokens of such an issuance violates the ban.
        if ((updatedShares->flags & lsfMPTCanTransfer) == 0 && txnType != ttVAULT_DEPOSIT &&
            txnType != ttVAULT_WITHDRAW && txnType != ttVAULT_CLAWBACK &&
            txnType != ttVAULT_CREATE && txnType != ttVAULT_DELETE)
        {
            if (touchedShareIssuances_.contains(afterVault.shareMPTID))
            {
                JLOG(j.fatal()) <<  //
                    "Invariant failed: non-transferable vault shares must not "
                    "move outside of deposit, withdraw, or clawback";
                result = false;
            }
        }

        // Assets available always tracks the real vault balance: any change
        // in one is matched by the other.  This applies to every vault
        // operation that may move funds into or out of the pseudo-account
        // (deposit, withdraw, clawback, and every loan-* sub-operation);
        // vault set is excluded because it may never change either, and
        // vault create has nothing to compare against.  The gate is shared
        // with the shares-outstanding check above so that pre-V1_1 loan
        // behaviour is unchanged.
        if (sharesCheckActive && txnType != ttVAULT_CREATE && txnType != ttVAULT_SET)
        {
            auto const& beforeVault = beforeVault_[0];
            auto const maybeVaultDeltaAssets = deltaAssets(afterVault.pseudoId);
            auto const vaultDelta = maybeVaultDeltaAssets.value_or(
                DeltaInfo{.delta = kZero, .scale = scale(afterVault.assetsTotal, vaultAsset)});
            auto const minScale = computeVaultMinScale(vaultDelta, view.rules());
            auto const vaultDeltaAssets = roundToAsset(vaultAsset, vaultDelta.delta, minScale);
            auto const assetAvailableDelta = roundToAsset(
                vaultAsset, afterVault.assetsAvailable - beforeVault.assetsAvailable, minScale);
            if (assetAvailableDelta != vaultDeltaAssets)
            {
                JLOG(j.fatal()) <<  //
                    "Invariant failed: vault balance and assets available must add up";
                result = false;
            }

            // For deposit / withdraw / clawback the vault's assets outstanding
            // must also track the vault balance in lock-step: no loan-side
            // activity is present to legitimately move `assetsTotal`
            // independently (interest booking, default write-off, or change
            // in what a loan owes to the vault on repayment all belong to
            // loan-* transactions).
            if (txnType == ttVAULT_DEPOSIT || txnType == ttVAULT_WITHDRAW ||
                txnType == ttVAULT_CLAWBACK)
            {
                auto const assetsTotalDelta = roundToAsset(
                    vaultAsset, afterVault.assetsTotal - beforeVault.assetsTotal, minScale);
                if (assetsTotalDelta != vaultDeltaAssets)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: vault balance and assets outstanding must add up";
                    result = false;
                }
            }
        }
    }

    // Technically this does not need to be a lambda, but it's more
    // convenient thanks to early "return false"; the not-so-nice
    // alternatives are several layers of nested if/else or more complex
    // (i.e. brittle) if statements.
    result &= [&]() {
        switch (txnType)
        {
            case ttVAULT_CREATE: {
                bool result = true;

                if (!beforeVault_.empty())
                {
                    JLOG(j.fatal())  //
                        << "Invariant failed: create operation must not have "
                           "updated a vault";
                    result = false;
                }

                if (afterVault.assetsAvailable != kZero || afterVault.assetsTotal != kZero ||
                    afterVault.lossUnrealized != kZero || updatedShares->sharesTotal != 0)
                {
                    JLOG(j.fatal())  //
                        << "Invariant failed: created vault must be empty";
                    result = false;
                }

                if (afterVault.pseudoId != updatedShares->share.getIssuer())
                {
                    JLOG(j.fatal())  //
                        << "Invariant failed: shares issuer and vault "
                           "pseudo-account must be the same";
                    result = false;
                }

                auto const sleSharesIssuer =
                    view.read(keylet::account(updatedShares->share.getIssuer()));
                if (!sleSharesIssuer)
                {
                    JLOG(j.fatal())  //
                        << "Invariant failed: shares issuer must exist";
                    return false;
                }

                if (!isPseudoAccount(sleSharesIssuer))
                {
                    JLOG(j.fatal())  //
                        << "Invariant failed: shares issuer must be a "
                           "pseudo-account";
                    result = false;
                }

                if (auto const vaultId = (*sleSharesIssuer)[~sfVaultID];
                    !vaultId || *vaultId != afterVault.key)
                {
                    JLOG(j.fatal())  //
                        << "Invariant failed: shares issuer pseudo-account "
                           "must point back to the vault";
                    result = false;
                }

                if (isClosedEnded(afterVault.vaultKind))
                {
                    if (!afterVault.subscriptionDate || !afterVault.redemptionDate)
                    {
                        JLOG(j.fatal())  //
                            << "Invariant failed: closed-ended vault must have SubscriptionDate "
                               "and RedemptionDate";
                        result = false;
                    }
                    else if (!isValidClosedEndedGap(
                                 *afterVault.subscriptionDate, *afterVault.redemptionDate))
                    {
                        JLOG(j.fatal())  //
                            << "Invariant failed: closed-ended vault RedemptionDate - "
                               "SubscriptionDate must be within [MIN_INVESTMENT_PERIOD, "
                               "MAX_INVESTMENT_PERIOD)";
                        result = false;
                    }
                }

                return result;
            }
            case ttVAULT_SET: {
                bool result = true;

                XRPL_ASSERT(
                    !beforeVault_.empty(), "xrpl::ValidVault::finalize : set updated a vault");
                auto const& beforeVault = beforeVault_[0];

                auto const vaultDeltaAssets = deltaAssets(afterVault.pseudoId);
                if (vaultDeltaAssets)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: set must not change vault balance";
                    result = false;
                }

                if (beforeVault.assetsTotal != afterVault.assetsTotal)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: set must not change assets "
                        "outstanding";
                    result = false;
                }

                if (afterVault.assetsMaximum > kZero &&
                    afterVault.assetsTotal > afterVault.assetsMaximum)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: set assets outstanding must not "
                        "exceed assets maximum";
                    result = false;
                }

                if (beforeVault.assetsAvailable != afterVault.assetsAvailable)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: set must not change assets "
                        "available";
                    result = false;
                }

                if (beforeShares && updatedShares &&
                    beforeShares->sharesTotal != updatedShares->sharesTotal)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: set must not change shares "
                        "outstanding";
                    result = false;
                }

                return result;
            }
            case ttVAULT_DEPOSIT: {
                bool result = true;

                XRPL_ASSERT(
                    !beforeVault_.empty(), "xrpl::ValidVault::finalize : deposit updated a vault");
                auto const& beforeVault = beforeVault_[0];

                // Deposit is only allowed while the vault is in NoPhase or
                // Subscription.
                auto const depositPhase = getVaultPhase(
                    view,
                    afterVault.vaultKind,
                    afterVault.subscriptionDate,
                    afterVault.redemptionDate);
                if (depositPhase != VaultPhase::NoPhase && depositPhase != VaultPhase::Subscription)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit only allowed in "
                        "Subscription or NoPhase";
                    result = false;
                }

                auto const maybeVaultDeltaAssets = deltaAssets(afterVault.pseudoId);
                if (!maybeVaultDeltaAssets)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must change vault balance";
                    return false;  // That's all we can do
                }

                // Get the posterior scale to round calculations to
                auto const minScale = computeVaultMinScale(*maybeVaultDeltaAssets, view.rules());

                auto const vaultDeltaAssets =
                    roundToAsset(vaultAsset, maybeVaultDeltaAssets->delta, minScale);
                auto const txAmount = roundToAsset(vaultAsset, tx[sfAmount], minScale);

                if (vaultDeltaAssets > txAmount)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must not change vault "
                        "balance by more than deposited amount";
                    result = false;
                }

                if (vaultDeltaAssets <= kZero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must increase vault balance";
                    result = false;
                }

                // Any payments (including deposits) made by the issuer
                // do not change their balance, but create funds instead.
                bool const issuerDeposit = [&]() -> bool {
                    if (vaultAsset.native())
                        return false;
                    return tx[sfAccount] == vaultAsset.getIssuer();
                }();

                if (!issuerDeposit)
                {
                    auto const maybeAccDeltaAssets = deltaAssetsTxAccount(tx, fee);
                    if (!maybeAccDeltaAssets)
                    {
                        JLOG(j.fatal())
                            << "Invariant failed: deposit must change depositor balance";
                        return false;
                    }
                    auto const localMinScale =
                        std::max(minScale, computeCoarsestScale({*maybeAccDeltaAssets}));

                    auto const accountDeltaAssets =
                        roundToAsset(vaultAsset, maybeAccDeltaAssets->delta, localMinScale);
                    auto const localVaultDeltaAssets =
                        roundToAsset(vaultAsset, vaultDeltaAssets, localMinScale);

                    // For IOUs, if the deposit amount is not-representable at depositor trustline
                    // scale deposit amount could round to zero, giving depositor shares for no
                    // assets. Unlike withdrawal, we do not allow that.
                    if (accountDeltaAssets >= kZero)
                    {
                        JLOG(j.fatal())
                            << "Invariant failed: deposit must decrease depositor balance";
                        result = false;
                    }

                    if (localVaultDeltaAssets * -1 != accountDeltaAssets)
                    {
                        JLOG(j.fatal()) << "Invariant failed: " <<  //
                            "deposit must change vault and depositor balance by equal amount";
                        result = false;
                    }
                }

                if (afterVault.assetsMaximum > kZero &&
                    afterVault.assetsTotal > afterVault.assetsMaximum)
                {
                    JLOG(j.fatal()) << "Invariant failed: " <<  //
                        "deposit assets outstanding must not exceed assets maximum";
                    result = false;
                }

                auto const maybeAccDeltaShares = deltaShares(tx[sfAccount]);
                if (!maybeAccDeltaShares)
                {
                    JLOG(j.fatal()) << "Invariant failed: deposit must change depositor shares";
                    return false;  // That's all we can do
                }
                // We don't round shares, they are integral MPT
                auto const& accountDeltaShares = *maybeAccDeltaShares;
                if (accountDeltaShares.delta <= kZero)
                {
                    JLOG(j.fatal()) << "Invariant failed: deposit must increase depositor shares";
                    result = false;
                }

                auto const maybeVaultDeltaShares = deltaShares(afterVault.pseudoId);
                if (!maybeVaultDeltaShares || maybeVaultDeltaShares->delta == kZero)
                {
                    JLOG(j.fatal()) << "Invariant failed: deposit must change vault shares";
                    return false;  // That's all we can do
                }

                // We don't round shares, they are integral MPT
                auto const& vaultDeltaShares = *maybeVaultDeltaShares;
                if (vaultDeltaShares.delta * -1 != accountDeltaShares.delta)
                {
                    JLOG(j.fatal()) << "Invariant failed: " <<  //
                        "deposit must change depositor and vault shares by equal amount";
                    result = false;
                }

                auto const assetTotalDelta = roundToAsset(
                    vaultAsset, afterVault.assetsTotal - beforeVault.assetsTotal, minScale);
                if (assetTotalDelta != vaultDeltaAssets)
                {
                    JLOG(j.fatal())
                        << "Invariant failed: deposit and assets outstanding must add up";
                    result = false;
                }

                auto const assetAvailableDelta = roundToAsset(
                    vaultAsset, afterVault.assetsAvailable - beforeVault.assetsAvailable, minScale);
                if (assetAvailableDelta != vaultDeltaAssets)
                {
                    JLOG(j.fatal()) << "Invariant failed: deposit and assets available must add up";
                    result = false;
                }

                return result;
            }
            case ttVAULT_WITHDRAW: {
                bool result = true;

                XRPL_ASSERT(
                    !beforeVault_.empty(),
                    "xrpl::ValidVault::finalize : withdrawal updated a vault");
                auto const& beforeVault = beforeVault_[0];

                // Withdrawal from a closed-ended vault is not allowed during the Investment phase
                // (strictly past SubscriptionDate, before RedemptionDate).
                if (getVaultPhase(
                        view,
                        afterVault.vaultKind,
                        afterVault.subscriptionDate,
                        afterVault.redemptionDate) == VaultPhase::Investment)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: withdrawal not allowed during "
                        "Investment phase";
                    result = false;
                }

                auto const maybeVaultDeltaAssets = deltaAssets(afterVault.pseudoId);

                // Post-featureLendingProtocolV1_1: a withdrawal that redeems shares from a
                // pool with no effective value left to back them (e.g. fully
                // impaired/insolvent) legitimately moves zero assets on both
                // sides — VaultWithdraw::doApply does not touch either
                // balance-holding entry for a zero-value transfer, so no delta
                // is recorded. VaultWithdraw::doApply separately rejects
                // (tecPRECISION_LOSS) the case where a *positive* per-share
                // value merely rounds down to zero, so a missing delta while
                // the pool still held positive effective value indicates a
                // real accounting bug, not this exception.
                bool const zeroDeltaIsLegitimate = view.rules().enabled(fixCleanup3_4_0) &&
                    !maybeVaultDeltaAssets && beforeVault.assetsTotal == beforeVault.lossUnrealized;

                if (!maybeVaultDeltaAssets && !zeroDeltaIsLegitimate)
                {
                    JLOG(j.fatal()) << "Invariant failed: withdrawal must change vault balance";
                    return false;  // That's all we can do
                }

                DeltaInfo const vaultDeltaAssets = maybeVaultDeltaAssets.value_or(
                    DeltaInfo{.delta = kNumZero, .scale = std::nullopt});

                // Get the posterior scale to round calculations to
                auto const minScale = computeVaultMinScale(vaultDeltaAssets, view.rules());

                auto const vaultPseudoDeltaAssets =
                    roundToAsset(vaultAsset, vaultDeltaAssets.delta, minScale);

                if (!zeroDeltaIsLegitimate && vaultPseudoDeltaAssets >= kZero)
                {
                    JLOG(j.fatal()) << "Invariant failed: withdrawal must decrease vault balance";
                    result = false;
                }

                // Any payments (including withdrawal) going to the issuer
                // do not change their balance, but destroy funds instead.
                bool const issuerWithdrawal = [&]() -> bool {
                    if (vaultAsset.native())
                        return false;
                    auto const destination = tx[~sfDestination].value_or(tx[sfAccount]);
                    return destination == vaultAsset.getIssuer();
                }();

                if (!issuerWithdrawal)
                {
                    auto const maybeAccDelta = deltaAssetsTxAccount(tx, fee);
                    auto const maybeOtherAccDelta = [&]() -> std::optional<DeltaInfo> {
                        if (auto const destination = tx[~sfDestination];
                            destination && *destination != tx[sfAccount])
                            return deltaAssets(*destination);
                        return std::nullopt;
                    }();

                    if (maybeAccDelta.has_value() == maybeOtherAccDelta.has_value())
                    {
                        // Both changed is always a bug. Neither changed is
                        // consistent only with a legitimate zero-value
                        // withdrawal, which moves nothing on either side —
                        // there is nothing left to cross-check.
                        if (!zeroDeltaIsLegitimate || maybeAccDelta.has_value())
                        {
                            JLOG(j.fatal()) <<  //
                                "Invariant failed: withdrawal must change one destination balance";
                            return false;
                        }
                    }
                    else
                    {
                        // A one-sided change is cross-checked even for a
                        // legitimate zero vault delta: the destination must
                        // then have moved by (rounded) zero as well.
                        auto const destinationDelta =
                            *maybeAccDelta.or_else([&] { return maybeOtherAccDelta; });

                        // the scale of destinationDelta can be coarser than
                        // minScale, so we take that into account when rounding
                        auto const destinationScale = computeCoarsestScale({destinationDelta});
                        auto const localMinScale = std::max(minScale, destinationScale);

                        auto const roundedDestinationDelta =
                            roundToAsset(vaultAsset, destinationDelta.delta, localMinScale);

                        // Post-fixCleanup3_2_0: Tolerate zero-rounded destination deltas for IOUs
                        // only. If the receiver's trust line sits at a coarser scale, the inflow
                        // may safely round down to zero.
                        //
                        // XRP and MPT remain strict. Because they are integer-exact, a zero
                        // destination delta indicates a true accounting bug, not a rounding
                        // artifact.
                        bool const tolerateZeroDelta =
                            view.rules().enabled(fixCleanup3_2_0) && !vaultAsset.integral();
                        auto const invalidBalanceChange = tolerateZeroDelta
                            ? roundedDestinationDelta < kZero
                            : roundedDestinationDelta <= kZero;
                        if (invalidBalanceChange)
                        {
                            JLOG(j.fatal()) <<  //
                                "Invariant failed: withdrawal must increase destination balance";
                            result = false;
                        }

                        auto const localPseudoDeltaAssets =
                            roundToAsset(vaultAsset, vaultPseudoDeltaAssets, localMinScale);
                        // For IOU assets near a precision boundary the destination's STAmount
                        // exponent can shift, making part of the sent value unrepresentable at
                        // the receiver's new scale — that portion is irreversibly absorbed by the
                        // IOU rail.  Tolerate the mismatch only when the destroyed amount (vault
                        // outflow minus destination inflow, in Number space) is itself sub-ULP at
                        // the destination's scale.  Floor rounding is used so that values exactly
                        // at the step boundary are not mistakenly dismissed.  Any representable
                        // discrepancy indicates a real accounting bug and must be caught.
                        auto const destroyedIsSubUlp = tolerateZeroDelta &&
                            roundToAsset(
                                vaultAsset,
                                vaultDeltaAssets.delta * -1 - destinationDelta.delta,
                                destinationScale,
                                Number::RoundingMode::Downward) == kZero;
                        if (!destroyedIsSubUlp &&
                            localPseudoDeltaAssets * -1 != roundedDestinationDelta)
                        {
                            JLOG(j.fatal()) << "Invariant failed: " <<  //
                                "withdrawal must change vault and destination balance by equal "
                                "amount";
                            result = false;
                        }
                    }
                }

                // We don't round shares, they are integral MPT
                auto const accountDeltaShares = deltaShares(tx[sfAccount]);
                if (!accountDeltaShares)
                {
                    JLOG(j.fatal()) << "Invariant failed: withdrawal must change depositor shares";
                    return false;
                }

                if (accountDeltaShares->delta >= kZero)
                {
                    JLOG(j.fatal())
                        << "Invariant failed: withdrawal must decrease depositor shares";
                    result = false;
                }

                // We don't round shares, they are integral MPT
                auto const vaultDeltaShares = deltaShares(afterVault.pseudoId);
                if (!vaultDeltaShares || vaultDeltaShares->delta == kZero)
                {
                    JLOG(j.fatal()) << "Invariant failed: withdrawal must change vault shares";
                    return false;  // That's all we can do
                }

                if (vaultDeltaShares->delta * -1 != accountDeltaShares->delta)
                {
                    JLOG(j.fatal()) << "Invariant failed: " <<  //
                        "withdrawal must change depositor and vault shares by equal amount";
                    result = false;
                }

                auto const assetTotalDelta = roundToAsset(
                    vaultAsset, afterVault.assetsTotal - beforeVault.assetsTotal, minScale);
                // Note, vaultBalance is negative (see check above)
                if (assetTotalDelta != vaultPseudoDeltaAssets)
                {
                    JLOG(j.fatal())
                        << "Invariant failed: withdrawal and assets outstanding must add up";
                    result = false;
                }

                auto const assetAvailableDelta = roundToAsset(
                    vaultAsset, afterVault.assetsAvailable - beforeVault.assetsAvailable, minScale);

                if (assetAvailableDelta != vaultPseudoDeltaAssets)
                {
                    JLOG(j.fatal())
                        << "Invariant failed: withdrawal and assets available must add up";
                    result = false;
                }

                return result;
            }
            case ttVAULT_CLAWBACK: {
                bool result = true;

                XRPL_ASSERT(
                    !beforeVault_.empty(), "xrpl::ValidVault::finalize : clawback updated a vault");
                auto const& beforeVault = beforeVault_[0];

                if (vaultAsset.native() || vaultAsset.getIssuer() != tx[sfAccount])
                {
                    // The owner can use clawback to force-burn shares when the
                    // vault is empty but there are outstanding shares
                    if (!(beforeShares && beforeShares->sharesTotal > 0 &&
                          isVaultEmpty(beforeVault) && beforeVault.owner == tx[sfAccount]))
                    {
                        JLOG(j.fatal()) << "Invariant failed: " <<  //
                            "clawback may only be performed by the asset issuer, or by the vault "
                            "owner of an empty vault";
                        return false;  // That's all we can do
                    }
                }

                auto const maybeVaultDeltaAssets = deltaAssets(afterVault.pseudoId);
                if (maybeVaultDeltaAssets)
                {
                    auto const minScale =
                        computeVaultMinScale(*maybeVaultDeltaAssets, view.rules());
                    auto const vaultDeltaAssets =
                        roundToAsset(vaultAsset, maybeVaultDeltaAssets->delta, minScale);
                    if (vaultDeltaAssets >= kZero)
                    {
                        JLOG(j.fatal()) << "Invariant failed: clawback must decrease vault balance";
                        result = false;
                    }

                    auto const assetsTotalDelta = roundToAsset(
                        vaultAsset, afterVault.assetsTotal - beforeVault.assetsTotal, minScale);
                    if (assetsTotalDelta != vaultDeltaAssets)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: clawback and assets outstanding must add up";
                        result = false;
                    }

                    auto const assetAvailableDelta = roundToAsset(
                        vaultAsset,
                        afterVault.assetsAvailable - beforeVault.assetsAvailable,
                        minScale);
                    if (assetAvailableDelta != vaultDeltaAssets)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: clawback and assets available must add up";
                        result = false;
                    }
                }
                else if (!isVaultEmpty(beforeVault))
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must change vault balance";
                    return false;  // That's all we can do
                }

                // We don't need to round shares, they are integral MPT
                auto const maybeAccountDeltaShares = deltaShares(tx[sfHolder]);
                if (!maybeAccountDeltaShares)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must change holder shares";
                    return false;  // That's all we can do
                }
                if (maybeAccountDeltaShares->delta >= kZero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must decrease holder shares";
                    result = false;
                }

                // We don't need to round shares, they are integral MPT
                auto const vaultDeltaShares = deltaShares(afterVault.pseudoId);
                if (!vaultDeltaShares || vaultDeltaShares->delta == kZero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must change vault shares";
                    return false;  // That's all we can do
                }

                if (vaultDeltaShares->delta * -1 != maybeAccountDeltaShares->delta)
                {
                    JLOG(j.fatal()) << "Invariant failed: " <<  //
                        "clawback must change holder and vault shares by equal amount";
                    result = false;
                }

                return result;
            }

            case ttLOAN_SET:
                return finalizeLoanSet(tx, fee, view, j);

            case ttLOAN_MANAGE:
                return finalizeLoanManage(tx, view, j);

            case ttLOAN_PAY:
                return finalizeLoanPay(tx, view, j);

            default:
                // LCOV_EXCL_START
                UNREACHABLE("xrpl::ValidVault::finalize : unknown transaction type");
                return false;
                // LCOV_EXCL_STOP
        }
    }();

    if (!result)
    {
        // The comment at the top of this file starting with "assert(enforce)"
        // explains this assert.
        XRPL_ASSERT(enforce, "xrpl::ValidVault::finalize : vault invariants");
        return !enforce;
    }

    return true;
}

[[nodiscard]] ValidVault::DeltaInfo
ValidVault::DeltaInfo::makeDelta(Number const& before, Number const& after, Asset const& asset)
{
    return {
        .delta = after - before,
        .scale = std::max(xrpl::scale(after, asset), xrpl::scale(before, asset))};
}

[[nodiscard]] std::int32_t
ValidVault::computeCoarsestScale(std::vector<DeltaInfo> const& numbers)
{
    if (numbers.empty())
        return 0;

    auto const max = std::ranges::max_element(
        numbers, [](auto const& a, auto const& b) -> bool { return a.scale < b.scale; });
    XRPL_ASSERT_PARTS(
        max->scale, "xrpl::ValidVault::computeCoarsestScale", "scale set for destinationDelta");
    return max->scale.value_or(STAmount::kMaxOffset);
}

}  // namespace xrpl
