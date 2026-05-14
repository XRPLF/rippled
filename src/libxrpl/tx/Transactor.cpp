#include <xrpl/tx/Transactor.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/NetworkIDService.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/to_string.h>  // IWYU pragma: keep
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/DelegateHelpers.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/ledger/helpers/OfferHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>  // IWYU pragma: keep
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/SignerEntries.h>
#include <xrpl/tx/apply.h>
#include <xrpl/tx/applySteps.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace xrpl {

/** Gate every transaction on a minimal set of structural invariants.
 *
 *  Checks that apply to all transaction types before any type-specific
 *  validation: pseudo-transaction / batch-inner exclusivity, network-ID
 *  presence rules for legacy vs. modern networks, a non-zero transaction
 *  ID, and the absence of unrecognised flag bits.
 *
 *  @param ctx      Preflight context carrying the transaction and network state.
 *  @param flagMask Bitmask of bits that must NOT be set in the transaction
 *      flags; bits set here are treated as invalid for this transaction type.
 *  @return `tesSUCCESS` if all checks pass; a `tel*` or `tem*` code otherwise.
 *  @note Pseudo-transactions are exempt from NetworkID checks unless they
 *      explicitly carry `sfNetworkID`.
 */
NotTEC
preflight0(PreflightContext const& ctx, std::uint32_t flagMask)
{
    if (isPseudoTx(ctx.tx) && ctx.tx.isFlag(tfInnerBatchTxn))
    {
        JLOG(ctx.j.warn()) << "Pseudo transactions cannot contain the "
                              "tfInnerBatchTxn flag.";
        return temINVALID_FLAG;
    }

    if (!isPseudoTx(ctx.tx) || ctx.tx.isFieldPresent(sfNetworkID))
    {
        uint32_t const nodeNID = ctx.registry.get().getNetworkIDService().getNetworkID();
        std::optional<uint32_t> txNID = ctx.tx[~sfNetworkID];

        if (nodeNID <= 1024)
        {
            // Legacy networks (ID ≤ 1024) never carry sfNetworkID — its
            // presence would make the transaction non-canonical on those nets.
            if (txNID)
                return telNETWORK_ID_MAKES_TX_NON_CANONICAL;
        }
        else
        {
            // Modern networks require sfNetworkID to be present and to match
            // the local node's network ID, preventing cross-network replay.
            if (!txNID)
                return telREQUIRES_NETWORK_ID;

            if (*txNID != nodeNID)
                return telWRONG_NETWORK;
        }
    }

    auto const txID = ctx.tx.getTransactionID();

    if (txID == beast::kZERO)
    {
        JLOG(ctx.j.warn()) << "applyTransaction: transaction id may not be zero";
        return temINVALID;
    }

    if ((ctx.tx.getFlags() & flagMask) != 0u)
    {
        JLOG(ctx.j.debug()) << ctx.tx.peekAtField(sfTransactionType).getFullText()
                            << ": invalid flags.";
        return temINVALID_FLAG;
    }

    return tesSUCCESS;
}

namespace detail {

/** Validate that `sfSigningPubKey`, when present, is a recognised key type.
 *
 *  An empty `sfSigningPubKey` is valid (multi-sign or simulation); a
 *  non-empty key that is not a recognised curve type (secp256k1 / Ed25519)
 *  is rejected immediately.
 *
 *  @param sigObject The STObject that carries `sfSigningPubKey` (typically
 *      the transaction itself).
 *  @param j         Journal for diagnostic logging.
 *  @return `tesSUCCESS` if the key is absent or has a valid type;
 *      `temBAD_SIGNATURE` otherwise.
 */
NotTEC
preflightCheckSigningKey(STObject const& sigObject, beast::Journal j)
{
    if (auto const spk = sigObject.getFieldVL(sfSigningPubKey);
        !spk.empty() && !publicKeyType(makeSlice(spk)))
    {
        JLOG(j.debug()) << "preflightCheckSigningKey: invalid signing key";
        return temBAD_SIGNATURE;
    }
    return tesSUCCESS;
}

/** Validate signing-key state for dry-run (simulation) transactions.
 *
 *  Returns a result only when `TapDryRun` is set; returns `std::nullopt`
 *  for normal (non-simulated) transactions so the caller continues with
 *  regular validation.  In simulation mode a transaction must carry no
 *  real signature: `sfTxnSignature` must be absent or empty, all entries
 *  in `sfSigners` must have no signature, and `sfSigningPubKey` must be
 *  empty (to avoid ambiguous single-sign + multi-sign state).
 *
 *  @param flags     Apply flags for this invocation; checked for `TapDryRun`.
 *  @param sigObject The STObject holding signing fields (typically the tx).
 *  @param j         Journal for diagnostic logging.
 *  @return `tesSUCCESS` if simulation constraints are satisfied; `temINVALID`
 *      if they are violated; `std::nullopt` if `TapDryRun` is not set.
 *  @note The `temINVALID` branches are marked `LCOV_EXCL_LINE` because the
 *      `simulate` RPC validates these constraints before reaching preflight.
 */
std::optional<NotTEC>
preflightCheckSimulateKeys(ApplyFlags flags, STObject const& sigObject, beast::Journal j)
{
    if ((flags & TapDryRun) != 0u)  // simulation
    {
        std::optional<Slice> const signature = sigObject[~sfTxnSignature];
        if (signature && !signature->empty())
        {
            // NOTE: This code should never be hit because it's checked in the
            // `simulate` RPC
            return temINVALID;  // LCOV_EXCL_LINE
        }

        if (!sigObject.isFieldPresent(sfSigners))
        {
            // no signers, no signature - a valid simulation
            return tesSUCCESS;
        }

        for (auto const& signer : sigObject.getFieldArray(sfSigners))
        {
            if (signer.isFieldPresent(sfTxnSignature) && !signer[sfTxnSignature].empty())
            {
                // NOTE: This code should never be hit because it's
                // checked in the `simulate` RPC
                return temINVALID;  // LCOV_EXCL_LINE
            }
        }

        Slice const signingPubKey = sigObject[sfSigningPubKey];
        if (!signingPubKey.empty())
        {
            // trying to single-sign _and_ multi-sign a transaction
            return temINVALID;
        }
        return tesSUCCESS;
    }
    return {};
}

}  // namespace detail

/** Validate account identity, fee format, signing-key syntax, and ordering
 *  constraints common to all transaction types.
 *
 *  Calls `preflight0` for transaction-ID / NetworkID / flag-mask checks,
 *  then adds: non-zero `sfAccount`, non-negative native `sfFee`, valid
 *  `sfSigningPubKey` format, and the mutual-exclusion rule between Tickets
 *  and `sfAccountTxnID`.  Also validates `sfDelegate` field presence against
 *  the `featurePermissionDelegationV1_1` amendment.
 *
 *  @param ctx      Preflight context carrying the transaction and rules.
 *  @param flagMask Bitmask of disallowed flag bits, forwarded to `preflight0`.
 *  @return `tesSUCCESS` if all checks pass; a `tem*` or `tel*` error otherwise.
 *  @note Do not call this directly from a derived transactor's `preflight`.
 *      It is invoked automatically by `invokePreflight<T>`.
 */
