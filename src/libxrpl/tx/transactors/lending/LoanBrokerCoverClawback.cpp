/** @file
 *  Implements `LoanBrokerCoverClawback`: the transaction that lets the
 *  original asset issuer forcibly reclaim cover funds from a loan broker's
 *  pseudo-account, subject to a minimum cover-to-debt ratio enforced via
 *  asymmetric rounding.
 *
 *  The broker may be identified either explicitly via `sfLoanBrokerID` or
 *  implicitly by encoding the pseudo-account as the IOU issuer in `sfAmount`.
 *  XRP assets and MPTs cannot use the implicit path.
 *
 *  @see LoanBrokerCoverDeposit   structural inverse — adds cover, same
 *      `WaiveTransferFee::Yes` convention
 *  @see LoanBrokerCoverWithdraw  voluntary cover removal by broker owner
 */
#include <xrpl/tx/transactors/lending/LoanBrokerCoverClawback.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <memory>
#include <optional>
#include <variant>

namespace xrpl {

bool
LoanBrokerCoverClawback::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx.rules, ctx.tx);
}

NotTEC
LoanBrokerCoverClawback::preflight(PreflightContext const& ctx)
{
    auto const brokerID = ctx.tx[~sfLoanBrokerID];
    auto const amount = ctx.tx[~sfAmount];

    if (!brokerID && !amount)
        return temINVALID;

    if (brokerID && *brokerID == beast::kZERO)
        return temINVALID;

    if (amount)
    {
        // XRP has no counterparty, and thus nobody can claw it back
        if (amount->native())
            return temBAD_AMOUNT;

        // Zero is OK, and indicates "take it all" (down to the minimum cover)
        if (*amount < beast::kZERO)
            return temBAD_AMOUNT;

        // This should be redundant
        if (!isLegalNet(*amount))
            return temBAD_AMOUNT;  // LCOV_EXCL_LINE

        if (!brokerID)
        {
            if (amount->holds<MPTIssue>())
                return temINVALID;

            auto const account = ctx.tx[sfAccount];
            // Since we don't have a LoanBrokerID, holder _should_ be the loan
            // broker's pseudo-account, but we don't know yet whether it is, so
            // use a generic placeholder name.
            auto const holder = amount->getIssuer();
            if (holder == account || holder == beast::kZERO)
                return temINVALID;
        }
    }

    return tesSUCCESS;
}

/** Resolve the target broker's ledger ID from the transaction.
 *
 *  Supports two identification modes:
 *  - **Explicit**: `sfLoanBrokerID` is present in the transaction.
 *  - **Implicit**: `sfAmount` is an IOU whose `issuer` field is the broker's
 *    pseudo-account; the function reads that account's SLE and extracts its
 *    `sfLoanBrokerID` field.
 *
 *  The implicit path exists because callers may naturally express the target
 *  using the trust-line issuer convention (pseudo-account as issuer) without
 *  knowing the broker's object ID up front.  `preflight` ensures the implicit
 *  path is only reached when `sfAmount` holds an IOU.
 *
 *  @param view  Read-only ledger view used to look up the pseudo-account SLE.
 *  @param tx    The transaction being applied.
 *  @return The broker's `uint256` object ID on success, or:
 *      - `tecINTERNAL` if the transaction state is inconsistent with what
 *        `preflight` should have rejected (unreachable in practice).
 *      - `tecNO_ENTRY` if the IOU issuer account does not exist.
 *      - `tecOBJECT_NOT_FOUND` if the IOU issuer account exists but is not
 *        a loan-broker pseudo-account.
 */
