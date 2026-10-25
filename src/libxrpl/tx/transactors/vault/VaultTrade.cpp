#include <xrpl/tx/transactors/vault/VaultTrade.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/scope.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/ledger/helpers/VaultManagerHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/paths/Flow.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl {

namespace {

STObject const*
findEntry(STArray const& mandate, Asset const& asset)
{
    for (auto const& entry : mandate)
    {
        if (entry[sfAsset] == asset)
            return &entry;
    }
    return nullptr;
}

// The mandate in force: a pending mandate counts from sfPendingUntil.
STArray const&
effectiveMandate(ReadView const& view, SLE const& manager)
{
    if (manager.isFieldPresent(sfPendingUpdate) &&
        view.parentCloseTime().time_since_epoch().count() >= manager[sfPendingUntil])
    {
        auto const& pending = manager.getFieldObject(sfPendingUpdate);
        if (pending.isFieldPresent(sfMandate))
            return pending.getFieldArray(sfMandate);
    }
    return manager.getFieldArray(sfMandate);
}

}  // namespace

std::uint32_t
VaultTrade::getFlagsMask(PreflightContext const& ctx)
{
    return tfVaultTradeMask;
}

NotTEC
VaultTrade::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    if (tx[sfVaultID] == beast::kZero)
        return temMALFORMED;

    auto const amount = tx[sfAmount];
    auto const sendMax = tx[sfSendMax];
    if (!isLegalNet(amount) || !isLegalNet(sendMax) || amount <= beast::kZero ||
        sendMax <= beast::kZero)
        return temBAD_AMOUNT;
    if (amount.asset() == sendMax.asset())
        return temMALFORMED;

    if (auto const deliverMin = tx[~sfDeliverMin])
    {
        if (!tx.isFlag(tfPartialPayment) || !isLegalNet(*deliverMin) ||
            *deliverMin <= beast::kZero || deliverMin->asset() != amount.asset() ||
            *deliverMin > amount)
            return temBAD_AMOUNT;
    }

    if (auto const domain = tx[~sfDomainID]; domain && *domain == beast::kZero)
        return temMALFORMED;

    // MPTs reach order books and AMMs only with MPTokensV2.
    if ((amount.holds<MPTIssue>() || sendMax.holds<MPTIssue>()) &&
        !ctx.rules.enabled(featureMPTokensV2))
        return temDISABLED;

    return tesSUCCESS;
}

TER
VaultTrade::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;
    auto const vaultID = tx[sfVaultID];
    auto const vault = ctx.view.read(keylet::vault(vaultID));
    auto const manager = ctx.view.read(keylet::vaultManager(vaultID));
    if (!vault || !manager)
        return tecNO_ENTRY;

    if (tx[sfAccount] != effectiveVaultRoles(ctx.view, manager).manager)
        return tecNO_PERMISSION;

    auto const amount = tx[sfAmount];
    auto const sendMax = tx[sfSendMax];
    Asset const vaultAsset = vault->at(sfAsset);
    bool const open = sendMax.asset() == vaultAsset;
    if (!open && amount.asset() != vaultAsset)
        return tecWRONG_ASSET;
    Asset const positionAsset = open ? amount.asset() : sendMax.asset();
    auto const* entry = findEntry(effectiveMandate(ctx.view, *manager), positionAsset);
    if (entry == nullptr)
        return tecWRONG_ASSET;

    // A mandate domain confines the trade to that domain's books.
    auto const txDomain = tx[~sfDomainID];
    if (auto const required = (*entry)[~sfDomainID]; required && txDomain != required)
        return tecNO_PERMISSION;
    if (txDomain && !ctx.view.exists(keylet::permissionedDomain(*txDomain)))
        return tecNO_PERMISSION;

    AccountID const pseudo = vault->at(sfAccount);
    if (auto const ter = checkFrozen(ctx.view, pseudo, amount.asset()))
        return ter;
    if (auto const ter = checkFrozen(ctx.view, pseudo, sendMax.asset()))
        return ter;

    if (!open)
    {
        if (sendMax > positionHolding(ctx.view, vault, positionAsset))
            return tecINSUFFICIENT_FUNDS;
        return tesSUCCESS;
    }

    auto const now = ctx.view.parentCloseTime().time_since_epoch().count();
    if (now < vault->at(sfMarkHaltUntil))
        return tecDEALING_HALTED;
    if (getVaultKind(vault) == VaultKind::ClosedEnded && now >= vault->at(sfRedemptionDate))
        return tecNO_PERMISSION;
    // An asset a waiting mandate drops takes no new money.
    if (manager->isFieldPresent(sfPendingUpdate))
    {
        auto const& pending = manager->getFieldObject(sfPendingUpdate);
        if (pending.isFieldPresent(sfMandate) &&
            findEntry(pending.getFieldArray(sfMandate), positionAsset) == nullptr)
            return tecNO_PERMISSION;
    }
    if (Number{sendMax} > vault->at(sfAssetsAvailable))
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