NotTEC
Transactor::preflight1(PreflightContext const& ctx, std::uint32_t flagMask)
{
    if (ctx.tx.isFieldPresent(sfDelegate))
    {
        if (!ctx.rules.enabled(featurePermissionDelegationV1_1))
            return temDISABLED;

        if (ctx.tx[sfDelegate] == ctx.tx[sfAccount])
            return temBAD_SIGNER;
    }

    if (auto const ret = preflight0(ctx, flagMask))
        return ret;

    auto const id = ctx.tx.getAccountID(sfAccount);
    if (id == beast::kZERO)
    {
        JLOG(ctx.j.warn()) << "preflight1: bad account id";
        return temBAD_SRC_ACCOUNT;
    }

    auto const fee = ctx.tx.getFieldAmount(sfFee);
    if (!fee.native() || fee.negative() || !isLegalAmount(fee.xrp()))
    {
        JLOG(ctx.j.debug()) << "preflight1: invalid fee";
        return temBAD_FEE;
    }

    if (auto const ret = detail::preflightCheckSigningKey(ctx.tx, ctx.j))
        return ret;

    // An AccountTxnID field constrains transaction ordering more than the
    // Sequence field.  Tickets, on the other hand, reduce ordering
    // constraints.  Because Tickets and AccountTxnID work against one
    // another the combination is unsupported and treated as malformed.
    //
    // We return temINVALID for such transactions.
    if (ctx.tx.getSeqProxy().isTicket() && ctx.tx.isFieldPresent(sfAccountTxnID))
        return temINVALID;

    if (ctx.tx.isFlag(tfInnerBatchTxn) && !ctx.rules.enabled(featureBatch))
        return temINVALID_FLAG;

    XRPL_ASSERT(
        ctx.tx.isFlag(tfInnerBatchTxn) == ctx.parentBatchId.has_value() ||
            !ctx.rules.enabled(featureBatch),
        "Inner batch transaction must have a parent batch ID.");

    return tesSUCCESS;
}

/** Verify cryptographic signature validity using the hash-router cache.
 *
 *  In simulation mode (`TapDryRun`), delegates to
 *  `detail::preflightCheckSimulateKeys` and returns early, skipping all
 *  subsequent checks.  For `tfInnerBatchTxn` transactions the signature
 *  check is skipped entirely — the outer batch already provides
 *  authorization.  For all other transactions, consults the hash router's
 *  cached `Validity` state; `SigBad` returns `temINVALID`.
 *
 *  @param ctx Preflight context carrying the transaction, rules, and flags.
 *  @return `tesSUCCESS` if the signature is valid (or skipped legitimately);
 *      `temINVALID` if the cached validity is `SigBad` or simulation
 *      constraints are violated.
 *  @note Do not call this directly from a derived transactor's `preflight`.
 *      It is invoked automatically by `invokePreflight<T>`.
 */