Expected<uint256, TER>
determineBrokerID(ReadView const& view, STTx const& tx)
{
    // If the broker ID was provided in the transaction, that's all we
    // need.
    if (auto const brokerID = tx[~sfLoanBrokerID])
        return *brokerID;

    // If the broker ID was not provided, and the amount is either
    // absent or holds a non-IOU - including MPT, something went wrong,
    // because that should have been rejected in preflight().
    auto const dstAmount = tx[~sfAmount];
    if (!dstAmount || !dstAmount->holds<Issue>())
        return Unexpected{tecINTERNAL};  // LCOV_EXCL_LINE

    // Every trust line is bidirectional. Both sides are simultaneously
    // issuer and holder. For this transaction, the Account is acting as
    // a holder, and clawing back funds from the LoanBroker
    // Pseudo-account acting as holder. If the Amount is an IOU, and the
    // `issuer` field specified in that Amount is a LoanBroker
    // Pseudo-account, we can get the LoanBrokerID from there.
    //
    // Thus, Amount.issuer _should_ be the loan broker's
    // pseudo-account, but we don't know yet whether it is.
    auto const maybePseudo = dstAmount->getIssuer();
    auto const sle = view.read(keylet::account(maybePseudo));

    // If the account was not found, the transaction can't go further.
    if (!sle)
        return Unexpected{tecNO_ENTRY};

    // If the account was found, and has a LoanBrokerID (and therefore
    // is a pseudo-account), that's the
    // answer we need.
    if (auto const brokerID = sle->at(~sfLoanBrokerID))
        return *brokerID;

    // If the account does not have a LoanBrokerID, the transaction
    // can't go further, even if it's a different type of Pseudo-account.
    return Unexpected{tecOBJECT_NOT_FOUND};
    // Or tecWRONG_ASSET?
}

/** Normalize the clawback asset to use the submitting account as issuer.
 *
 *  IOU trust lines are bidirectional: the same line can be expressed with
 *  either endpoint as the `issuer` field.  This function canonicalises the
 *  asset so that comparisons against the vault's `sfAsset` (which always
 *  uses the actual issuer account) work correctly regardless of which
 *  perspective the submitter used when constructing `sfAmount`.
 *
 *  MPT assets are returned as-is because MPTs have no trust-line issuer
 *  ambiguity.
 *
 *  @param view                  Read-only ledger view (currently unused;
 *      reserved for future validation).
 *  @param account               The submitting account (the asset issuer).
 *  @param brokerPseudoAccountID The broker pseudo-account ID resolved by
 *      `determineBrokerID`.
 *  @param amount                The `sfAmount` from the transaction, which may
 *      encode the asset from either the issuer's or pseudo-account's
 *      perspective.
 *  @return The canonical `Asset` with `account` as issuer on success, or
 *      `tecWRONG_ASSET` if the IOU issuer matches neither `account` nor
 *      `brokerPseudoAccountID`.
 */
Expected<Asset, TER>
determineAsset(
    ReadView const& view,
    AccountID const& account,
    AccountID const& brokerPseudoAccountID,
    STAmount const& amount)
{
    if (amount.holds<MPTIssue>())
        return amount.asset();

    // An IOU has an issue, which could be either end of the trust line.
    // This check only applies to IOUs
    auto const holder = amount.getIssuer();

    // holder can be the submitting account (the issuer of the asset) if a
    // LoanBrokerID was provided in the transaction.
    if (holder == account)
    {
        return amount.asset();
    }
    if (holder == brokerPseudoAccountID)
    {
        // We want the asset to match the vault asset, so use the account as the
        // issuer
        return Issue{amount.get<Issue>().currency, account};
    }

    return Unexpected(tecWRONG_ASSET);
}

/** Compute the maximum amount the issuer may claw back while respecting the
 *  broker's minimum cover floor.
 *
 *  The floor is `sfDebtTotal × sfCoverRateMinimum` (in tenth-basis-points),
 *  computed with ceiling rounding so the ledger never permits cover to drop
 *  below the contractual minimum.  The remaining surplus
 *  (`sfCoverAvailable − minRequiredCover`) is then rounded down.  Asymmetric
 *  rounding is deliberate: ceiling on the required minimum, floor on the
 *  available surplus.
 *
 *  When `amount` is absent or zero the function returns the full surplus
 *  (`maxClawAmount`).  When `amount` is provided but exceeds the surplus it
 *  is silently capped — the submitter always receives at most what the floor
 *  allows, without needing to know the exact number in advance.
 *
 *  @param sleBroker   The broker SLE supplying `sfDebtTotal`,
 *      `sfCoverRateMinimum`, and `sfCoverAvailable`.
 *  @param vaultAsset  Canonical vault asset used to type the returned
 *      `STAmount` (avoids pseudo-account issuer ambiguity).
 *  @param amount      Requested claw amount, or absent/zero for "take all".
 *  @return The effective claw amount capped to `maxClawAmount`, or
 *      `tecINSUFFICIENT_FUNDS` if cover is already at or below the floor.
 */
