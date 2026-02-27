#include <xrpl/tx/invariants/VaultInvariant.h>
//
#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/invariants/InvariantCheckPrivilege.h>

namespace xrpl {

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
    self.sharesMaximum = from[~sfMaximumAmount].value_or(maxMPTokenAmount);
    return self;
}

void
ValidVault::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    // If `before` is empty, this means an object is being created, in which
    // case `isDelete` must be false. Otherwise `before` and `after` are set and
    // `isDelete` indicates whether an object is being deleted or modified.
    XRPL_ASSERT(
        after != nullptr && (before != nullptr || !isDelete),
        "xrpl::ValidVault::visitEntry : some object is available");

    // Number balanceDelta will capture the difference (delta) between "before"
    // state (zero if created) and "after" state (zero if destroyed), so the
    // invariants can validate that the change in account balances matches the
    // change in vault balances, stored to deltas_ at the end of this function.
    Number balanceDelta{};

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
                balanceDelta = static_cast<std::int64_t>(before->getFieldU64(sfOutstandingAmount));
                sign = 1;
                break;
            case ltMPTOKEN:
                balanceDelta = static_cast<std::int64_t>(before->getFieldU64(sfMPTAmount));
                sign = -1;
                break;
            case ltACCOUNT_ROOT:
            case ltRIPPLE_STATE:
                balanceDelta = before->getFieldAmount(sfBalance);
                sign = -1;
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
                balanceDelta -=
                    Number(static_cast<std::int64_t>(after->getFieldU64(sfOutstandingAmount)));
                sign = 1;
                break;
            case ltMPTOKEN:
                balanceDelta -= Number(static_cast<std::int64_t>(after->getFieldU64(sfMPTAmount)));
                sign = -1;
                break;
            case ltACCOUNT_ROOT:
            case ltRIPPLE_STATE:
                balanceDelta -= Number(after->getFieldAmount(sfBalance));
                sign = -1;
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
        deltas_[key] = balanceDelta * sign;
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
        if (hasPrivilege(tx, mustModifyVault))
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: vault operation succeeded without modifying "
                "a vault";
            XRPL_ASSERT(enforce, "xrpl::ValidVault::finalize : vault noop invariant");
            return !enforce;
        }

        return true;  // Not a vault operation
    }
    else if (!(hasPrivilege(tx, mustModifyVault) || hasPrivilege(tx, mayModifyVault)))
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
                    return std::move(e);
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
        if (beforeVault.assetsTotal != zero)
        {
            JLOG(j.fatal()) << "Invariant failed: deleted vault must have no "
                               "assets outstanding";
            result = false;
        }
        if (beforeVault.assetsAvailable != zero)
        {
            JLOG(j.fatal()) << "Invariant failed: deleted vault must have no "
                               "assets available";
            result = false;
        }

        return result;
    }
    else if (txnType == ttVAULT_DELETE)
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

        auto const sleShares = view.read(keylet::mptIssuance(afterVault.shareMPTID));

        return sleShares ? std::optional<Shares>(Shares::make(*sleShares)) : std::nullopt;
    }();

    bool result = true;

    // Universal transaction checks
    if (!beforeVault_.empty())
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
        if (afterVault.assetsTotal != zero)
        {
            JLOG(j.fatal()) << "Invariant failed: updated zero sized "
                               "vault must have no assets outstanding";
            result = false;
        }
        if (afterVault.assetsAvailable != zero)
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

    if (afterVault.assetsAvailable < zero)
    {
        JLOG(j.fatal()) << "Invariant failed: assets available must be positive";
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

    if (afterVault.assetsTotal < zero)
    {
        JLOG(j.fatal()) << "Invariant failed: assets outstanding must be positive";
        result = false;
    }

    if (afterVault.assetsMaximum < zero)
    {
        JLOG(j.fatal()) << "Invariant failed: assets maximum must be positive";
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

    auto const beforeShares = [&]() -> std::optional<Shares> {
        if (beforeVault_.empty())
            return std::nullopt;
        auto const& beforeVault = beforeVault_[0];

        for (auto const& e : beforeMPTs_)
        {
            if (e.share.getMptID() == beforeVault.shareMPTID)
                return std::move(e);
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
    auto const deltaAssets = [&](AccountID const& id) -> std::optional<Number> {
        auto const get =  //
            [&](auto const& it, std::int8_t sign = 1) -> std::optional<Number> {
            if (it == deltas_.end())
                return std::nullopt;

            return it->second * sign;
        };

        return std::visit(
            [&]<typename TIss>(TIss const& issue) {
                if constexpr (std::is_same_v<TIss, Issue>)
                {
                    if (isXRP(issue))
                        return get(deltas_.find(keylet::account(id).key));
                    return get(
                        deltas_.find(keylet::line(id, issue).key), id > issue.getIssuer() ? -1 : 1);
                }
                else if constexpr (std::is_same_v<TIss, MPTIssue>)
                {
                    return get(deltas_.find(keylet::mptoken(issue.getMptID(), id).key));
                }
            },
            vaultAsset.value());
    };
    auto const deltaAssetsTxAccount = [&]() -> std::optional<Number> {
        auto ret = deltaAssets(tx[sfAccount]);
        // Nothing returned or not XRP transaction
        if (!ret.has_value() || !vaultAsset.native())
            return ret;

        // Delegated transaction; no need to compensate for fees
        if (auto const delegate = tx[~sfDelegate];
            delegate.has_value() && *delegate != tx[sfAccount])
            return ret;

        *ret += fee.drops();
        if (*ret == zero)
            return std::nullopt;

        return ret;
    };
    auto const deltaShares = [&](AccountID const& id) -> std::optional<Number> {
        auto const it = [&]() {
            if (id == afterVault.pseudoId)
                return deltas_.find(keylet::mptIssuance(afterVault.shareMPTID).key);
            return deltas_.find(keylet::mptoken(afterVault.shareMPTID, id).key);
        }();

        return it != deltas_.end() ? std::optional<Number>(it->second) : std::nullopt;
    };

    auto const vaultHoldsNoAssets = [&](Vault const& vault) {
        return vault.assetsAvailable == 0 && vault.assetsTotal == 0;
    };

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

                if (afterVault.assetsAvailable != zero || afterVault.assetsTotal != zero ||
                    afterVault.lossUnrealized != zero || updatedShares->sharesTotal != 0)
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

                if (afterVault.assetsMaximum > zero &&
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

                auto const vaultDeltaAssets = deltaAssets(afterVault.pseudoId);

                if (!vaultDeltaAssets)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must change vault balance";
                    return false;  // That's all we can do
                }

                if (*vaultDeltaAssets > tx[sfAmount])
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must not change vault "
                        "balance by more than deposited amount";
                    result = false;
                }

                if (*vaultDeltaAssets <= zero)
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
                    auto const accountDeltaAssets = deltaAssetsTxAccount();
                    if (!accountDeltaAssets)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: deposit must change depositor "
                            "balance";
                        return false;
                    }

                    if (*accountDeltaAssets >= zero)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: deposit must decrease depositor "
                            "balance";
                        result = false;
                    }

                    if (*accountDeltaAssets * -1 != *vaultDeltaAssets)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: deposit must change vault and "
                            "depositor balance by equal amount";
                        result = false;
                    }
                }

                if (afterVault.assetsMaximum > zero &&
                    afterVault.assetsTotal > afterVault.assetsMaximum)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit assets outstanding must not "
                        "exceed assets maximum";
                    result = false;
                }

                auto const accountDeltaShares = deltaShares(tx[sfAccount]);
                if (!accountDeltaShares)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must change depositor "
                        "shares";
                    return false;  // That's all we can do
                }

                if (*accountDeltaShares <= zero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must increase depositor "
                        "shares";
                    result = false;
                }

                auto const vaultDeltaShares = deltaShares(afterVault.pseudoId);
                if (!vaultDeltaShares || *vaultDeltaShares == zero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must change vault shares";
                    return false;  // That's all we can do
                }

                if (*vaultDeltaShares * -1 != *accountDeltaShares)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must change depositor and "
                        "vault shares by equal amount";
                    result = false;
                }

                if (beforeVault.assetsTotal + *vaultDeltaAssets != afterVault.assetsTotal)
                {
                    JLOG(j.fatal()) << "Invariant failed: deposit and assets "
                                       "outstanding must add up";
                    result = false;
                }
                if (beforeVault.assetsAvailable + *vaultDeltaAssets != afterVault.assetsAvailable)
                {
                    JLOG(j.fatal()) << "Invariant failed: deposit and assets "
                                       "available must add up";
                    result = false;
                }

                return result;
            }
            case ttVAULT_WITHDRAW: {
                bool result = true;

                XRPL_ASSERT(
                    !beforeVault_.empty(),
                    "xrpl::ValidVault::finalize : withdrawal updated a "
                    "vault");
                auto const& beforeVault = beforeVault_[0];

                auto const vaultDeltaAssets = deltaAssets(afterVault.pseudoId);

                if (!vaultDeltaAssets)
                {
                    JLOG(j.fatal()) << "Invariant failed: withdrawal must "
                                       "change vault balance";
                    return false;  // That's all we can do
                }

                if (*vaultDeltaAssets >= zero)
                {
                    JLOG(j.fatal()) << "Invariant failed: withdrawal must "
                                       "decrease vault balance";
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
                    auto const accountDeltaAssets = deltaAssetsTxAccount();
                    auto const otherAccountDelta = [&]() -> std::optional<Number> {
                        if (auto const destination = tx[~sfDestination];
                            destination && *destination != tx[sfAccount])
                            return deltaAssets(*destination);
                        return std::nullopt;
                    }();

                    if (accountDeltaAssets.has_value() == otherAccountDelta.has_value())
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: withdrawal must change one "
                            "destination balance";
                        return false;
                    }

                    auto const destinationDelta =  //
                        accountDeltaAssets ? *accountDeltaAssets : *otherAccountDelta;

                    if (destinationDelta <= zero)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: withdrawal must increase "
                            "destination balance";
                        result = false;
                    }

                    if (*vaultDeltaAssets * -1 != destinationDelta)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: withdrawal must change vault "
                            "and destination balance by equal amount";
                        result = false;
                    }
                }

                auto const accountDeltaShares = deltaShares(tx[sfAccount]);
                if (!accountDeltaShares)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: withdrawal must change depositor "
                        "shares";
                    return false;
                }

                if (*accountDeltaShares >= zero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: withdrawal must decrease depositor "
                        "shares";
                    result = false;
                }

                auto const vaultDeltaShares = deltaShares(afterVault.pseudoId);
                if (!vaultDeltaShares || *vaultDeltaShares == zero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: withdrawal must change vault shares";
                    return false;  // That's all we can do
                }

                if (*vaultDeltaShares * -1 != *accountDeltaShares)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: withdrawal must change depositor "
                        "and vault shares by equal amount";
                    result = false;
                }

                // Note, vaultBalance is negative (see check above)
                if (beforeVault.assetsTotal + *vaultDeltaAssets != afterVault.assetsTotal)
                {
                    JLOG(j.fatal()) << "Invariant failed: withdrawal and "
                                       "assets outstanding must add up";
                    result = false;
                }

                if (beforeVault.assetsAvailable + *vaultDeltaAssets != afterVault.assetsAvailable)
                {
                    JLOG(j.fatal()) << "Invariant failed: withdrawal and "
                                       "assets available must add up";
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
                          vaultHoldsNoAssets(beforeVault) && beforeVault.owner == tx[sfAccount]))
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: clawback may only be performed "
                            "by the asset issuer, or by the vault owner of an "
                            "empty vault";
                        return false;  // That's all we can do
                    }
                }

                auto const vaultDeltaAssets = deltaAssets(afterVault.pseudoId);
                if (vaultDeltaAssets)
                {
                    if (*vaultDeltaAssets >= zero)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: clawback must decrease vault "
                            "balance";
                        result = false;
                    }

                    if (beforeVault.assetsTotal + *vaultDeltaAssets != afterVault.assetsTotal)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: clawback and assets outstanding "
                            "must add up";
                        result = false;
                    }

                    if (beforeVault.assetsAvailable + *vaultDeltaAssets !=
                        afterVault.assetsAvailable)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: clawback and assets available "
                            "must add up";
                        result = false;
                    }
                }
                else if (!vaultHoldsNoAssets(beforeVault))
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must change vault balance";
                    return false;  // That's all we can do
                }

                auto const accountDeltaShares = deltaShares(tx[sfHolder]);
                if (!accountDeltaShares)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must change holder shares";
                    return false;  // That's all we can do
                }

                if (*accountDeltaShares >= zero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must decrease holder "
                        "shares";
                    result = false;
                }

                auto const vaultDeltaShares = deltaShares(afterVault.pseudoId);
                if (!vaultDeltaShares || *vaultDeltaShares == zero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must change vault shares";
                    return false;  // That's all we can do
                }

                if (*vaultDeltaShares * -1 != *accountDeltaShares)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must change holder and "
                        "vault shares by equal amount";
                    result = false;
                }

                return result;
            }

            case ttLOAN_SET:
            case ttLOAN_MANAGE:
            case ttLOAN_PAY: {
                // TBD
                return true;
            }

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

}  // namespace xrpl
