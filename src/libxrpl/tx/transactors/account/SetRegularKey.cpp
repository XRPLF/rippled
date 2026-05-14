/** @file
 *  @brief Implementation of the SetRegularKey transactor.
 *
 *  Handles assignment and removal of an account's regular (secondary) signing
 *  key.  The three framework entry points implemented here are, in pipeline
 *  order: `calculateBaseFee` (fee waiver for first-time use), `preflight`
 *  (self-referential key rejection), and `doApply` (SLE mutation with
 *  lockout-prevention guard).
 */
#include <xrpl/tx/transactors/account/SetRegularKey.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <memory>

namespace xrpl {

/** Compute the fee, returning zero when the one-time "password" waiver applies.
 *
 *  Three conditions must all hold for the waiver:
 *  1. `sfSigningPubKey` must be a recognised key type — checked via
 *     `publicKeyType` before `calcAccountID` to avoid a crash on empty or
 *     malformed keys (e.g. in multi-sig or inner-batch contexts where the
 *     field is intentionally blank).
 *  2. The derived account ID must equal `sfAccount` — i.e. the transaction
 *     was signed with the account's own master key, not a regular key or
 *     third-party signer.
 *  3. `lsfPasswordSpent` must be clear on the account SLE — the waiver has
 *     not already been consumed.
 *
 *  When the waiver fires, `doApply` will set `lsfPasswordSpent` so
 *  subsequent attempts are charged the normal fee.
 */
XRPAmount
SetRegularKey::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    auto const id = tx.getAccountID(sfAccount);
    auto const spk = tx.getSigningPubKey();

    if (publicKeyType(makeSlice(spk)))
    {
        if (calcAccountID(PublicKey(makeSlice(spk))) == id)
        {
            auto const sle = view.read(keylet::account(id));

            if (sle && ((sle->getFlags() & lsfPasswordSpent) == 0u))
            {
                return XRPAmount{0};
            }
        }
    }

    return Transactor::calculateBaseFee(view, tx);
}

/** Stateless validity check: reject self-referential regular key.
 *
 *  Setting `sfRegularKey` to the account's own address is semantically
 *  circular and would interfere with the remove-regular-key path in
 *  `doApply`, which distinguishes "no field present" from "field equals
 *  account".  Absence of `sfRegularKey` is valid here — it signals intent
 *  to remove the key and is handled in `doApply`.
 *
 *  @return `temBAD_REGKEY` if `sfRegularKey == sfAccount`; `tesSUCCESS`
 *      otherwise.
 */
NotTEC
SetRegularKey::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.isFieldPresent(sfRegularKey) &&
        (ctx.tx.getAccountID(sfRegularKey) == ctx.tx.getAccountID(sfAccount)))
    {
        return temBAD_REGKEY;
    }

    return tesSUCCESS;
}

/** Write or remove the regular key on the account SLE.
 *
 *  **Fee-waiver bookkeeping**: if the fee paid is below the minimum (i.e. the
 *  zero-fee waiver fired in `calculateBaseFee`), `lsfPasswordSpent` is set to
 *  prevent a second free rotation.
 *
 *  **Set path** (`sfRegularKey` present): writes the address directly to the
 *  `AccountRoot` entry.  No further validation is needed — the address is an
 *  opaque 160-bit identifier used only by the signing subsystem on future
 *  transactions.
 *
 *  **Remove path** (`sfRegularKey` absent): enforces the lockout-prevention
 *  invariant before clearing the field.  If `lsfDisableMaster` is set *and*
 *  no multisig signer list exists, removal would leave the account with no
 *  valid signing path and permanently inaccessible — rejected with
 *  `tecNO_ALTERNATIVE_KEY`.  Either a live master key or a signer list alone
 *  is sufficient to allow removal.
 *
 *  @return `tesSUCCESS` on success; `tecNO_ALTERNATIVE_KEY` if removal would
 *      lock the account out; `tefINTERNAL` (unreachable in practice, guarded
 *      by LCOV_EXCL_LINE) if the account SLE is missing.
 */
TER
SetRegularKey::doApply()
{
    auto const sle = view().peek(keylet::account(account_));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (!minimumFee(ctx_.registry, ctx_.baseFee, view().fees(), view().flags()))
        sle->setFlag(lsfPasswordSpent);

    if (ctx_.tx.isFieldPresent(sfRegularKey))
    {
        sle->setAccountID(sfRegularKey, ctx_.tx.getAccountID(sfRegularKey));
    }
    else
    {
        if (sle->isFlag(lsfDisableMaster) && !view().peek(keylet::signers(account_)))
            return tecNO_ALTERNATIVE_KEY;

        sle->makeFieldAbsent(sfRegularKey);
    }

    ctx_.view().update(sle);

    return tesSUCCESS;
}

void
SetRegularKey::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
SetRegularKey::finalizeInvariants(
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