Expected<STAmount, TER>
determineClawAmount(
    SLE const& sleBroker,
    Asset const& vaultAsset,
    std::optional<STAmount> const& amount)
{
    auto const maxClawAmount = [&]() {
        NumberRoundModeGuard const mg1(Number::RoundingMode::Upward);
        auto const minRequiredCover =
            tenthBipsOfValue(sleBroker[sfDebtTotal], TenthBips32(sleBroker[sfCoverRateMinimum]));
        NumberRoundModeGuard const mg2(Number::RoundingMode::Downward);
        return sleBroker[sfCoverAvailable] - minRequiredCover;
    }();
    if (maxClawAmount <= beast::kZERO)
        return Unexpected(tecINSUFFICIENT_FUNDS);

    // Use the vaultAsset here, because it will be the right type in all
    // circumstances. The amount may be an IOU indicating the pseudo-account's
    // asset, which is correct, but not what is needed here.
    if (!amount || *amount == beast::kZERO)
        return STAmount{vaultAsset, maxClawAmount};
    Number const magnitude{*amount};
    if (magnitude > maxClawAmount)
        return STAmount{vaultAsset, maxClawAmount};
    return STAmount{vaultAsset, magnitude};
}

/** Asset-type-specific permission check for the vault asset issuer.
 *
 *  Dispatched from `preclaim` via `std::visit` on the vault asset variant.
 *  Each specialisation enforces the clawback-permission model appropriate to
 *  its asset type: `Issue` checks IOU account flags; `MPTIssue` checks the
 *  `MPTIssuance` SLE flags.
 *
 *  @tparam T         Asset type: `Issue` or `MPTIssue`.
 *  @param ctx        Preclaim context with read-only ledger view.
 *  @param sleIssuer  The issuer's `AccountRoot` SLE.
 *  @param clawAmount The effective claw amount (used by the MPT path to derive
 *      the issuance keylet).
 *  @return `tesSUCCESS` if the issuer holds the required permission flag, or
 *      `tecNO_PERMISSION` / `tecOBJECT_NOT_FOUND` / `tecINTERNAL` otherwise.
 */
template <ValidIssueType T>
static TER
preclaimHelper(PreclaimContext const& ctx, SLE const& sleIssuer, STAmount const& clawAmount);

/** IOU specialisation: enforces `lsfAllowTrustLineClawback` and absence of
 *  `lsfNoFreeze` on the issuer account, mirroring the standard XRPL IOU
 *  clawback permission model.
 */