TER
VaultTrade::doApply()
{
    auto const& tx = ctx_.tx;
    auto const vaultID = tx[sfVaultID];
    auto const amount = tx[sfAmount];
    auto const sendMax = tx[sfSendMax];

    {
        auto vault = view().peek(keylet::vault(vaultID));
        auto manager = view().peek(keylet::vaultManager(vaultID));
        if (!vault || !manager)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        if (auto const ter = applyDuePendingUpdate(view(), manager, vault, j_); !isTesSuccess(ter))
            return ter;
    }

    auto const vaultRead = view().read(keylet::vault(vaultID));
    Asset const vaultAsset = vaultRead->at(sfAsset);
    AccountID const pseudo = vaultRead->at(sfAccount);
    AccountID const owner = vaultRead->at(sfOwner);
    bool const open = sendMax.asset() == vaultAsset;
    Asset const positionAsset = open ? amount.asset() : sendMax.asset();
    Asset const received = amount.asset();
    Number const holdingBefore = positionHolding(view(), vaultRead, positionAsset);

    // The pseudo-account needs a holding of what it receives. The owner
    // provisions the vault, so its balance backs the reserve check.
    if (!holdingExists(view(), pseudo, received))
    {
        auto const ownerSle = view().read(keylet::account(owner));
        if (!ownerSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        if (auto const ter = addEmptyHolding(
                ctx_.getApplyViewContext(), pseudo, ownerSle->at(sfBalance).xrp(), received, j_))
            return ter;
    }

    STAmount spent;
    STAmount got;
    {
        PaymentSandbox psb(&view());
        {
            // A vault pseudo-account's trust lines carry a zero limit; lift the
            // receiving line's limit while flow runs, as CheckCash does.
            std::optional<Keylet> lineKey;
            STAmount savedLimit;
            bool const pseudoLow =
                !received.native() && received.holds<Issue>() && pseudo < received.getIssuer();
            if (received.holds<Issue>() && !received.native())
            {
                lineKey = keylet::trustLine(pseudo, received.get<Issue>());
                auto const line = psb.peek(*lineKey);
                if (!line)
                    return tecNO_LINE;  // LCOV_EXCL_LINE
                SF_AMOUNT const& limitField = pseudoLow ? sfLowLimit : sfHighLimit;
                savedLimit = line->at(limitField);
                line->at(limitField) = STAmount{
                    Issue{received.get<Issue>().currency, pseudo},
                    STAmount::kMaxValue,
                    STAmount::kMaxOffset};
            }
            ScopeExit const restore([&psb, &lineKey, &savedLimit, pseudoLow]() {
                if (!lineKey)
                    return;
                if (auto const line = psb.peek(*lineKey))
                    line->at(pseudoLow ? sfLowLimit : sfHighLimit) = savedLimit;
            });

            auto const result = flow(
                psb,
                amount,
                pseudo,
                pseudo,
                STPathSet{},
                true,  // default path
                tx.isFlag(tfPartialPayment),
                false,  // as a Payment: the vault pays transfer fees
                OfferCrossing::No,
                std::nullopt,
                sendMax,
                tx[~sfDomainID],
                j_);
            if (auto const ter = result.result(); !isTesSuccess(ter))
                return isTerRetry(ter) ? TER{tecPATH_DRY} : ter;
            if (auto const deliverMin = tx[~sfDeliverMin];
                deliverMin && result.actualAmountOut < *deliverMin)
                return tecPATH_PARTIAL;
            spent = result.actualAmountIn;
            got = result.actualAmountOut;
        }
        psb.apply(ctx_.rawView());
    }

    auto vault = view().peek(keylet::vault(vaultID));
    auto manager = view().peek(keylet::vaultManager(vaultID));
    if (!vault || !manager)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    auto const now = view().parentCloseTime().time_since_epoch().count();

    if (!open)
        return close(vault, manager, positionAsset, holdingBefore, spent, got);

    // The unit of account left the vault for the position.
    vault->at(sfAssetsTotal) -= Number{spent};
    vault->at(sfAssetsAvailable) -= Number{spent};

    auto const positionKey = keylet::vaultPosition(vaultID, positionAsset);
    if (auto position = view().peek(positionKey))
    {
        position->at(sfCostBasis) += Number{spent};
        view().update(position);
        associateAsset(*position, vaultAsset);
    }
    else
    {
        auto ownerSle = view().peek(keylet::account(owner));
        if (!ownerSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        auto sle = std::make_shared<SLE>(positionKey);
        if (auto const ter = dirLink(view(), owner, sle))
            return ter;  // LCOV_EXCL_LINE
        increaseOwnerCount(view(), ownerSle, {}, 1, j_);
        if (ownerSle->at(sfBalance)->xrp() < accountReserve(view(), ownerSle, j_))
            return tecINSUFFICIENT_RESERVE;

        // The first mark is the execution price: a transacted figure.
        sle->at(sfVaultID) = vaultID;
        sle->at(sfAsset) = STIssue{sfAsset, positionAsset};
        sle->at(sfMarkPrice) = Number{spent} / Number{got};
        sle->at(sfLastMarkTime) = now;
        sle->at(sfCostBasis) = Number{spent};
        view().insert(sle);
        associateAsset(*sle, vaultAsset);
        vault->at(sfManagedPositionCount) += 1;
    }

    view().update(vault);
    associateAsset(*vault, vaultAsset);

    // An open may not leave deployment, the position or cover past its limit.
    Number const deployed = vaultPositionsValue(view(), vault);
    if (deployed > manager->at(sfDeployedMaximum))
        return tecLIMIT_EXCEEDED;
    auto const* entry = findEntry(manager->getFieldArray(sfMandate), positionAsset);
    if (entry == nullptr)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if (Number const cap = (*entry)[~sfPositionMaximum].value_or(Number{});
        cap != Number{} && positionValue(view(), vault, view().read(positionKey)) > cap)
        return tecLIMIT_EXCEEDED;
    if (minimumManagerCover(deployed, *manager) > manager->at(sfCoverAvailable))
        return tecLIMIT_EXCEEDED;
    return tesSUCCESS;
}

TER
VaultTrade::close(
    SLE::ref vault,
    SLE::ref manager,
    Asset const& positionAsset,
    Number const& holdingBefore,
    STAmount const& sold,
    STAmount const& received)
{
    auto const vaultID = vault->key();
    Asset const vaultAsset = vault->at(sfAsset);
    AccountID const pseudo = vault->at(sfAccount);
    AccountID const owner = vault->at(sfOwner);

    auto position = view().peek(keylet::vaultPosition(vaultID, positionAsset));
    if (!position)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    vault->at(sfAssetsTotal) += Number{received};
    vault->at(sfAssetsAvailable) += Number{received};

    // Cost basis falls pro rata with the quantity sold.
    Number const costPortion = [&] {
        NumberRoundModeGuard const guard(Number::RoundingMode::Downward);
        return position->at(sfCostBasis) * Number{sold} / holdingBefore;
    }();
    position->at(sfCostBasis) -= costPortion;

    // A sale below cost draws first-loss cover: realized loss against cost,
    // never mark-to-mark movement, so only the manager's trades consume it.
    if (Number{received} < costPortion)
    {
        Number const loss = costPortion - Number{received};
        Number const cap = [&] {
            NumberRoundModeGuard const guard(Number::RoundingMode::Downward);
            return tenthBipsOfValue(
                Number{manager->at(sfCoverAvailable)},
                TenthBips32{manager->at(sfCoverRateLiquidation)});
        }();
        STAmount const covered{vaultAsset, std::min(loss, cap)};
        if (covered > beast::kZero)
        {
            if (auto const ter = accountSend(
                    view(), manager->at(sfAccount), pseudo, covered, j_, {}, WaiveTransferFee::Yes))
                return ter;
            manager->at(sfCoverAvailable) -= Number{covered};
            view().update(manager);
            associateAsset(*manager, vaultAsset);
            vault->at(sfAssetsTotal) += Number{covered};
            vault->at(sfAssetsAvailable) += Number{covered};
        }
    }

    if (positionHolding(view(), vault, positionAsset) == beast::kZero)
    {
        if (!view().dirRemove(
                keylet::ownerDir(owner), position->at(sfOwnerNode), position->key(), false))
            return tefBAD_LEDGER;  // LCOV_EXCL_LINE
        auto ownerSle = view().peek(keylet::account(owner));
        if (!ownerSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        decreaseOwnerCountForObject(view(), ownerSle, position, 1, j_);
        view().erase(position);
        vault->at(sfManagedPositionCount) -= 1;
        if (auto const ter =
                removeEmptyHolding(ctx_.getApplyViewContext(), pseudo, positionAsset, j_))
            return ter;
    }
    else
    {
        view().update(position);
        associateAsset(*position, vaultAsset);
    }

    view().update(vault);
    associateAsset(*vault, vaultAsset);
    return tesSUCCESS;
}

void
VaultTrade::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
VaultTrade::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