NotTEC
Transactor::preflight2(PreflightContext const& ctx)
{
    if (auto const ret = detail::preflightCheckSimulateKeys(ctx.flags, ctx.tx, ctx.j))
    {
        // Skips following checks if the transaction is being simulated,
        // regardless of success or failure
        return *ret;
    }

    // It should be impossible for the InnerBatchTxn flag to be set without
    // featureBatch being enabled
    XRPL_ASSERT_PARTS(
        !ctx.tx.isFlag(tfInnerBatchTxn) || ctx.rules.enabled(featureBatch),
        "xrpl::Transactor::preflight2",
        "InnerBatch flag only set if feature enabled");
    // Skip signature check on batch inner transactions
    if (ctx.tx.isFlag(tfInnerBatchTxn) && ctx.rules.enabled(featureBatch))
        return tesSUCCESS;
    // Do not add any checks after this point that are relevant for
    // batch inner transactions. They will be skipped.

    auto const sigValid = checkValidity(ctx.registry.get().getHashRouter(), ctx.tx, ctx.rules);
    if (sigValid.first == Validity::SigBad)
    {  // LCOV_EXCL_START
        JLOG(ctx.j.debug()) << "preflight2: bad signature. " << sigValid.second;
        return temINVALID;
        // LCOV_EXCL_STOP
    }

    // Do not add any checks after this point that are relevant for
    // batch inner transactions. They will be skipped.

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

Transactor::Transactor(ApplyContext& ctx)
    : ctx_(ctx)
    , sink_(ctx.journal, toShortString(ctx.tx.getTransactionID()) + " ")
    , j_(sink_)
    , account_(ctx.tx.getAccountID(sfAccount))
{
}

bool
Transactor::validDataLength(std::optional<Slice> const& slice, std::size_t maxLength)
{
    if (!slice)
        return true;
    return !slice->empty() && slice->length() <= maxLength;
}

std::uint32_t
Transactor::getFlagsMask(PreflightContext const& ctx)
{
    return tfUniversalMask;
}

NotTEC
Transactor::preflightSigValidated(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

/** Verify that the optional `sfDelegate` field authorises this transaction.
 *
 *  If `sfDelegate` is absent the transaction is self-signed and no
 *  delegation check is needed.  If present, a `DelegateObject` at
 *  `keylet::delegate(account, delegate)` must exist and must grant the
 *  delegate permission to submit this specific transaction type via
 *  `checkTxPermission`.
 *
 *  @param view Read-only ledger view used to look up the delegate object.
 *  @param tx   The transaction carrying the optional `sfDelegate` field.
 *  @return `tesSUCCESS` if delegation is not in use or the delegate is
 *      authorised; `terNO_DELEGATE_PERMISSION` if the delegate object is
 *      absent or the permission is not granted.
 */
NotTEC
Transactor::checkPermission(ReadView const& view, STTx const& tx)
{
    auto const delegate = tx[~sfDelegate];
    if (!delegate)
        return tesSUCCESS;

    auto const delegateKey = keylet::delegate(tx[sfAccount], *delegate);
    auto const sle = view.read(delegateKey);

    if (!sle)
        return terNO_DELEGATE_PERMISSION;

    return checkTxPermission(sle, tx);
}

/** Compute the base fee for a transaction before load scaling.
 *
 *  The fee is `baseFee * (1 + signerCount)`: the network's base fee unit
 *  plus one additional unit for each entry in `sfSigners` (multi-sign).
 *  Derived transactors may override this for types that have a different
 *  cost model (e.g., `AccountDelete` charges an owner-reserve fee).
 *
 *  @param view Read-only ledger view supplying the current `fees().base`.
 *  @param tx   The transaction whose `sfSigners` array is inspected.
 *  @return The unscaled base fee in drops.
 */
XRPAmount
Transactor::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    XRPAmount const baseFee = view.fees().base;

    std::size_t const signerCount =
        tx.isFieldPresent(sfSigners) ? tx.getFieldArray(sfSigners).size() : 0;

    return baseFee + (signerCount * baseFee);
}

/** Return the owner-reserve increment as a transaction fee (unscaled).
 *
 *  Used by transaction types whose cost is proportional to the reserve
 *  rather than the base fee (e.g., `AccountDelete`, `AMMCreate`,
 *  `LedgerStateFix`).  The current ledger's `fees().increment` is returned
 *  directly; load scaling is applied separately by `minimumFee`.
 *
 *  @param view Read-only ledger view supplying `fees().increment`.
 *  @param tx   The transaction (unused; present for the static interface
 *      convention shared with `calculateBaseFee`).
 *  @return The owner-reserve increment in drops.
 *  @note An assertion fires if `increment ≤ base * 100`, because the
 *      reserve-as-fee model breaks down when the reserve is not
 *      substantially larger than the base fee.
 */
XRPAmount
Transactor::calculateOwnerReserveFee(ReadView const& view, STTx const& tx)
{
    // The reserve must be significantly larger than the base fee; if it ever
    // is not, charging an owner reserve as a tx fee needs to be reconsidered.
    XRPL_ASSERT(
        view.fees().increment > view.fees().base * 100,
        "xrpl::Transactor::calculateOwnerReserveFee : Owner reserve is "
        "reasonable");
    return view.fees().increment;
}

XRPAmount
Transactor::minimumFee(
    ServiceRegistry& registry,
    XRPAmount baseFee,
    Fees const& fees,
    ApplyFlags flags)
{
    return scaleFeeLoad(baseFee, registry.getFeeTrack(), fees, (flags & TapUnlimited) != 0u);
}

/** Validate the transaction fee and the fee-payer's ability to cover it.
 *
 *  For open-ledger transactions, confirms the fee meets the load-adjusted
 *  minimum (`minimumFee`).  For all transactions, confirms the fee-payer's
 *  account balance is sufficient.
 *
 *  Batch inner transactions (`TapBatch`) must carry a zero fee; any
 *  non-zero fee is rejected with `temBAD_FEE`.
 *
 *  @param ctx     Preclaim context with read-only ledger view and flags.
 *  @param baseFee The unscaled base fee returned by `calculateBaseFee`.
 *  @return `tesSUCCESS` if the fee is valid and the account can cover it;
 *      a `tel*`, `tec*`, or `ter*` code otherwise.
 *  @note Because preclaim evaluates against a static `ReadView`, it does not
 *      reflect fee deductions from other in-flight transactions from the same
 *      account within the current ledger.  The balance check here may
 *      therefore pass optimistically; the `Transactor::reset` mechanism
 *      corrects any shortfall by clamping the actual fee to the remaining
 *      balance when the transaction is applied.
 */
TER
Transactor::checkFee(PreclaimContext const& ctx, XRPAmount baseFee)
{
    if (!ctx.tx[sfFee].native())
        return temBAD_FEE;

    auto const feePaid = ctx.tx[sfFee].xrp();

    if ((ctx.flags & TapBatch) != 0u)
    {
        if (feePaid == beast::kZERO)
            return tesSUCCESS;

        JLOG(ctx.j.trace()) << "Batch: Fee must be zero.";
        return temBAD_FEE;  // LCOV_EXCL_LINE
    }

    if (!isLegalAmount(feePaid) || feePaid < beast::kZERO)
        return temBAD_FEE;

    // Only check fee is sufficient when the ledger is open.
    if (ctx.view.open())
    {
        auto const feeDue = minimumFee(ctx.registry, baseFee, ctx.view.fees(), ctx.flags);

        if (feePaid < feeDue)
        {
            JLOG(ctx.j.trace()) << "Insufficient fee paid: " << to_string(feePaid) << "/"
                                << to_string(feeDue);
            return telINSUF_FEE_P;
        }
    }

    if (feePaid == beast::kZERO)
        return tesSUCCESS;

    auto const id = ctx.tx.getFeePayer();
    auto const sle = ctx.view.read(keylet::account(id));
    if (!sle)
        return terNO_ACCOUNT;

    auto const balance = (*sle)[sfBalance].xrp();

    if (balance < feePaid)
    {
        JLOG(ctx.j.trace()) << "Insufficient balance:" << " balance=" << to_string(balance)
                            << " paid=" << to_string(feePaid);

        if ((balance > beast::kZERO) && !ctx.view.open())
        {
            // Closed ledger, non-zero balance, less than fee
            return tecINSUFF_FEE;
        }

        return terINSUF_FEE_B;
    }

    return tesSUCCESS;
}

/** Deduct the transaction fee from the fee-payer's account balance.
 *
 *  Decrements `sfBalance` on the fee-payer's `AccountRoot` in the mutable
 *  view.  If the fee-payer is the transaction account (`account_`), the SLE
 *  update is deferred to `apply()` to avoid a redundant write; otherwise
 *  `view().update` is called immediately.
 *
 *  @return `tesSUCCESS` on success; `tefINTERNAL` (unreachable in practice)
 *      if the fee-payer account root is missing from the ledger.
 */
TER
Transactor::payFee()
{
    auto const feePaid = ctx_.tx[sfFee].xrp();

    auto const feePayer = ctx_.tx.getFeePayer();
    auto const sle = view().peek(keylet::account(feePayer));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Deduct the fee so it is unavailable during the transaction body.
    // The account SLE is written back in apply() when feePayer == account_.
    sle->setFieldAmount(sfBalance, sle->getFieldAmount(sfBalance) - feePaid);
    if (feePayer != account_)
        view().update(sle);

    // VFALCO Should we call view().rawDestroyXRP() here as well?
    return tesSUCCESS;
}

/** Verify that the transaction's sequence or ticket is valid for the account.
 *
 *  For sequence-based transactions, enforces strict monotonic ordering:
 *  the transaction sequence must equal the account's current sequence.
 *  Future sequences return `terPRE_SEQ` (retryable); past sequences
 *  return `tefPAST_SEQ` (permanently invalid).
 *
 *  For ticket-based transactions, the ticket's numeric value must be
 *  strictly less than the account's current sequence (to rule out tickets
 *  that have not yet been created), and the ticket SLE must actually exist.
 *
 *  @param view Read-only ledger view for account and ticket lookups.
 *  @param tx   The transaction carrying `sfSequence` or `sfTicketSequence`.
 *  @param j    Journal for diagnostic logging.
 *  @return `tesSUCCESS` if the sequence or ticket is valid;
 *      `terNO_ACCOUNT` if the source account does not exist;
 *      `terPRE_SEQ` / `terPRE_TICKET` if retryable;
 *      `tefPAST_SEQ` / `tefNO_TICKET` / `temSEQ_AND_TICKET` otherwise.
 */
NotTEC
Transactor::checkSeqProxy(ReadView const& view, STTx const& tx, beast::Journal j)
{
    auto const id = tx.getAccountID(sfAccount);

    auto const sle = view.read(keylet::account(id));

    if (!sle)
    {
        JLOG(j.trace()) << "applyTransaction: delay: source account does not exist "
                        << toBase58(id);
        return terNO_ACCOUNT;
    }

    SeqProxy const tSeqProx = tx.getSeqProxy();
    SeqProxy const aSeq = SeqProxy::sequence((*sle)[sfSequence]);

    if (tSeqProx.isSeq())
    {
        if (tx.isFieldPresent(sfTicketSequence))
        {
            JLOG(j.trace()) << "applyTransaction: has both a TicketSequence "
                               "and a non-zero Sequence number";
            return temSEQ_AND_TICKET;
        }
        if (tSeqProx != aSeq)
        {
            if (aSeq < tSeqProx)
            {
                JLOG(j.trace()) << "applyTransaction: has future sequence number "
                                << "a_seq=" << aSeq << " t_seq=" << tSeqProx;
                return terPRE_SEQ;
            }
            // It's an already-used sequence number.
            JLOG(j.trace()) << "applyTransaction: has past sequence number "
                            << "a_seq=" << aSeq << " t_seq=" << tSeqProx;
            return tefPAST_SEQ;
        }
    }
    else if (tSeqProx.isTicket())
    {
        // Bypass the type comparison. Apples and oranges.
        if (aSeq.value() <= tSeqProx.value())
        {
            // If the Ticket number is greater than or equal to the
            // account sequence there's the possibility that the
            // transaction to create the Ticket has not hit the ledger
            // yet.  Allow a retry.
            JLOG(j.trace()) << "applyTransaction: has future ticket id "
                            << "a_seq=" << aSeq << " t_seq=" << tSeqProx;
            return terPRE_TICKET;
        }

        // Transaction can never succeed if the Ticket is not in the ledger.
        if (!view.exists(keylet::kTICKET(id, tSeqProx)))
        {
            JLOG(j.trace()) << "applyTransaction: ticket already used or never created "
                            << "a_seq=" << aSeq << " t_seq=" << tSeqProx;
            return tefNO_TICKET;
        }
    }

    return tesSUCCESS;
}

/** Verify ordering constraints: `sfAccountTxnID`, `sfLastLedgerSequence`,
 *  and duplicate-transaction detection.
 *
 *  Checks three conditions in order:
 *  1. If `sfAccountTxnID` is present, the account's recorded last-tx hash
 *     must match (enforcing a strict prior-transaction chain).
 *  2. If `sfLastLedgerSequence` is present, the current ledger index must
 *     not exceed it (expiry check).
 *  3. The transaction hash must not already exist in the current ledger
 *     (replay / duplicate prevention).
 *
 *  @param ctx Preclaim context with read-only ledger view and transaction.
 *  @return `tesSUCCESS` if all ordering checks pass;
 *      `terNO_ACCOUNT` if the source account is missing;
 *      `tefWRONG_PRIOR` / `tefMAX_LEDGER` / `tefALREADY` otherwise.
 */
NotTEC
Transactor::checkPriorTxAndLastLedger(PreclaimContext const& ctx)
{
    auto const id = ctx.tx.getAccountID(sfAccount);

    auto const sle = ctx.view.read(keylet::account(id));

    if (!sle)
    {
        JLOG(ctx.j.trace()) << "applyTransaction: delay: source account does not exist "
                            << toBase58(id);
        return terNO_ACCOUNT;
    }

    if (ctx.tx.isFieldPresent(sfAccountTxnID) &&
        (sle->getFieldH256(sfAccountTxnID) != ctx.tx.getFieldH256(sfAccountTxnID)))
        return tefWRONG_PRIOR;

    if (ctx.tx.isFieldPresent(sfLastLedgerSequence) &&
        (ctx.view.seq() > ctx.tx.getFieldU32(sfLastLedgerSequence)))
        return tefMAX_LEDGER;

    if (ctx.view.txExists(ctx.tx.getTransactionID()))
        return tefALREADY;

    return tesSUCCESS;
}

/** Advance the account sequence or consume the referenced ticket.
 *
 *  For sequence-based transactions, increments `sfSequence` to `seqProxy + 1`.
 *  For ticket-based transactions, calls `ticketDelete` to erase the ticket SLE,
 *  remove it from the owner directory, and adjust the owner count.
 *
 *  @param sleAccount Mutable account root SLE; must not be null.
 *  @return `tesSUCCESS` on success; a `tef*` error if ticket deletion fails
 *      (which indicates ledger corruption — see `ticketDelete`).
 *  @note For `TicketCreate` transactions, `sfSequence` will be advanced a
 *      second time by the transactor body to allocate the ticket IDs.
 */
TER
Transactor::consumeSeqProxy(SLE::pointer const& sleAccount)
{
    XRPL_ASSERT(sleAccount, "xrpl::Transactor::consumeSeqProxy : non-null account");
    SeqProxy const seqProx = ctx_.tx.getSeqProxy();
    if (seqProx.isSeq())
    {
        // TicketCreate will advance sfSequence again during doApply to
        // allocate the requested ticket IDs.
        sleAccount->setFieldU32(sfSequence, seqProx.value() + 1);
        return tesSUCCESS;
    }
    return ticketDelete(view(), account_, getTicketIndex(account_, seqProx), j_);
}

/** Remove a single Ticket from the ledger and release its owner reserve.
 *
 *  Performs four coordinated mutations:
 *  1. Removes the ticket SLE from the ledger.
 *  2. Removes the ticket from the account's owner directory.
 *  3. Decrements `sfTicketCount` on the account root, removing the field
 *     entirely when it reaches zero.
 *  4. Calls `adjustOwnerCount` to release the ticket's reserve unit.
 *
 *  @param view         Mutable ledger view to apply changes against.
 *  @param account      The account that owns the ticket.
 *  @param ticketIndex  The ledger index of the ticket SLE to delete.
 *  @param j            Journal for diagnostic logging.
 *  @return `tesSUCCESS` on success; `tefBAD_LEDGER` if any expected SLE is
 *      missing (indicates ledger corruption; branches are `LCOV_EXCL`).
 */
TER
Transactor::ticketDelete(
    ApplyView& view,
    AccountID const& account,
    uint256 const& ticketIndex,
    beast::Journal j)
{
    SLE::pointer const sleTicket = view.peek(keylet::kTICKET(ticketIndex));
    if (!sleTicket)
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Ticket disappeared from ledger.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    std::uint64_t const page{(*sleTicket)[sfOwnerNode]};
    if (!view.dirRemove(keylet::ownerDir(account), page, ticketIndex, true))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete Ticket from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto sleAccount = view.peek(keylet::account(account));
    if (!sleAccount)
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Could not find Ticket owner account root.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    if (auto ticketCount = (*sleAccount)[~sfTicketCount])
    {
        if (*ticketCount == 1)
        {
            sleAccount->makeFieldAbsent(sfTicketCount);
        }
        else
        {
            ticketCount = *ticketCount - 1;
        }
    }
    else
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "TicketCount field missing from account root.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    adjustOwnerCount(view, sleAccount, -1, j);

    view.erase(sleTicket);
    return tesSUCCESS;
}

/** Perform inexpensive pre-apply setup before the ledger is mutated.
 *
 *  The base implementation just asserts that `account_` is non-zero.
 *  Derived transactors override this to cache fields or compute derived
 *  values they will need in `doApply`.
 */
void
Transactor::preCompute()
{
    XRPL_ASSERT(account_ != beast::kZERO, "xrpl::Transactor::preCompute : nonzero account");
}

/** Orchestrate the mutable phase of transaction processing.
 *
 *  Calls `preCompute()`, snapshots `preFeeBalance_` from the account's
 *  current balance, then in order: `consumeSeqProxy`, `payFee`, updates
 *  `sfAccountTxnID` if present, and finally calls `doApply`.
 *
 *  @return The `TER` from `doApply`, or an earlier error from
 *      `consumeSeqProxy` / `payFee` if those fail.
 *  @note Reserve checks inside `doApply` should compare against
 *      `preFeeBalance_` (pre-fee snapshot), not the post-fee balance,
 *      to correctly allow an account to dip into its reserve to pay the fee.
 */
TER
Transactor::apply()
{
    preCompute();

    auto const sle = view().peek(keylet::account(account_));

    // sle must exist except for transactions that allow zero account.
    XRPL_ASSERT(
        sle != nullptr || account_ == beast::kZERO,
        "xrpl::Transactor::apply : non-null SLE or zero account");

    if (sle)
    {
        preFeeBalance_ = STAmount{(*sle)[sfBalance]}.xrp();

        TER result = consumeSeqProxy(sle);
        if (!isTesSuccess(result))
            return result;

        result = payFee();
        if (!isTesSuccess(result))
            return result;

        if (sle->isFieldPresent(sfAccountTxnID))
            sle->setFieldH256(sfAccountTxnID, ctx_.tx.getTransactionID());

        view().update(sle);
    }

    return doApply();
}

/** Verify the signing authority for a single transaction or signer object.
 *
 *  Dispatches to one of four paths:
 *  1. **Pseudo-account guard** — rejects with `tefBAD_AUTH` when
 *     `featureLendingProtocol` is active and `idAccount` is a pseudo-account.
 *  2. **Batch inner** — if `parentBatchId` is set and `featureBatch` is
 *     active, asserts that no key/signature/signer-list is present (the
 *     outer batch already authorised the inner transactions) and returns.
 *  3. **Dry-run** — skips validation entirely when `TapDryRun` is set and
 *     no signing key or signer list is present.
 *  4. **Multi-sign** — if `sfSigners` is present, delegates to
 *     `checkMultiSign`.
 *  5. **Single-sign** — derives the signer account from the public key and
 *     delegates to `checkSingleSign`.
 *
 *  @param view          Read-only ledger view for account lookups.
 *  @param flags         Apply flags (checked for `TapDryRun`).
 *  @param parentBatchId Set when this is an inner batch transaction.
 *  @param idAccount     The account whose signing authority is being checked
 *      (may be the delegate account when `sfDelegate` is present).
 *  @param sigObject     The STObject carrying signing fields (`sfSigningPubKey`,
 *      `sfTxnSignature`, `sfSigners`).
 *  @param j             Journal for diagnostic logging.
 *  @return `tesSUCCESS` if the signing authority is valid;
 *      `tefBAD_AUTH`, `tefMASTER_DISABLED`, `tefNOT_MULTI_SIGNING`,
 *      `tefBAD_QUORUM`, `temINVALID_FLAG`, or `terNO_ACCOUNT` otherwise.
 */
NotTEC
Transactor::checkSign(
    ReadView const& view,
    ApplyFlags flags,
    std::optional<uint256 const> const& parentBatchId,
    AccountID const& idAccount,
    STObject const& sigObject,
    beast::Journal const j)
{
    {
        auto const sle = view.read(keylet::account(idAccount));

        if (view.rules().enabled(featureLendingProtocol) && isPseudoAccount(sle))
        {
            // Pseudo-accounts can't sign transactions. This check is gated on
            // the Lending Protocol amendment because that's the project it was
            // added under, and it doesn't justify another amendment
            return tefBAD_AUTH;
        }
    }

    auto const pkSigner = sigObject.getFieldVL(sfSigningPubKey);
    if (parentBatchId && view.rules().enabled(featureBatch))
    {
        // Defensive: also checked in Batch::preflight, but guard here too.
        if (sigObject.isFieldPresent(sfTxnSignature) || !pkSigner.empty() ||
            sigObject.isFieldPresent(sfSigners))
        {
            return temINVALID_FLAG;  // LCOV_EXCL_LINE
        }
        return tesSUCCESS;
    }

    if (((flags & TapDryRun) != 0u) && pkSigner.empty() && !sigObject.isFieldPresent(sfSigners))
    {
        return tesSUCCESS;
    }

    if (sigObject.isFieldPresent(sfSigners))
    {
        return checkMultiSign(view, flags, idAccount, sigObject, j);
    }

    XRPL_ASSERT(!pkSigner.empty(), "xrpl::Transactor::checkSign : non-empty signer");

    if (!publicKeyType(makeSlice(pkSigner)))
    {
        JLOG(j.trace()) << "checkSign: signing public key type is unknown";
        return tefBAD_AUTH;  // FIXME: should be better error!
    }

    // Look up the account.
    auto const idSigner = calcAccountID(PublicKey(makeSlice(pkSigner)));
    auto const sleAccount = view.read(keylet::account(idAccount));
    if (!sleAccount)
        return terNO_ACCOUNT;

    return checkSingleSign(view, idSigner, idAccount, sleAccount, j);
}

/** Convenience overload that extracts signing context from a `PreclaimContext`.
 *
 *  When `sfDelegate` is present, the signing identity is the delegate account;
 *  otherwise it is `sfAccount`.  Forwards to the full `checkSign` overload.
 *
 *  @param ctx Preclaim context carrying the view, flags, and transaction.
 *  @return The result of the underlying `checkSign` call.
 */
NotTEC
Transactor::checkSign(PreclaimContext const& ctx)
{
    auto const idAccount = ctx.tx.isFieldPresent(sfDelegate) ? ctx.tx.getAccountID(sfDelegate)
                                                             : ctx.tx.getAccountID(sfAccount);
    return checkSign(ctx.view, ctx.flags, ctx.parentBatchId, idAccount, ctx.tx, ctx.j);
}

/** Verify the `sfBatchSigners` array on the outer Batch transaction.
 *
 *  Iterates each entry in `sfBatchSigners`.  An entry with an empty
 *  `sfSigningPubKey` is validated as multi-sign; an entry with a non-empty
 *  key is validated as single-sign.  A special case allows a signer whose
 *  account does not yet exist in the ledger, provided the signing key
 *  derives to that account (master key) — this permits a Batch to fund an
 *  account creation as part of the same bundle.
 *
 *  @param ctx Preclaim context with read-only view, flags, and transaction.
 *  @return `tesSUCCESS` if every batch signer is valid;
 *      `tefBAD_AUTH`, `tefMASTER_DISABLED`, `tefNOT_MULTI_SIGNING`, or
 *      `tefBAD_QUORUM` if any signer fails validation.
 */
NotTEC
Transactor::checkBatchSign(PreclaimContext const& ctx)
{
    NotTEC ret = tesSUCCESS;
    STArray const& signers{ctx.tx.getFieldArray(sfBatchSigners)};
    for (auto const& signer : signers)
    {
        auto const idAccount = signer.getAccountID(sfAccount);

        Blob const& pkSigner = signer.getFieldVL(sfSigningPubKey);
        if (pkSigner.empty())
        {
            if (ret = checkMultiSign(ctx.view, ctx.flags, idAccount, signer, ctx.j);
                !isTesSuccess(ret))
                return ret;
        }
        else
        {
            // LCOV_EXCL_START
            if (!publicKeyType(makeSlice(pkSigner)))
                return tefBAD_AUTH;
            // LCOV_EXCL_STOP

            auto const idSigner = calcAccountID(PublicKey(makeSlice(pkSigner)));
            auto const sleAccount = ctx.view.read(keylet::account(idAccount));

            // A batch can include transactions from an un-created account ONLY
            // when the account master key is the signer
            if (!sleAccount)
            {
                if (idAccount != idSigner)
                    return tefBAD_AUTH;

                return tesSUCCESS;
            }

            if (ret = checkSingleSign(ctx.view, idSigner, idAccount, sleAccount, ctx.j);
                !isTesSuccess(ret))
                return ret;
        }
    }
    return ret;
}

/** Verify that a single-signature key is authorised for `idAccount`.
 *
 *  Applies three precedence rules in order:
 *  1. Regular key: if `sfRegularKey` is set and equals `idSigner`, accept.
 *  2. Enabled master key: if master is not disabled and `idAccount == idSigner`, accept.
 *  3. Disabled master key: if master is disabled and `idAccount == idSigner`, reject with
 *     `tefMASTER_DISABLED`.
 *  Any other key returns `tefBAD_AUTH`.
 *
 *  @param view       Read-only ledger view (unused after caller reads `sleAccount`).
 *  @param idSigner   AccountID derived from the transaction's signing public key.
 *  @param idAccount  The account whose authority is being checked.
 *  @param sleAccount The account root SLE for `idAccount`.
 *  @param j          Journal for diagnostic logging.
 *  @return `tesSUCCESS`, `tefMASTER_DISABLED`, or `tefBAD_AUTH`.
 */
NotTEC
Transactor::checkSingleSign(
    ReadView const& view,
    AccountID const& idSigner,
    AccountID const& idAccount,
    std::shared_ptr<SLE const> sleAccount,
    beast::Journal const j)
{
    bool const isMasterDisabled = sleAccount->isFlag(lsfDisableMaster);

    if ((*sleAccount)[~sfRegularKey] == idSigner)
        return tesSUCCESS;

    if (!isMasterDisabled && idAccount == idSigner)
        return tesSUCCESS;

    if (isMasterDisabled && idAccount == idSigner)
        return tefMASTER_DISABLED;

    return tefBAD_AUTH;
}

/** Verify a multi-signature against the account's registered signer list.
 *
 *  Performs a linear merge of the sorted `sfSigners` array from `sigObject`
 *  against the account's sorted `SignerEntry` list, validating each signer
 *  under three rules (established January 2015):
 *  - **Phantom account**: `idSigner == txSignerAcctID` and no account root
 *    in the ledger — always accepted.
 *  - **Master key**: `idSigner == txSignerAcctID` and account root present
 *    — accepted unless `lsfDisableMaster` is set.
 *  - **Regular key**: `idSigner != txSignerAcctID` and account root present
 *    — accepted only if `idSigner == sfRegularKey` on the account root.
 *
 *  Accumulates weights; fails with `tefBAD_QUORUM` if the total falls below
 *  `sfSignerQuorum`.  All signers must be valid — the first invalid entry
 *  short-circuits with a `tef*` error.
 *
 *  @param view      Read-only ledger view for account and signer-list lookups.
 *  @param flags     Apply flags (checked for `TapDryRun` simulation mode).
 *  @param id        The account whose `SignerList` is consulted.
 *  @param sigObject The STObject carrying the `sfSigners` array.
 *  @param j         Journal for diagnostic logging.
 *  @return `tesSUCCESS` if quorum is met and all signers are valid;
 *      `tefNOT_MULTI_SIGNING` if no signer list exists;
 *      `tefBAD_SIGNATURE`, `tefMASTER_DISABLED`, or `tefBAD_QUORUM` otherwise.
 */
NotTEC
Transactor::checkMultiSign(
    ReadView const& view,
    ApplyFlags flags,
    AccountID const& id,
    STObject const& sigObject,
    beast::Journal const j)
{
    std::shared_ptr<STLedgerEntry const> const sleAccountSigners = view.read(keylet::signers(id));
    if (!sleAccountSigners)
    {
        JLOG(j.trace()) << "applyTransaction: Invalid: Not a multi-signing account.";
        return tefNOT_MULTI_SIGNING;
    }

    // SignerListID == 0 is reserved for future multi-list support.
    XRPL_ASSERT(
        sleAccountSigners->isFieldPresent(sfSignerListID),
        "xrpl::Transactor::checkMultiSign : has signer list ID");
    XRPL_ASSERT(
        sleAccountSigners->getFieldU32(sfSignerListID) == 0,
        "xrpl::Transactor::checkMultiSign : signer list ID is 0");

    auto accountSigners = SignerEntries::deserialize(*sleAccountSigners, j, "ledger");
    if (!accountSigners)
        return accountSigners.error();

    STArray const& txSigners(sigObject.getFieldArray(sfSigners));

    std::uint32_t weightSum = 0;
    auto iter = accountSigners->begin();
    for (auto const& txSigner : txSigners)
    {
        AccountID const txSignerAcctID = txSigner.getAccountID(sfAccount);

        while (iter->account < txSignerAcctID)
        {
            if (++iter == accountSigners->end())
            {
                JLOG(j.trace()) << "applyTransaction: Invalid SigningAccount.Account.";
                return tefBAD_SIGNATURE;
            }
        }
        if (iter->account != txSignerAcctID)
        {
            JLOG(j.trace()) << "applyTransaction: Invalid SigningAccount.Account.";
            return tefBAD_SIGNATURE;
        }

        auto const spk = txSigner.getFieldVL(sfSigningPubKey);

        // spk non-empty in non-simulate mode is enforced by STTx::checkMultiSign.
        if (!spk.empty() && !publicKeyType(makeSlice(spk)))
        {
            JLOG(j.trace()) << "checkMultiSign: signing public key type is unknown";
            return tefBAD_SIGNATURE;
        }

        XRPL_ASSERT(
            (flags & TapDryRun) || !spk.empty(),
            "xrpl::Transactor::checkMultiSign : non-empty signer or "
            "simulation");
        AccountID const signingAcctIDFromPubKey =
            spk.empty() ? txSignerAcctID : calcAccountID(PublicKey(makeSlice(spk)));

        auto const sleTxSignerRoot = view.read(keylet::account(txSignerAcctID));

        if (signingAcctIDFromPubKey == txSignerAcctID)
        {
            // Phantom (no account root) or Master Key.  Phantoms pass automatically.
            if (sleTxSignerRoot)
            {
                std::uint32_t const signerAccountFlags = sleTxSignerRoot->getFieldU32(sfFlags);

                if ((signerAccountFlags & lsfDisableMaster) != 0u)
                {
                    JLOG(j.trace()) << "applyTransaction: Signer:Account lsfDisableMaster.";
                    return tefMASTER_DISABLED;
                }
            }
        }
        else
        {
            // Regular Key: public key must hash to the account's sfRegularKey.
            if (!sleTxSignerRoot)
            {
                JLOG(j.trace()) << "applyTransaction: Non-phantom signer "
                                   "lacks account root.";
                return tefBAD_SIGNATURE;
            }

            if (!sleTxSignerRoot->isFieldPresent(sfRegularKey))
            {
                JLOG(j.trace()) << "applyTransaction: Account lacks RegularKey.";
                return tefBAD_SIGNATURE;
            }
            if (signingAcctIDFromPubKey != sleTxSignerRoot->getAccountID(sfRegularKey))
            {
                JLOG(j.trace()) << "applyTransaction: Account doesn't match RegularKey.";
                return tefBAD_SIGNATURE;
            }
        }
        weightSum += iter->weight;
    }

    if (weightSum < sleAccountSigners->getFieldU32(sfSignerQuorum))
    {
        JLOG(j.trace()) << "applyTransaction: Signers failed to meet quorum.";
        return tefBAD_QUORUM;
    }

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

/** Delete up to `kUNFUNDED_OFFER_REMOVE_LIMIT` unfunded offers from the ledger.
 *
 *  Called after a `tecOVERSIZE` or `tecKILLED` failure to clean up offers
 *  that were identified as unfunded during the failed transaction's execution.
 *  Offer deletion is capped to avoid unbounded ledger growth from a single
 *  failed transaction.
 *
 *  @param view   Mutable ledger view to apply deletions against.
 *  @param offers Indices of offers to attempt to delete.
 *  @param viewJ  Journal for logging inside `offerDelete`.
 */
static void
removeUnfundedOffers(ApplyView& view, std::vector<uint256> const& offers, beast::Journal viewJ)
{
    int removed = 0;

    for (auto const& index : offers)
    {
        if (auto const sleOffer = view.peek(keylet::offer(index)))
        {
            // offer is unfunded
            offerDelete(view, sleOffer, viewJ);
            if (++removed == kUNFUNDED_OFFER_REMOVE_LIMIT)
                return;
        }
    }
}

/** Delete up to `kEXPIRED_OFFER_REMOVE_LIMIT` expired NFToken offers.
 *
 *  Called after a `tecEXPIRED` failure to remove NFToken offers that were
 *  found to have expired during the failed transaction's execution.
 *
 *  @param view   Mutable ledger view to apply deletions against.
 *  @param offers Indices of NFToken offer SLEs to attempt to delete.
 *  @param viewJ  Journal for logging inside `nft::deleteTokenOffer`.
 */
static void
removeExpiredNFTokenOffers(
    ApplyView& view,
    std::vector<uint256> const& offers,
    beast::Journal viewJ)
{
    std::size_t removed = 0;

    for (auto const& index : offers)
    {
        if (auto const offer = view.peek(keylet::nftoffer(index)))
        {
            nft::deleteTokenOffer(view, offer);
            if (++removed == kEXPIRED_OFFER_REMOVE_LIMIT)
                return;
        }
    }
}

/** Delete all expired credential SLEs collected during a failed transaction.
 *
 *  Called after a `tecEXPIRED` failure.  Unlike the offer-removal helpers
 *  there is no cap on the number of credentials removed; any deletion
 *  failure is logged but does not abort the loop.
 *
 *  @param view  Mutable ledger view to apply deletions against.
 *  @param creds Indices of credential SLEs to attempt to delete.
 *  @param viewJ Journal for error logging on deletion failures.
 */
static void
removeExpiredCredentials(ApplyView& view, std::vector<uint256> const& creds, beast::Journal viewJ)
{
    for (auto const& index : creds)
    {
        if (auto const sle = view.peek(keylet::credential(index)))
        {
            if (auto const ter = credentials::deleteSLE(view, sle, viewJ); !isTesSuccess(ter))
            {
                JLOG(viewJ.error())
                    << "removeExpiredCredentials: failed to delete expired credential. Err: "
                    << transToken(ter);
            }
        }
    }
}

/** Delete obsolete AMM trust lines collected during a `tecINCOMPLETE` failure.
 *
 *  Called to clean up `ltRIPPLE_STATE` entries that were deleted during the
 *  failed transaction's execution but must still be removed from the ledger.
 *  Aborts without deletion if the count exceeds `kMAX_DELETABLE_AMM_TRUST_LINES`.
 *
 *  @param view       Mutable ledger view to apply deletions against.
 *  @param trustLines Indices of trust-line SLEs to attempt to delete.
 *  @param viewJ      Journal for error logging on deletion failures.
 */
static void
removeDeletedTrustLines(
    ApplyView& view,
    std::vector<uint256> const& trustLines,
    beast::Journal viewJ)
{
    if (trustLines.size() > kMAX_DELETABLE_AMM_TRUST_LINES)
    {
        JLOG(viewJ.error()) << "removeDeletedTrustLines: deleted trustlines exceed max "
                            << trustLines.size();
        return;
    }

    for (auto const& index : trustLines)
    {
        if (auto const sleState = view.peek({ltRIPPLE_STATE, index});
            !isTesSuccess(deleteAMMTrustLine(view, sleState, std::nullopt, viewJ)))
        {
            JLOG(viewJ.error()) << "removeDeletedTrustLines: failed to delete AMM trustline";
        }
    }
}

/** Delete obsolete AMM MPToken holders collected during a `tecINCOMPLETE` failure.
 *
 *  Called alongside `removeDeletedTrustLines` to clean up `ltMPTOKEN` entries
 *  for each side of an AMM pool.  At most two MPTs can be present (one per
 *  pool asset); a count exceeding two indicates an unexpected state and
 *  is logged as an error without applying any deletions.
 *
 *  @param view  Mutable ledger view to apply deletions against.
 *  @param mpts  Indices of MPToken SLEs to attempt to delete.
 *  @param viewJ Journal for error logging on deletion failures.
 */
static void
removeDeletedMPTs(ApplyView& view, std::vector<uint256> const& mpts, beast::Journal viewJ)
{
    // At most two MPTs — one per side of the AMM pool.
    if (mpts.size() > 2)
    {
        JLOG(viewJ.error()) << "removeDeletedMPTs: deleted mpts exceed 2 " << mpts.size();
        return;
    }

    for (auto const& index : mpts)
    {
        if (auto const sleState = view.peek({ltMPTOKEN, index}); sleState &&
            deleteAMMMPToken(view, sleState, (*sleState)[sfIssuer], viewJ) != tesSUCCESS)
        {
            JLOG(viewJ.error()) << "removeDeletedMPTs: failed to delete AMM MPT";
        }
    }
}

/** Discard all ledger mutations and re-apply only the fee and sequence.
 *
 *  Calls `ctx_.discard()` to roll back every change made during `doApply`,
 *  then re-deducts the fee from the fee-payer and re-consumes the sequence
 *  or ticket.  If the fee exceeds the payer's current balance (possible
 *  when multiple in-flight transactions over-commit the same account), the
 *  fee is clamped to the remaining balance.
 *
 *  This is the mechanism that ensures a failing `tec*` transaction always
 *  charges its fee even when the account's balance was over-committed by
 *  other transactions evaluated against the same static `ReadView`.
 *
 *  @param fee The fee that should be charged; may be clamped downward.
 *  @return A pair `{TER, actualFee}`.  `TER` is `tesSUCCESS` on success or
 *      `tefINTERNAL` if the account or fee-payer SLE is unexpectedly absent.
 *      `actualFee` is the fee after any clamping.
 *  @note The fee is clamped only downward; it is never raised above what the
 *      transaction declared in `sfFee`.
 */
std::pair<TER, XRPAmount>
Transactor::reset(XRPAmount fee)
{
    ctx_.discard();

    auto const txnAcct = view().peek(keylet::account(ctx_.tx.getAccountID(sfAccount)));

    if (!txnAcct)
        return {tefINTERNAL, beast::kZERO};

    auto const payerSle = view().peek(keylet::account(ctx_.tx.getFeePayer()));
    if (!payerSle)
        return {tefINTERNAL, beast::kZERO};  // LCOV_EXCL_LINE

    auto const balance = payerSle->getFieldAmount(sfBalance).xrp();

    // balance should have already been checked in checkFee / preflight.
    XRPL_ASSERT(
        balance != beast::kZERO && (!view().open() || balance >= fee),
        "xrpl::Transactor::reset : valid balance");

    if (fee > balance)
        fee = balance;

    // Re-deduct the fee and re-consume the sequence/ticket after discard.
    // Failure here means ledger corruption — reject rather than making it worse.
    payerSle->setFieldAmount(sfBalance, balance - fee);
    TER const ter{consumeSeqProxy(txnAcct)};
    XRPL_ASSERT(isTesSuccess(ter), "xrpl::Transactor::reset : result is tesSUCCESS");

    if (isTesSuccess(ter))
    {
        view().update(txnAcct);
        if (payerSle != txnAcct)
            view().update(payerSle);
    }

    return {ter, fee};
}

/** Named breakpoint location for replaying specific transactions in a debugger.
 *
 *  Does nothing except log the hash at debug level.  To replay a specific
 *  transaction, set a breakpoint here and configure the node's trap-tx hash
 *  via the `ServiceRegistry`.
 *
 *  @param txHash The hash of the trapped transaction, for logging.
 */
void
Transactor::trapTransaction(uint256 txHash) const
{
    JLOG(j_.debug()) << "Transaction trapped: " << txHash;
}

/** Run transaction-specific invariants over every modified ledger entry.
 *
 *  Two-phase execution:
 *  1. Visits every SLE modified by this transaction via `visitInvariantEntry`.
 *  2. Calls `finalizeInvariants` on the derived transactor to evaluate
 *     accumulated state.
 *
 *  Any exception during either phase is caught and converted to
 *  `tecINVARIANT_FAILED` to maintain determinism across validators.
 *
 *  @param result Tentative TER from transaction processing.
 *  @param fee    Fee consumed by the transaction.
 *  @return The original `result` if all invariants pass;
 *      `tecINVARIANT_FAILED` otherwise.
 */
[[nodiscard]] TER
Transactor::checkTransactionInvariants(TER result, XRPAmount fee)
{
    try
    {
        ctx_.visit(
            [this](uint256 const&, bool isDelete, SLE::const_ref before, SLE::const_ref after) {
                this->visitInvariantEntry(isDelete, before, after);
            });

        if (!this->finalizeInvariants(ctx_.tx, result, fee, ctx_.view(), ctx_.journal))
        {
            JLOG(ctx_.journal.fatal()) <<                                             //
                "Transaction has failed one or more transaction invariants, tx: " <<  //
                to_string(ctx_.tx.getJson(JsonOptions::Values::None));
            return tecINVARIANT_FAILED;
        }
    }
    catch (std::exception const& ex)
    {
        JLOG(ctx_.journal.fatal()) <<                               //
            "Exception while checking transaction invariants: " <<  //
            ex.what() <<                                            //
            ", tx: " <<                                             //
            to_string(ctx_.tx.getJson(JsonOptions::Values::None));

        return tecINVARIANT_FAILED;
    }

    return result;
}

/** Run both transaction-specific and protocol-level invariants.
 *
 *  Calls `checkTransactionInvariants` first (more specific — if these fail,
 *  the transaction's core logic is wrong), then `ctx_.checkInvariants`
 *  (broader protocol properties that must hold for any transaction).
 *
 *  Both layers always run; neither short-circuits the other.  If both
 *  fail, `tefINVARIANT_FAILED` (the more severe code) is returned.
 *
 *  @param result Tentative TER from transaction processing.
 *  @param fee    Fee consumed by the transaction.
 *  @return The original `result` if all invariants pass;
 *      `tecINVARIANT_FAILED` if either layer fails;
 *      `tefINVARIANT_FAILED` if the protocol layer returns that code.
 */
[[nodiscard]] TER
Transactor::checkInvariants(TER result, XRPAmount fee)
{
    auto const txResult = checkTransactionInvariants(result, fee);
    auto const protoResult = ctx_.checkInvariants(result, fee);

    // tefINVARIANT_FAILED is more severe than tec; it prevents ledger inclusion.
    if (protoResult == tefINVARIANT_FAILED)
        return tefINVARIANT_FAILED;
    if (txResult == tecINVARIANT_FAILED || protoResult == tecINVARIANT_FAILED)
        return tecINVARIANT_FAILED;

    return result;
}

//------------------------------------------------------------------------------

/** Execute the full apply phase and return a result with optional metadata.
 *
 *  Entry point for phase 3 of the transaction pipeline.  Orchestrates:
 *  1. RAII guards for numeric arithmetic rules (`NumberSO`,
 *     `CurrentTransactionRulesGuard`).
 *  2. Debug-only serdes round-trip check to catch serialisation mismatches.
 *  3. Optional `trapTransaction` breakpoint for replay debugging.
 *  4. Calls `apply()` when `ctx_.preclaimResult` is `tesSUCCESS`.
 *  5. Enforces `tecOVERSIZE` when metadata exceeds `kOVERSIZE_META_DATA_CAP`.
 *  6. For `tapFAIL_HARD` + `tec*`: discards immediately, nothing is applied.
 *  7. For `tecOVERSIZE`, `tecKILLED`, `tecINCOMPLETE`, `tecEXPIRED`: visits
 *     the context diff to collect deleted objects, calls `reset()`, then runs
 *     the appropriate targeted cleanup helpers.
 *  8. Runs `checkInvariants`; on failure, resets to fee-only and re-checks
 *     protocol invariants.
 *  9. For `tapDRY_RUN`, forces `applied = false` unconditionally.
 *
 *  @return An `ApplyResult` containing the final `TER`, whether the
 *      transaction was applied, and optional `TxMeta`.
 *  @note `applied = true` means the context was committed via `ctx_.apply()`.
 *      Dry-run mode computes all mutations but never commits them.
 */
ApplyResult
Transactor::operator()()
{
    JLOG(j_.trace()) << "apply: " << ctx_.tx.getTransactionID();

    // NumberSO and CurrentTransactionRulesGuard ideally should apply for every
    // pipeline phase (preflight/preclaim/doApply); they were retrofitted here.
    // See with_txn_type() for the full story.
    NumberSO const stNumberSO{view().rules().enabled(fixUniversalNumber)};
    CurrentTransactionRulesGuard const currentTransactionRulesGuard(view().rules());

#ifdef DEBUG
    {
        Serializer ser;
        ctx_.tx.add(ser);
        SerialIter sit(ser.slice());
        STTx const s2(sit);

        if (!s2.isEquivalent(ctx_.tx))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Transaction serdes mismatch";
            JLOG(j_.fatal()) << ctx_.tx.getJson(JsonOptions::Values::None);
            JLOG(j_.fatal()) << s2.getJson(JsonOptions::Values::None);
            UNREACHABLE("xrpl::Transactor::operator() : transaction serdes mismatch");
            // LCOV_EXCL_STOP
        }
    }
#endif

    if (auto const& trap = ctx_.registry.get().getTrapTxID();
        trap && *trap == ctx_.tx.getTransactionID())
    {
        trapTransaction(*trap);
    }

    auto result = ctx_.preclaimResult;
    if (isTesSuccess(result))
        result = apply();

    // No transaction can return temUNKNOWN from apply,
    // and it can't be passed in from a preclaim.
    XRPL_ASSERT(result != temUNKNOWN, "xrpl::Transactor::operator() : result is not temUNKNOWN");

    if (auto stream = j_.trace())
        stream << "preclaim result: " << transToken(result);

    bool applied = isTesSuccess(result);
    auto fee = ctx_.tx.getFieldAmount(sfFee).xrp();

    if (ctx_.size() > kOVERSIZE_META_DATA_CAP)
        result = tecOVERSIZE;

    if (isTecClaim(result) && ((view().flags() & TapFailHard) != 0u))
    {
        // tapFAIL_HARD: discard all mutations; fee is not charged.
        ctx_.discard();
        applied = false;
    }
    else if (
        (result == tecOVERSIZE) || (result == tecKILLED) || (result == tecINCOMPLETE) ||
        (result == tecEXPIRED) || (isTecClaimHardFail(result, view().flags())))
    {
        JLOG(j_.trace()) << "reapplying because of " << transToken(result);

        std::vector<uint256> removedOffers;
        std::vector<uint256> removedTrustLines;
        std::vector<uint256> removedMPTs;
        std::vector<uint256> expiredNFTokenOffers;
        std::vector<uint256> expiredCredentials;

        bool const doOffers = ((result == tecOVERSIZE) || (result == tecKILLED));
        bool const doLinesOrMPTs = (result == tecINCOMPLETE);
        bool const doNFTokenOffers = (result == tecEXPIRED);
        bool const doCredentials = (result == tecEXPIRED);
        if (doOffers || doLinesOrMPTs || doNFTokenOffers || doCredentials)
        {
            ctx_.visit([doOffers,
                        &removedOffers,
                        doLinesOrMPTs,
                        &removedTrustLines,
                        &removedMPTs,
                        doNFTokenOffers,
                        &expiredNFTokenOffers,
                        doCredentials,
                        &expiredCredentials](
                           uint256 const& index,
                           bool isDelete,
                           std::shared_ptr<SLE const> const& before,
                           std::shared_ptr<SLE const> const& after) {
                if (isDelete)
                {
                    XRPL_ASSERT(
                        before && after,
                        "xrpl::Transactor::operator()::visit : non-null SLE "
                        "inputs");
                    if (doOffers && before && after && (before->getType() == ltOFFER) &&
                        (before->getFieldAmount(sfTakerPays) == after->getFieldAmount(sfTakerPays)))
                    {
                        // Removal of offer found or made unfunded
                        removedOffers.push_back(index);
                    }

                    if (doLinesOrMPTs && before && after)
                    {
                        // Removal of obsolete AMM trust line
                        if (before->getType() == ltRIPPLE_STATE)
                        {
                            removedTrustLines.push_back(index);
                        }
                        else if (before->getType() == ltMPTOKEN)
                        {
                            removedMPTs.push_back(index);
                        }
                    }

                    if (doNFTokenOffers && before && after &&
                        (before->getType() == ltNFTOKEN_OFFER))
                        expiredNFTokenOffers.push_back(index);

                    if (doCredentials && before && after && (before->getType() == ltCREDENTIAL))
                        expiredCredentials.push_back(index);
                }
            });
        }

        {
            auto const resetResult = reset(fee);
            if (!isTesSuccess(resetResult.first))
                result = resetResult.first;

            fee = resetResult.second;
        }

        if ((result == tecOVERSIZE) || (result == tecKILLED))
        {
            removeUnfundedOffers(view(), removedOffers, ctx_.registry.get().getJournal("View"));
        }

        if (result == tecEXPIRED)
        {
            removeExpiredNFTokenOffers(
                view(), expiredNFTokenOffers, ctx_.registry.get().getJournal("View"));
        }

        if (result == tecINCOMPLETE)
        {
            removeDeletedTrustLines(
                view(), removedTrustLines, ctx_.registry.get().getJournal("View"));
            removeDeletedMPTs(view(), removedMPTs, ctx_.registry.get().getJournal("View"));
        }

        if (result == tecEXPIRED)
        {
            removeExpiredCredentials(
                view(), expiredCredentials, ctx_.registry.get().getJournal("View"));
        }

        applied = isTecClaim(result);
    }

    if (applied)
    {
        result = checkInvariants(result, fee);
        if (result == tecINVARIANT_FAILED)
        {
            // Invariants failed: reset to fee-only claim and re-check protocol invariants.
            auto const resetResult = reset(fee);
            if (!isTesSuccess(resetResult.first))
                result = resetResult.first;

            fee = resetResult.second;

            // After reset, only protocol invariants are re-checked; transaction
            // invariants are not meaningful once effects have been rolled back.
            if (isTesSuccess(result) || isTecClaim(result))
                result = ctx_.checkInvariants(result, fee);
        }

        // A tef* from the invariant checker means the transaction cannot be
        // applied at all — not even as a fee claim.
        if (!isTecClaim(result) && !isTesSuccess(result))
            applied = false;
    }

    std::optional<TxMeta> metadata;
    if (applied)
    {
        // The transactor and invariant checkers guarantee this never triggers,
        // but guard against it anyway — a negative fee must never be committed.
        if (fee < beast::kZERO)
            Throw<std::logic_error>("fee charged is negative!");

        // Burn the fee in the closed ledger header (already deducted from the
        // account balance above; this records it as destroyed XRP).
        if (!view().open() && fee != beast::kZERO)
            ctx_.destroyXRP(fee);

        // ctx_.apply() commits the view to base_; view() is invalid after this.
        metadata = ctx_.apply(result);
    }

    if ((ctx_.flags() & TapDryRun) != 0u)
    {
        applied = false;
    }

    JLOG(j_.trace()) << (applied ? "applied " : "not applied ") << transToken(result);

    return {result, applied, metadata};
}

}  // namespace xrpl