template <>
TER
preclaimHelper<Issue>(PreclaimContext const& ctx, SLE const& sleIssuer, STAmount const& clawAmount)
{
    if (!(sleIssuer.isFlag(lsfAllowTrustLineClawback)) || (sleIssuer.isFlag(lsfNoFreeze)))
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

/** MPT specialisation: looks up the `MPTIssuance` SLE and checks
 *  `lsfMPTCanClawback`.  Also asserts (redundantly) that the issuance's
 *  `sfIssuer` matches the submitting account — a consistency guard that
 *  should be unreachable given upstream checks.
 */
template <>
TER
preclaimHelper<MPTIssue>(
    PreclaimContext const& ctx,
    SLE const& sleIssuer,
    STAmount const& clawAmount)
{
    auto const issuanceKey = keylet::mptIssuance(clawAmount.get<MPTIssue>().getMptID());
    auto const sleIssuance = ctx.view.read(issuanceKey);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    if (!sleIssuance->isFlag(lsfMPTCanClawback))
        return tecNO_PERMISSION;

    // With all the checking already done, this should be impossible
    if (sleIssuance->at(sfIssuer) != sleIssuer[sfAccount])
        return tecINTERNAL;  // LCOV_EXCL_LINE

    return tesSUCCESS;
}

TER
LoanBrokerCoverClawback::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;

    auto const account = tx[sfAccount];
    auto const findBrokerID = determineBrokerID(ctx.view, tx);
    if (!findBrokerID)
        return findBrokerID.error();
    auto const brokerID = *findBrokerID;
    auto const amount = tx[~sfAmount];

    auto const sleBroker = ctx.view.read(keylet::loanbroker(brokerID));
    if (!sleBroker)
    {
        JLOG(ctx.j.warn()) << "LoanBroker does not exist.";
        return tecNO_ENTRY;
    }

    auto const brokerPseudoAccountID = sleBroker->at(sfAccount);

    auto const vault = ctx.view.read(keylet::vault(sleBroker->at(sfVaultID)));
    if (!vault)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.fatal()) << "Vault is missing for Broker " << brokerID;
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const vaultAsset = vault->at(sfAsset);

    if (vaultAsset.native())
    {
        JLOG(ctx.j.warn()) << "Cannot clawback native asset.";
        return tecNO_PERMISSION;
    }

    if (vaultAsset.getIssuer() != account)
    {
        JLOG(ctx.j.warn()) << "Account is not the issuer of the vault asset.";
        return tecNO_PERMISSION;
    }

    if (amount)
    {
        auto const findAsset = determineAsset(ctx.view, account, brokerPseudoAccountID, *amount);
        if (!findAsset)
            return findAsset.error();
        auto const txAsset = *findAsset;
        if (txAsset != vaultAsset)
        {
            JLOG(ctx.j.warn()) << "Account is the correct issuer, but trying "
                                  "to clawback the wrong asset from LoanBroker";
            return tecWRONG_ASSET;
        }
    }

    auto const findClawAmount = determineClawAmount(*sleBroker, vaultAsset, amount);
    if (!findClawAmount)
    {
        JLOG(ctx.j.warn()) << "LoanBroker cover is already at minimum.";
        return findClawAmount.error();
    }
    STAmount const& clawAmount = *findClawAmount;

    if (accountHolds(
            ctx.view,
            brokerPseudoAccountID,
            vaultAsset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            ctx.j) < clawAmount)
        return tecINTERNAL;  // tecINSUFFICIENT_FUNDS; LCOV_EXCL_LINE

    auto const sleIssuer = ctx.view.read(keylet::account(vaultAsset.getIssuer()));
    if (!sleIssuer)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.fatal()) << "Issuer account does not exist.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    return std::visit(
        [&]<typename T>(T const&) { return preclaimHelper<T>(ctx, *sleIssuer, clawAmount); },
        vaultAsset.value());
}

TER
LoanBrokerCoverClawback::doApply()
{
    auto const& tx = ctx_.tx;
    auto const account = tx[sfAccount];
    auto const findBrokerID = determineBrokerID(view(), tx);
    if (!findBrokerID)
        return tecINTERNAL;  // LCOV_EXCL_LINE
    auto const brokerID = *findBrokerID;
    auto const amount = tx[~sfAmount];

    auto sleBroker = view().peek(keylet::loanbroker(brokerID));
    if (!sleBroker)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const brokerPseudoID = *sleBroker->at(sfAccount);

    auto const vault = view().read(keylet::vault(sleBroker->at(sfVaultID)));
    if (!vault)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const vaultAsset = vault->at(sfAsset);

    auto const findClawAmount = determineClawAmount(*sleBroker, vaultAsset, amount);
    if (!findClawAmount)
        return tecINTERNAL;  // LCOV_EXCL_LINE
    STAmount const& clawAmount = *findClawAmount;
    // Just for paranoia's sake
    if (clawAmount.native())
        return tecINTERNAL;  // LCOV_EXCL_LINE

    sleBroker->at(sfCoverAvailable) -= clawAmount;
    view().update(sleBroker);

    associateAsset(*sleBroker, vaultAsset);

    return accountSend(view(), brokerPseudoID, account, clawAmount, j_, WaiveTransferFee::Yes);
}

void
LoanBrokerCoverClawback::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
LoanBrokerCoverClawback::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

//------------------------------------------------------------------------------

}  // namespace xrpl
