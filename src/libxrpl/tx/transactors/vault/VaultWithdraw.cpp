/** @file VaultWithdraw.cpp
 *  Implementation of the `ttVAULT_WITHDRAW` transactor.
 *
 *  Redeems vault-share MPTokens for the underlying pooled asset — the inverse
 *  of `VaultDeposit`. The user specifies `sfAmount` in either the vault's
 *  underlying asset (fixed assets, variable shares) or the vault's share MPT
 *  (fixed shares, variable assets); the complementary quantity is computed via
 *  `assetsToSharesWithdraw` / `sharesToAssetsWithdraw` from VaultHelpers.h.
 *
 *  Liquidity is gated on `sfAssetsAvailable` rather than the vault
 *  pseudo-account's raw balance, so that assets pledged to lending brokers
 *  are correctly excluded from on-demand redemptions.
 */
#include <xrpl/tx/transactors/vault/VaultWithdraw.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <memory>
#include <stdexcept>

namespace xrpl {

/** Stateless, ledger-free validation of a `ttVAULT_WITHDRAW` transaction.
 *
 *  Checks are ordered cheapest-first and fail on the first violation. Each
 *  rejected condition is a programming error or a structurally invalid
 *  request that would be meaningless against any ledger state:
 *
 *  - A zero `sfVaultID` is always a null key; no valid vault can have this ID.
 *  - A non-positive `sfAmount` cannot represent a redemption of any value.
 *  - A zero explicit `sfDestination`, when present, cannot identify any real
 *    account; only the absent-field case (withdraw to self) is permitted to
 *    omit a destination.
 *
 *  @param ctx  Preflight context carrying the transaction and active rules.
 *  @return `tesSUCCESS`, `temMALFORMED`, or `temBAD_AMOUNT`.
 */
NotTEC
VaultWithdraw::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfVaultID] == beast::kZERO)
    {
        JLOG(ctx.j.debug()) << "VaultWithdraw: zero/empty vault ID.";
        return temMALFORMED;
    }

    if (ctx.tx[sfAmount] <= beast::kZERO)
        return temBAD_AMOUNT;

    if (auto const destination = ctx.tx[~sfDestination])
    {
        if (*destination == beast::kZERO)
        {
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

/** Read-only ledger validation for a vault withdrawal.
 *
 *  Checks are ordered from cheapest to most expensive and fail fast on the
 *  first violation. Several design choices merit explanation:
 *
 *  **`fixSecurity3_1_3` and the share-limit gap**: before this amendment, when
 *  `sfAmount` was share-denominated, the withdrawal-limit check (`canWithdraw`)
 *  was silently skipped — only the simpler two-argument overload ran. Post-
 *  amendment, shares are first converted to an equivalent asset amount via
 *  `sharesToAssetsWithdraw`, and that asset amount is fed to the full
 *  four-argument `canWithdraw` overload. Overflow during this conversion
 *  (easily reachable with large `sfScale` values) returns `tecPATH_DRY` rather
 *  than crashing; the log level is deliberately `debug` because this is a
 *  user-triggerable condition, not a ledger-corruption sentinel.
 *
 *  **Two-tier authorization**: if the destination is the submitting account
 *  itself, `WeakAuth` is used — `doApply` will create a trust line or MPToken
 *  on the submitter's behalf as needed. If `sfDestination` names a third party,
 *  `StrongAuth` is required: the holding must already exist before the
 *  withdrawal is applied.
 *
 *  **Dual freeze checks**: one call verifies the vault's underlying asset is
 *  not frozen for the destination (the receiver must be able to accept it), and
 *  a second call verifies the vault's share MPT is not frozen or locked for the
 *  submitter (the redeemer must be able to surrender shares).
 *
 *  The `sfWithdrawalPolicy` guard and the missing-issuance guard are marked
 *  `LCOV_EXCL` because `VaultCreate` invariants make them unreachable under
 *  correct ledger state.
 *
 *  @param ctx  Preclaim context carrying a read-only ledger view and rules.
 *  @return `tesSUCCESS`, `tecNO_ENTRY`, `tecWRONG_ASSET`, `tecPATH_DRY`,
 *      `tefINTERNAL`, or a code from `canTransfer`, `canWithdraw`,
 *      `requireAuth`, or `checkFrozen`.
 */
TER
VaultWithdraw::preclaim(PreclaimContext const& ctx)
{
    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecNO_ENTRY;

    auto const amount = ctx.tx[sfAmount];
    auto const vaultAsset = vault->at(sfAsset);
    auto const vaultShare = vault->at(sfShareMPTID);
    if (amount.asset() != vaultAsset && amount.asset() != vaultShare)
        return tecWRONG_ASSET;

    auto const& vaultAccount = vault->at(sfAccount);
    auto const& account = ctx.tx[sfAccount];
    auto const& dstAcct = ctx.tx[~sfDestination].value_or(account);
    if (auto ter = canTransfer(ctx.view, vaultAsset, vaultAccount, dstAcct); !isTesSuccess(ter))
    {
        JLOG(ctx.j.debug()) << "VaultWithdraw: vault assets are non-transferable.";
        return ter;
    }

    if (vault->at(sfWithdrawalPolicy) != kVAULT_STRATEGY_FIRST_COME_FIRST_SERVE)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error()) << "VaultWithdraw: invalid withdrawal policy.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    if (ctx.view.rules().enabled(fixSecurity3_1_3) && amount.asset() == vaultShare)
    {
        auto const sleIssuance = ctx.view.read(keylet::mptIssuance(vaultShare));
        if (!sleIssuance)
        {
            // LCOV_EXCL_START
            JLOG(ctx.j.error()) << "VaultWithdraw: missing issuance of vault shares.";
            return tefINTERNAL;
            // LCOV_EXCL_STOP
        }

        try
        {
            auto const maybeAssets = sharesToAssetsWithdraw(vault, sleIssuance, amount);
            if (!maybeAssets)
                return tefINTERNAL;  // LCOV_EXCL_LINE

            if (auto const ret = canWithdraw(
                    ctx.view,
                    account,
                    dstAcct,
                    *maybeAssets,
                    ctx.tx.isFieldPresent(sfDestinationTag)))
                return ret;
        }
        catch (std::overflow_error const&)
        {
            // It's easy to hit this exception from Number with large enough Scale
            // so we avoid spamming the log and only use debug here.
            JLOG(ctx.j.debug())  //
                << "VaultWithdraw: overflow error with"
                << " scale=" << (int)vault->at(sfScale)  //
                << ", assetsTotal=" << vault->at(sfAssetsTotal)
                << ", sharesTotal=" << sleIssuance->at(sfOutstandingAmount)
                << ", amount=" << amount.value();
            return tecPATH_DRY;
        }
    }
    else
    {
        if (auto const ret = canWithdraw(ctx.view, ctx.tx))
            return ret;
    }

    AuthType const authType = account == dstAcct ? AuthType::WeakAuth : AuthType::StrongAuth;
    if (auto const ter = requireAuth(ctx.view, vaultAsset, dstAcct, authType); !isTesSuccess(ter))
        return ter;

    if (auto const ret = checkFrozen(ctx.view, dstAcct, vaultAsset))
        return ret;

    if (auto const ret = checkFrozen(ctx.view, account, Asset{vaultShare}))
        return ret;

    return tesSUCCESS;
}

/** Mutate the ledger to execute a vault withdrawal.
 *
 *  ### Exchange rate computation (two modes)
 *
 *  **Asset-denominated** (`sfAmount.asset() == vaultAsset`): `assetsToSharesWithdraw`
 *  converts the requested asset quantity to an integer share count (truncating).
 *  The result is immediately re-converted via `sharesToAssetsWithdraw` to
 *  determine the exact assets to disburse — this double-conversion is not
 *  redundant: it corrects for MPT integer truncation so the vault always pays
 *  out the precise amount that the integer share count entitles, never more.
 *  If the first conversion produces zero shares, `tecPRECISION_LOSS` is returned
 *  (the requested amount is too small to represent even one share unit).
 *
 *  **Share-denominated** (`sfAmount.asset() == share`): the share count is used
 *  directly; `sharesToAssetsWithdraw` computes the corresponding asset payout.
 *  No rounding step is needed because the share count is already integral.
 *
 *  Both modes catch `std::overflow_error` and return `tecPATH_DRY`. The log
 *  level is `debug` because large `sfScale` values make overflow arithmetically
 *  trivial for legitimate users — this is not a ledger-corruption sentinel.
 *
 *  ### Liquidity gating
 *
 *  The check against `sfAssetsAvailable` (not the raw pseudo-account balance)
 *  is deliberate: a vault may have pledged assets to lending brokers, reducing
 *  what is actually liquid. `sfAssetsAvailable` tracks only unencumbered assets.
 *  Both `sfAssetsTotal` and `sfAssetsAvailable` are decremented by
 *  `assetsWithdrawn` on success.
 *
 *  ### Share redemption and MPToken cleanup
 *
 *  Shares flow back to the vault pseudo-account via `accountSend` with
 *  `WaiveTransferFee::Yes` — shares are internal bookkeeping tokens, not
 *  economic transfers subject to issuer-configured transfer fees.
 *
 *  After redemption, if the submitter's share balance drops to zero and the
 *  submitter is not the vault owner, `removeEmptyHolding` attempts to delete
 *  the now-empty MPToken. `tecHAS_OBLIGATIONS` is silently ignored (the holder
 *  has associated obligations and must retain the MPToken). Vault owners are
 *  excluded from cleanup because their MPToken may be needed for future
 *  deposits and internal accounting.
 *
 *  ### `lsfVaultPrivate` omission (intentional)
 *
 *  `doApply` does not re-check the `lsfVaultPrivate` flag. Possession of vault
 *  shares is proof of prior authorized deposit; withdrawal authorization is
 *  effectively irrevocable once shares are held, regardless of whether the
 *  vault's private flag or domain credentials have subsequently changed.
 *
 *  ### Completion ordering
 *
 *  `associateAsset` is called before `doWithdraw` to re-round stored `STNumber`
 *  fields to the vault asset's precision. Per the `STTakesAsset` contract, this
 *  must be the final write to the vault SLE. `doWithdraw` then handles trust
 *  line or MPToken creation for the destination (under `WeakAuth`) and executes
 *  the final `accountSend` from the vault pseudo-account to the destination.
 *
 *  @return `tesSUCCESS`, `tecPRECISION_LOSS`, `tecPATH_DRY`,
 *      `tecINSUFFICIENT_FUNDS`, `tefINTERNAL`, or a code from `accountSend`,
 *      `removeEmptyHolding`, or `doWithdraw`.
 */
TER
VaultWithdraw::doApply()
{
    auto const vault = view().peek(keylet::vault(ctx_.tx[sfVaultID]));
    if (!vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const mptIssuanceID = *((*vault)[sfShareMPTID]);
    auto const sleIssuance = view().read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultWithdraw: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    auto const amount = ctx_.tx[sfAmount];
    Asset const vaultAsset = vault->at(sfAsset);

    MPTIssue const share{mptIssuanceID};
    STAmount sharesRedeemed = {share};
    STAmount assetsWithdrawn;
    try
    {
        if (amount.asset() == vaultAsset)
        {
            {
                auto const maybeShares = assetsToSharesWithdraw(vault, sleIssuance, amount);
                if (!maybeShares)
                    return tecINTERNAL;  // LCOV_EXCL_LINE
                sharesRedeemed = *maybeShares;
            }

            if (sharesRedeemed == beast::kZERO)
                return tecPRECISION_LOSS;
            auto const maybeAssets = sharesToAssetsWithdraw(vault, sleIssuance, sharesRedeemed);
            if (!maybeAssets)
                return tecINTERNAL;  // LCOV_EXCL_LINE
            assetsWithdrawn = *maybeAssets;
        }
        else if (amount.asset() == share)
        {
            sharesRedeemed = amount;
            auto const maybeAssets = sharesToAssetsWithdraw(vault, sleIssuance, sharesRedeemed);
            if (!maybeAssets)
                return tecINTERNAL;  // LCOV_EXCL_LINE
            assetsWithdrawn = *maybeAssets;
        }
        else
        {
            return tefINTERNAL;  // LCOV_EXCL_LINE
        }
    }
    catch (std::overflow_error const&)
    {
        // It's easy to hit this exception from Number with large enough Scale
        // so we avoid spamming the log and only use debug here.
        JLOG(j_.debug())  //
            << "VaultWithdraw: overflow error with"
            << " scale=" << (int)vault->at(sfScale).value()  //
            << ", assetsTotal=" << vault->at(sfAssetsTotal).value()
            << ", sharesTotal=" << sleIssuance->at(sfOutstandingAmount)
            << ", amount=" << amount.value();
        return tecPATH_DRY;
    }

    if (accountHolds(
            view(), account_, share, FreezeHandling::ZeroIfFrozen, AuthHandling::IgnoreAuth, j_) <
        sharesRedeemed)
    {
        JLOG(j_.debug()) << "VaultWithdraw: account doesn't hold enough shares";
        return tecINSUFFICIENT_FUNDS;
    }

    auto assetsAvailable = vault->at(sfAssetsAvailable);
    auto assetsTotal = vault->at(sfAssetsTotal);
    [[maybe_unused]] auto const lossUnrealized = vault->at(sfLossUnrealized);
    XRPL_ASSERT(
        lossUnrealized <= (assetsTotal - assetsAvailable),
        "xrpl::VaultWithdraw::doApply : loss and assets do balance");

    if (*assetsAvailable < assetsWithdrawn)
    {
        JLOG(j_.debug()) << "VaultWithdraw: vault doesn't hold enough assets";
        return tecINSUFFICIENT_FUNDS;
    }

    assetsTotal -= assetsWithdrawn;
    assetsAvailable -= assetsWithdrawn;
    view().update(vault);

    auto const& vaultAccount = vault->at(sfAccount);
    if (auto const ter =
            accountSend(view(), account_, vaultAccount, sharesRedeemed, j_, WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    // Vault pseudo-account will never set lsfMPTAuthorized, so we ignore flags.
    if (account_ != vault->at(sfOwner))
    {
        if (auto const ter = removeEmptyHolding(view(), account_, sharesRedeemed.asset(), j_);
            isTesSuccess(ter))
        {
            JLOG(j_.debug())  //
                << "VaultWithdraw: removed empty MPToken for vault shares"
                << " MPTID=" << to_string(mptIssuanceID)  //
                << " account=" << toBase58(account_);
        }
        else if (ter != tecHAS_OBLIGATIONS)
        {
            // LCOV_EXCL_START
            JLOG(j_.error())  //
                << "VaultWithdraw: failed to remove MPToken for vault shares"
                << " MPTID=" << to_string(mptIssuanceID)  //
                << " account=" << toBase58(account_)      //
                << " with result: " << transToken(ter);
            return ter;
            // LCOV_EXCL_STOP
        }
    }

    auto const dstAcct = ctx_.tx[~sfDestination].value_or(account_);

    associateAsset(*vault, vaultAsset);

    return doWithdraw(
        view(), ctx_.tx, account_, dstAcct, vaultAccount, preFeeBalance_, assetsWithdrawn, j_);
}

void
VaultWithdraw::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
VaultWithdraw::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
