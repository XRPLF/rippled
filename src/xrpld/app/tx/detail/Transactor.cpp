#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/DelegateUtils.h>
#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/app/tx/apply.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>
#include <xrpld/app/tx/detail/SignerEntries.h>
#include <xrpld/app/tx/detail/Transactor.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/CredentialHelpers.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>

namespace ripple {

/** Performs early sanity checks on the txid */
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
        uint32_t nodeNID = ctx.app.config().NETWORK_ID;
        std::optional<uint32_t> txNID = ctx.tx[~sfNetworkID];

        if (nodeNID <= 1024)
        {
            // legacy networks have ids less than 1024, these networks cannot
            // specify NetworkID in txn
            if (txNID)
                return telNETWORK_ID_MAKES_TX_NON_CANONICAL;
        }
        else
        {
            // new networks both require the field to be present and require it
            // to match
            if (!txNID)
                return telREQUIRES_NETWORK_ID;

            if (*txNID != nodeNID)
                return telWRONG_NETWORK;
        }
    }

    auto const txID = ctx.tx.getTransactionID();

    if (txID == beast::zero)
    {
        JLOG(ctx.j.warn())
            << "applyTransaction: transaction id may not be zero";
        return temINVALID;
    }

    if (ctx.tx.getFlags() & flagMask)
    {
        JLOG(ctx.j.debug())
            << ctx.tx.peekAtField(sfTransactionType).getFullText()
            << ": invalid flags.";
        return temINVALID_FLAG;
    }

    return tesSUCCESS;
}

namespace detail {

/** Checks the validity of the transactor signing key.
 *
 * Normally called from preflight1.
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

std::optional<NotTEC>
preflightCheckSimulateKeys(
    ApplyFlags flags,
    STObject const& sigObject,
    beast::Journal j)
{
    if (flags & tapDRY_RUN)  // simulation
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
            if (signer.isFieldPresent(sfTxnSignature) &&
                !signer[sfTxnSignature].empty())
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

/** Performs early sanity checks on the account and fee fields */
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
    if (id == beast::zero)
    {
        JLOG(ctx.j.warn()) << "preflight1: bad account id";
        return temBAD_SRC_ACCOUNT;
    }

    // No point in going any further if the transaction fee is malformed.
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
    if (ctx.tx.getSeqProxy().isTicket() &&
        ctx.tx.isFieldPresent(sfAccountTxnID))
        return temINVALID;

    if (ctx.tx.isFlag(tfInnerBatchTxn) && !ctx.rules.enabled(featureBatch))
        return temINVALID_FLAG;

    XRPL_ASSERT(
        ctx.tx.isFlag(tfInnerBatchTxn) == ctx.parentBatchId.has_value() ||
            !ctx.rules.enabled(featureBatch),
        "Inner batch transaction must have a parent batch ID.");

    return tesSUCCESS;
}

/** Checks whether the signature appears valid */
NotTEC
Transactor::preflight2(PreflightContext const& ctx)
{
    if (auto const ret =
            detail::preflightCheckSimulateKeys(ctx.flags, ctx.tx, ctx.j))
        // Skips following checks if the transaction is being simulated,
        // regardless of success or failure
        return *ret;

    auto const sigValid = checkValidity(
        ctx.app.getHashRouter(), ctx.tx, ctx.rules, ctx.app.config());
    if (sigValid.first == Validity::SigBad)
    {
        JLOG(ctx.j.debug()) << "preflight2: bad signature. " << sigValid.second;
        return temINVALID;  // LCOV_EXCL_LINE
    }
    return tesSUCCESS;
}

//------------------------------------------------------------------------------

Transactor::Transactor(ApplyContext& ctx)
    : ctx_(ctx)
    , sink_(ctx.journal, to_short_string(ctx.tx.getTransactionID()) + " ")
    , j_(sink_)
    , account_(ctx.tx.getAccountID(sfAccount))
{
}

bool
Transactor::validDataLength(
    std::optional<Slice> const& slice,
    std::size_t maxLength)
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

XRPAmount
Transactor::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // Returns the fee in fee units.

    // The computation has two parts:
    //  * The base fee, which is the same for most transactions.
    //  * The additional cost of each multisignature on the transaction.
    XRPAmount const baseFee = view.fees().base;

    // Each signer adds one more baseFee to the minimum required fee
    // for the transaction.
    std::size_t const signerCount =
        tx.isFieldPresent(sfSigners) ? tx.getFieldArray(sfSigners).size() : 0;

    return baseFee + (signerCount * baseFee);
}

// Returns the fee in fee units, not scaled for load.
XRPAmount
Transactor::calculateOwnerReserveFee(ReadView const& view, STTx const& tx)
{
    // Assumption: One reserve increment is typically much greater than one base
    // fee.
    // This check is in an assert so that it will come to the attention of
    // developers if that assumption is not correct. If the owner reserve is not
    // significantly larger than the base fee (or even worse, smaller), we will
    // need to rethink charging an owner reserve as a transaction fee.
    // TODO: This function is static, and I don't want to add more parameters.
    // When it is finally refactored to be in a context that has access to the
    // Application, include "app().overlay().networkID() > 2 ||" in the
    // condition.
    XRPL_ASSERT(
        view.fees().increment > view.fees().base * 100,
        "ripple::Transactor::calculateOwnerReserveFee : Owner reserve is "
        "reasonable");
    return view.fees().increment;
}

XRPAmount
Transactor::minimumFee(
    Application& app,
    XRPAmount baseFee,
    Fees const& fees,
    ApplyFlags flags)
{
    return scaleFeeLoad(baseFee, app.getFeeTrack(), fees, flags & tapUNLIMITED);
}

TER
Transactor::checkFee(PreclaimContext const& ctx, XRPAmount baseFee)
{
    if (!ctx.tx[sfFee].native())
        return temBAD_FEE;

    auto const feePaid = ctx.tx[sfFee].xrp();

    if (ctx.flags & tapBATCH)
    {
        if (feePaid == beast::zero)
            return tesSUCCESS;

        JLOG(ctx.j.trace()) << "Batch: Fee must be zero.";
        return temBAD_FEE;  // LCOV_EXCL_LINE
    }

    if (!isLegalAmount(feePaid) || feePaid < beast::zero)
        return temBAD_FEE;

    // Only check fee is sufficient when the ledger is open.
    if (ctx.view.open())
    {
        auto const feeDue =
            minimumFee(ctx.app, baseFee, ctx.view.fees(), ctx.flags);

        if (feePaid < feeDue)
        {
            JLOG(ctx.j.trace())
                << "Insufficient fee paid: " << to_string(feePaid) << "/"
                << to_string(feeDue);
            return telINSUF_FEE_P;
        }
    }

    if (feePaid == beast::zero)
        return tesSUCCESS;

    auto const id = ctx.tx.isFieldPresent(sfDelegate)
        ? ctx.tx.getAccountID(sfDelegate)
        : ctx.tx.getAccountID(sfAccount);
    auto const sle = ctx.view.read(keylet::account(id));
    if (!sle)
        return terNO_ACCOUNT;

    auto const balance = (*sle)[sfBalance].xrp();

    if (balance < feePaid)
    {
        JLOG(ctx.j.trace())
            << "Insufficient balance:" << " balance=" << to_string(balance)
            << " paid=" << to_string(feePaid);

        if ((balance > beast::zero) && !ctx.view.open())
        {
            // Closed ledger, non-zero balance, less than fee
            return tecINSUFF_FEE;
        }

        return terINSUF_FEE_B;
    }

    return tesSUCCESS;
}

TER
Transactor::payFee()
{
    auto const feePaid = ctx_.tx[sfFee].xrp();

    if (ctx_.tx.isFieldPresent(sfDelegate))
    {
        // Delegated transactions are paid by the delegated account.
        auto const delegate = ctx_.tx.getAccountID(sfDelegate);
        auto const delegatedSle = view().peek(keylet::account(delegate));
        if (!delegatedSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        delegatedSle->setFieldAmount(
            sfBalance, delegatedSle->getFieldAmount(sfBalance) - feePaid);
        view().update(delegatedSle);
    }
    else
    {
        auto const sle = view().peek(keylet::account(account_));
        if (!sle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        // Deduct the fee, so it's not available during the transaction.
        // Will only write the account back if the transaction succeeds.

        mSourceBalance -= feePaid;
        sle->setFieldAmount(sfBalance, mSourceBalance);

        // VFALCO Should we call view().rawDestroyXRP() here as well?
    }

    return tesSUCCESS;
}

NotTEC
Transactor::checkSeqProxy(
    ReadView const& view,
    STTx const& tx,
    beast::Journal j)
{
    auto const id = tx.getAccountID(sfAccount);

    auto const sle = view.read(keylet::account(id));

    if (!sle)
    {
        JLOG(j.trace())
            << "applyTransaction: delay: source account does not exist "
            << toBase58(id);
        return terNO_ACCOUNT;
    }

    SeqProxy const t_seqProx = tx.getSeqProxy();
    SeqProxy const a_seq = SeqProxy::sequence((*sle)[sfSequence]);

    if (t_seqProx.isSeq())
    {
        if (tx.isFieldPresent(sfTicketSequence))
        {
            JLOG(j.trace()) << "applyTransaction: has both a TicketSequence "
                               "and a non-zero Sequence number";
            return temSEQ_AND_TICKET;
        }
        if (t_seqProx != a_seq)
        {
            if (a_seq < t_seqProx)
            {
                JLOG(j.trace())
                    << "applyTransaction: has future sequence number "
                    << "a_seq=" << a_seq << " t_seq=" << t_seqProx;
                return terPRE_SEQ;
            }
            // It's an already-used sequence number.
            JLOG(j.trace()) << "applyTransaction: has past sequence number "
                            << "a_seq=" << a_seq << " t_seq=" << t_seqProx;
            return tefPAST_SEQ;
        }
    }
    else if (t_seqProx.isTicket())
    {
        // Bypass the type comparison. Apples and oranges.
        if (a_seq.value() <= t_seqProx.value())
        {
            // If the Ticket number is greater than or equal to the
            // account sequence there's the possibility that the
            // transaction to create the Ticket has not hit the ledger
            // yet.  Allow a retry.
            JLOG(j.trace()) << "applyTransaction: has future ticket id "
                            << "a_seq=" << a_seq << " t_seq=" << t_seqProx;
            return terPRE_TICKET;
        }

        // Transaction can never succeed if the Ticket is not in the ledger.
        if (!view.exists(keylet::ticket(id, t_seqProx)))
        {
            JLOG(j.trace())
                << "applyTransaction: ticket already used or never created "
                << "a_seq=" << a_seq << " t_seq=" << t_seqProx;
            return tefNO_TICKET;
        }
    }

    return tesSUCCESS;
}

NotTEC
Transactor::checkPriorTxAndLastLedger(PreclaimContext const& ctx)
{
    auto const id = ctx.tx.getAccountID(sfAccount);

    auto const sle = ctx.view.read(keylet::account(id));

    if (!sle)
    {
        JLOG(ctx.j.trace())
            << "applyTransaction: delay: source account does not exist "
            << toBase58(id);
        return terNO_ACCOUNT;
    }

    if (ctx.tx.isFieldPresent(sfAccountTxnID) &&
        (sle->getFieldH256(sfAccountTxnID) !=
         ctx.tx.getFieldH256(sfAccountTxnID)))
        return tefWRONG_PRIOR;

    if (ctx.tx.isFieldPresent(sfLastLedgerSequence) &&
        (ctx.view.seq() > ctx.tx.getFieldU32(sfLastLedgerSequence)))
        return tefMAX_LEDGER;

    if (ctx.view.txExists(ctx.tx.getTransactionID()))
        return tefALREADY;

    return tesSUCCESS;
}

TER
Transactor::consumeSeqProxy(SLE::pointer const& sleAccount)
{
    XRPL_ASSERT(
        sleAccount, "ripple::Transactor::consumeSeqProxy : non-null account");
    SeqProxy const seqProx = ctx_.tx.getSeqProxy();
    if (seqProx.isSeq())
    {
        // Note that if this transaction is a TicketCreate, then
        // the transaction will modify the account root sfSequence
        // yet again.
        sleAccount->setFieldU32(sfSequence, seqProx.value() + 1);
        return tesSUCCESS;
    }
    return ticketDelete(
        view(), account_, getTicketIndex(account_, seqProx), j_);
}

// Remove a single Ticket from the ledger.
TER
Transactor::ticketDelete(
    ApplyView& view,
    AccountID const& account,
    uint256 const& ticketIndex,
    beast::Journal j)
{
    // Delete the Ticket, adjust the account root ticket count, and
    // reduce the owner count.
    SLE::pointer const sleTicket = view.peek(keylet::ticket(ticketIndex));
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

    // Update the account root's TicketCount.  If the ticket count drops to
    // zero remove the (optional) field.
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
            sleAccount->makeFieldAbsent(sfTicketCount);
        else
            ticketCount = *ticketCount - 1;
    }
    else
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "TicketCount field missing from account root.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    // Update the Ticket owner's reserve.
    adjustOwnerCount(view, sleAccount, -1, j);

    // Remove Ticket from ledger.
    view.erase(sleTicket);
    return tesSUCCESS;
}

// check stuff before you bother to lock the ledger
void
Transactor::preCompute()
{
    XRPL_ASSERT(
        account_ != beast::zero,
        "ripple::Transactor::preCompute : nonzero account");
}

TER
Transactor::apply()
{
    preCompute();

    // If the transactor requires a valid account and the transaction doesn't
    // list one, preflight will have already a flagged a failure.
    auto const sle = view().peek(keylet::account(account_));

    // sle must exist except for transactions
    // that allow zero account.
    XRPL_ASSERT(
        sle != nullptr || account_ == beast::zero,
        "ripple::Transactor::apply : non-null SLE or zero account");

    if (sle)
    {
        mPriorBalance = STAmount{(*sle)[sfBalance]}.xrp();
        mSourceBalance = mPriorBalance;

        TER result = consumeSeqProxy(sle);
        if (result != tesSUCCESS)
            return result;

        result = payFee();
        if (result != tesSUCCESS)
            return result;

        if (sle->isFieldPresent(sfAccountTxnID))
            sle->setFieldH256(sfAccountTxnID, ctx_.tx.getTransactionID());

        view().update(sle);
    }

    return doApply();
}

NotTEC
Transactor::checkSign(
    ReadView const& view,
    ApplyFlags flags,
    AccountID const& idAccount,
    STObject const& sigObject,
    beast::Journal const j)
{
    auto const pkSigner = sigObject.getFieldVL(sfSigningPubKey);
    // Ignore signature check on batch inner transactions
    if (sigObject.isFlag(tfInnerBatchTxn) && view.rules().enabled(featureBatch))
    {
        // Defensive Check: These values are also checked in Batch::preflight
        if (sigObject.isFieldPresent(sfTxnSignature) || !pkSigner.empty() ||
            sigObject.isFieldPresent(sfSigners))
        {
            return temINVALID_FLAG;  // LCOV_EXCL_LINE
        }
        return tesSUCCESS;
    }

    if ((flags & tapDRY_RUN) && pkSigner.empty() &&
        !sigObject.isFieldPresent(sfSigners))
    {
        // simulate: skip signature validation when neither SigningPubKey nor
        // Signers are provided
        return tesSUCCESS;
    }

    // If the pk is empty and not simulate or simulate and signers,
    // then we must be multi-signing.
    if (sigObject.isFieldPresent(sfSigners))
    {
        return checkMultiSign(view, flags, idAccount, sigObject, j);
    }

    // Check Single Sign
    XRPL_ASSERT(
        !pkSigner.empty(), "ripple::Transactor::checkSign : non-empty signer");

    if (!publicKeyType(makeSlice(pkSigner)))
    {
        JLOG(j.trace()) << "checkSign: signing public key type is unknown";
        return tefBAD_AUTH;  // FIXME: should be better error!
    }

    // Look up the account.
    auto const idSigner = pkSigner.empty()
        ? idAccount
        : calcAccountID(PublicKey(makeSlice(pkSigner)));
    auto const sleAccount = view.read(keylet::account(idAccount));
    if (!sleAccount)
        return terNO_ACCOUNT;

    return checkSingleSign(view, idSigner, idAccount, sleAccount, j);
}

NotTEC
Transactor::checkSign(PreclaimContext const& ctx)
{
    auto const idAccount = ctx.tx.isFieldPresent(sfDelegate)
        ? ctx.tx.getAccountID(sfDelegate)
        : ctx.tx.getAccountID(sfAccount);
    return checkSign(ctx.view, ctx.flags, idAccount, ctx.tx, ctx.j);
}

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
            if (ret = checkMultiSign(
                    ctx.view, ctx.flags, idAccount, signer, ctx.j);
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

            if (ret = checkSingleSign(
                    ctx.view, idSigner, idAccount, sleAccount, ctx.j);
                !isTesSuccess(ret))
                return ret;
        }
    }
    return ret;
}

NotTEC
Transactor::checkSingleSign(
    ReadView const& view,
    AccountID const& idSigner,
    AccountID const& idAccount,
    std::shared_ptr<SLE const> sleAccount,
    beast::Journal const j)
{
    bool const isMasterDisabled = sleAccount->isFlag(lsfDisableMaster);

    // Signed with regular key.
    if ((*sleAccount)[~sfRegularKey] == idSigner)
    {
        return tesSUCCESS;
    }

    // Signed with enabled master key.
    if (!isMasterDisabled && idAccount == idSigner)
    {
        return tesSUCCESS;
    }

    // Signed with disabled master key.
    if (isMasterDisabled && idAccount == idSigner)
    {
        return tefMASTER_DISABLED;
    }

    // Signed with any other key.
    return tefBAD_AUTH;
}

NotTEC
Transactor::checkMultiSign(
    ReadView const& view,
    ApplyFlags flags,
    AccountID const& id,
    STObject const& sigObject,
    beast::Journal const j)
{
    // Get id's SignerList and Quorum.
    std::shared_ptr<STLedgerEntry const> sleAccountSigners =
        view.read(keylet::signers(id));
    // If the signer list doesn't exist the account is not multi-signing.
    if (!sleAccountSigners)
    {
        JLOG(j.trace())
            << "applyTransaction: Invalid: Not a multi-signing account.";
        return tefNOT_MULTI_SIGNING;
    }

    // We have plans to support multiple SignerLists in the future.  The
    // presence and defaulted value of the SignerListID field will enable that.
    XRPL_ASSERT(
        sleAccountSigners->isFieldPresent(sfSignerListID),
        "ripple::Transactor::checkMultiSign : has signer list ID");
    XRPL_ASSERT(
        sleAccountSigners->getFieldU32(sfSignerListID) == 0,
        "ripple::Transactor::checkMultiSign : signer list ID is 0");

    auto accountSigners =
        SignerEntries::deserialize(*sleAccountSigners, j, "ledger");
    if (!accountSigners)
        return accountSigners.error();

    // Get the array of transaction signers.
    STArray const& txSigners(sigObject.getFieldArray(sfSigners));

    // Walk the accountSigners performing a variety of checks and see if
    // the quorum is met.

    // Both the multiSigners and accountSigners are sorted by account.  So
    // matching multi-signers to account signers should be a simple
    // linear walk.  *All* signers must be valid or the transaction fails.
    std::uint32_t weightSum = 0;
    auto iter = accountSigners->begin();
    for (auto const& txSigner : txSigners)
    {
        AccountID const txSignerAcctID = txSigner.getAccountID(sfAccount);

        // Attempt to match the SignerEntry with a Signer;
        while (iter->account < txSignerAcctID)
        {
            if (++iter == accountSigners->end())
            {
                JLOG(j.trace())
                    << "applyTransaction: Invalid SigningAccount.Account.";
                return tefBAD_SIGNATURE;
            }
        }
        if (iter->account != txSignerAcctID)
        {
            // The SigningAccount is not in the SignerEntries.
            JLOG(j.trace())
                << "applyTransaction: Invalid SigningAccount.Account.";
            return tefBAD_SIGNATURE;
        }

        // We found the SigningAccount in the list of valid signers.  Now we
        // need to compute the accountID that is associated with the signer's
        // public key.
        auto const spk = txSigner.getFieldVL(sfSigningPubKey);

        // spk being non-empty in non-simulate is checked in
        // STTx::checkMultiSign
        if (!spk.empty() && !publicKeyType(makeSlice(spk)))
        {
            JLOG(j.trace())
                << "checkMultiSign: signing public key type is unknown";
            return tefBAD_SIGNATURE;
        }

        XRPL_ASSERT(
            (flags & tapDRY_RUN) || !spk.empty(),
            "ripple::Transactor::checkMultiSign : non-empty signer or "
            "simulation");
        AccountID const signingAcctIDFromPubKey = spk.empty()
            ? txSignerAcctID
            : calcAccountID(PublicKey(makeSlice(spk)));

        // Verify that the signingAcctID and the signingAcctIDFromPubKey
        // belong together.  Here are the rules:
        //
        //   1. "Phantom account": an account that is not in the ledger
        //      A. If signingAcctID == signingAcctIDFromPubKey and the
        //         signingAcctID is not in the ledger then we have a phantom
        //         account.
        //      B. Phantom accounts are always allowed as multi-signers.
        //
        //   2. "Master Key"
        //      A. signingAcctID == signingAcctIDFromPubKey, and signingAcctID
        //         is in the ledger.
        //      B. If the signingAcctID in the ledger does not have the
        //         asfDisableMaster flag set, then the signature is allowed.
        //
        //   3. "Regular Key"
        //      A. signingAcctID != signingAcctIDFromPubKey, and signingAcctID
        //         is in the ledger.
        //      B. If signingAcctIDFromPubKey == signingAcctID.RegularKey (from
        //         ledger) then the signature is allowed.
        //
        // No other signatures are allowed.  (January 2015)

        // In any of these cases we need to know whether the account is in
        // the ledger.  Determine that now.
        auto const sleTxSignerRoot = view.read(keylet::account(txSignerAcctID));

        if (signingAcctIDFromPubKey == txSignerAcctID)
        {
            // Either Phantom or Master.  Phantoms automatically pass.
            if (sleTxSignerRoot)
            {
                // Master Key.  Account may not have asfDisableMaster set.
                std::uint32_t const signerAccountFlags =
                    sleTxSignerRoot->getFieldU32(sfFlags);

                if (signerAccountFlags & lsfDisableMaster)
                {
                    JLOG(j.trace())
                        << "applyTransaction: Signer:Account lsfDisableMaster.";
                    return tefMASTER_DISABLED;
                }
            }
        }
        else
        {
            // May be a Regular Key.  Let's find out.
            // Public key must hash to the account's regular key.
            if (!sleTxSignerRoot)
            {
                JLOG(j.trace()) << "applyTransaction: Non-phantom signer "
                                   "lacks account root.";
                return tefBAD_SIGNATURE;
            }

            if (!sleTxSignerRoot->isFieldPresent(sfRegularKey))
            {
                JLOG(j.trace())
                    << "applyTransaction: Account lacks RegularKey.";
                return tefBAD_SIGNATURE;
            }
            if (signingAcctIDFromPubKey !=
                sleTxSignerRoot->getAccountID(sfRegularKey))
            {
                JLOG(j.trace())
                    << "applyTransaction: Account doesn't match RegularKey.";
                return tefBAD_SIGNATURE;
            }
        }
        // The signer is legitimate.  Add their weight toward the quorum.
        weightSum += iter->weight;
    }

    // Cannot perform transaction if quorum is not met.
    if (weightSum < sleAccountSigners->getFieldU32(sfSignerQuorum))
    {
        JLOG(j.trace()) << "applyTransaction: Signers failed to meet quorum.";
        return tefBAD_QUORUM;
    }

    // Met the quorum.  Continue.
    return tesSUCCESS;
}

//------------------------------------------------------------------------------

static void
removeUnfundedOffers(
    ApplyView& view,
    std::vector<uint256> const& offers,
    beast::Journal viewJ)
{
    int removed = 0;

    for (auto const& index : offers)
    {
        if (auto const sleOffer = view.peek(keylet::offer(index)))
        {
            // offer is unfunded
            offerDelete(view, sleOffer, viewJ);
            if (++removed == unfundedOfferRemoveLimit)
                return;
        }
    }
}

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
            if (++removed == expiredOfferRemoveLimit)
                return;
        }
    }
}

static void
removeExpiredCredentials(
    ApplyView& view,
    std::vector<uint256> const& creds,
    beast::Journal viewJ)
{
    for (auto const& index : creds)
    {
        if (auto const sle = view.peek(keylet::credential(index)))
            credentials::deleteSLE(view, sle, viewJ);
    }
}

static void
removeDeletedTrustLines(
    ApplyView& view,
    std::vector<uint256> const& trustLines,
    beast::Journal viewJ)
{
    if (trustLines.size() > maxDeletableAMMTrustLines)
    {
        JLOG(viewJ.error())
            << "removeDeletedTrustLines: deleted trustlines exceed max "
            << trustLines.size();
        return;
    }

    for (auto const& index : trustLines)
    {
        if (auto const sleState = view.peek({ltRIPPLE_STATE, index});
            deleteAMMTrustLine(view, sleState, std::nullopt, viewJ) !=
            tesSUCCESS)
        {
            JLOG(viewJ.error())
                << "removeDeletedTrustLines: failed to delete AMM trustline";
        }
    }
}

/** Reset the context, discarding any changes made and adjust the fee.

    @param fee The transaction fee to be charged.
    @return A pair containing the transaction result and the actual fee charged.
 */
std::pair<TER, XRPAmount>
Transactor::reset(XRPAmount fee)
{
    ctx_.discard();

    auto const txnAcct =
        view().peek(keylet::account(ctx_.tx.getAccountID(sfAccount)));

    // The account should never be missing from the ledger.  But if it
    // is missing then we can't very well charge it a fee, can we?
    if (!txnAcct)
        return {tefINTERNAL, beast::zero};

    auto const payerSle = ctx_.tx.isFieldPresent(sfDelegate)
        ? view().peek(keylet::account(ctx_.tx.getAccountID(sfDelegate)))
        : txnAcct;
    if (!payerSle)
        return {tefINTERNAL, beast::zero};  // LCOV_EXCL_LINE

    auto const balance = payerSle->getFieldAmount(sfBalance).xrp();

    // balance should have already been checked in checkFee / preFlight.
    XRPL_ASSERT(
        balance != beast::zero && (!view().open() || balance >= fee),
        "ripple::Transactor::reset : valid balance");

    // We retry/reject the transaction if the account balance is zero or
    // we're applying against an open ledger and the balance is less than
    // the fee
    if (fee > balance)
        fee = balance;

    // Since we reset the context, we need to charge the fee and update
    // the account's sequence number (or consume the Ticket) again.
    //
    // If for some reason we are unable to consume the ticket or sequence
    // then the ledger is corrupted.  Rather than make things worse we
    // reject the transaction.
    payerSle->setFieldAmount(sfBalance, balance - fee);
    TER const ter{consumeSeqProxy(txnAcct)};
    XRPL_ASSERT(
        isTesSuccess(ter), "ripple::Transactor::reset : result is tesSUCCESS");

    if (isTesSuccess(ter))
    {
        view().update(txnAcct);
        if (payerSle != txnAcct)
            view().update(payerSle);
    }

    return {ter, fee};
}

// The sole purpose of this function is to provide a convenient, named
// location to set a breakpoint, to be used when replaying transactions.
void
Transactor::trapTransaction(uint256 txHash) const
{
    JLOG(j_.debug()) << "Transaction trapped: " << txHash;
}

//------------------------------------------------------------------------------
ApplyResult
Transactor::operator()()
{
    JLOG(j_.trace()) << "apply: " << ctx_.tx.getTransactionID();

    // raii classes for the current ledger rules.
    // fixUniversalNumber predate the rulesGuard and should be replaced.
    NumberSO stNumberSO{view().rules().enabled(fixUniversalNumber)};
    CurrentTransactionRulesGuard currentTransctionRulesGuard(view().rules());

#ifdef DEBUG
    {
        Serializer ser;
        ctx_.tx.add(ser);
        SerialIter sit(ser.slice());
        STTx s2(sit);

        if (!s2.isEquivalent(ctx_.tx))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Transaction serdes mismatch";
            JLOG(j_.info()) << to_string(ctx_.tx.getJson(JsonOptions::none));
            JLOG(j_.fatal()) << s2.getJson(JsonOptions::none);
            UNREACHABLE(
                "ripple::Transactor::operator() : transaction serdes mismatch");
            // LCOV_EXCL_STOP
        }
    }
#endif

    if (auto const& trap = ctx_.app.trapTxID();
        trap && *trap == ctx_.tx.getTransactionID())
    {
        trapTransaction(*trap);
    }

    auto result = ctx_.preclaimResult;
    if (result == tesSUCCESS)
        result = apply();

    // No transaction can return temUNKNOWN from apply,
    // and it can't be passed in from a preclaim.
    XRPL_ASSERT(
        result != temUNKNOWN,
        "ripple::Transactor::operator() : result is not temUNKNOWN");

    if (auto stream = j_.trace())
        stream << "preclaim result: " << transToken(result);

    bool applied = isTesSuccess(result);
    auto fee = ctx_.tx.getFieldAmount(sfFee).xrp();

    if (ctx_.size() > oversizeMetaDataCap)
        result = tecOVERSIZE;

    if (isTecClaim(result) && (view().flags() & tapFAIL_HARD))
    {
        // If the tapFAIL_HARD flag is set, a tec result
        // must not do anything
        ctx_.discard();
        applied = false;
    }
    else if (
        (result == tecOVERSIZE) || (result == tecKILLED) ||
        (result == tecINCOMPLETE) || (result == tecEXPIRED) ||
        (isTecClaimHardFail(result, view().flags())))
    {
        JLOG(j_.trace()) << "reapplying because of " << transToken(result);

        // FIXME: This mechanism for doing work while returning a `tec` is
        //        awkward and very limiting. A more general purpose approach
        //        should be used, making it possible to do more useful work
        //        when transactions fail with a `tec` code.
        std::vector<uint256> removedOffers;
        std::vector<uint256> removedTrustLines;
        std::vector<uint256> expiredNFTokenOffers;
        std::vector<uint256> expiredCredentials;

        bool const doOffers =
            ((result == tecOVERSIZE) || (result == tecKILLED));
        bool const doLines = (result == tecINCOMPLETE);
        bool const doNFTokenOffers = (result == tecEXPIRED);
        bool const doCredentials = (result == tecEXPIRED);
        if (doOffers || doLines || doNFTokenOffers || doCredentials)
        {
            ctx_.visit([doOffers,
                        &removedOffers,
                        doLines,
                        &removedTrustLines,
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
                        "ripple::Transactor::operator()::visit : non-null SLE "
                        "inputs");
                    if (doOffers && before && after &&
                        (before->getType() == ltOFFER) &&
                        (before->getFieldAmount(sfTakerPays) ==
                         after->getFieldAmount(sfTakerPays)))
                    {
                        // Removal of offer found or made unfunded
                        removedOffers.push_back(index);
                    }

                    if (doLines && before && after &&
                        (before->getType() == ltRIPPLE_STATE))
                    {
                        // Removal of obsolete AMM trust line
                        removedTrustLines.push_back(index);
                    }

                    if (doNFTokenOffers && before && after &&
                        (before->getType() == ltNFTOKEN_OFFER))
                        expiredNFTokenOffers.push_back(index);

                    if (doCredentials && before && after &&
                        (before->getType() == ltCREDENTIAL))
                        expiredCredentials.push_back(index);
                }
            });
        }

        // Reset the context, potentially adjusting the fee.
        {
            auto const resetResult = reset(fee);
            if (!isTesSuccess(resetResult.first))
                result = resetResult.first;

            fee = resetResult.second;
        }

        // If necessary, remove any offers found unfunded during processing
        if ((result == tecOVERSIZE) || (result == tecKILLED))
            removeUnfundedOffers(
                view(), removedOffers, ctx_.app.journal("View"));

        if (result == tecEXPIRED)
            removeExpiredNFTokenOffers(
                view(), expiredNFTokenOffers, ctx_.app.journal("View"));

        if (result == tecINCOMPLETE)
            removeDeletedTrustLines(
                view(), removedTrustLines, ctx_.app.journal("View"));

        if (result == tecEXPIRED)
            removeExpiredCredentials(
                view(), expiredCredentials, ctx_.app.journal("View"));

        applied = isTecClaim(result);
    }

    if (applied)
    {
        // Check invariants: if `tecINVARIANT_FAILED` is not returned, we can
        // proceed to apply the tx
        result = ctx_.checkInvariants(result, fee);

        if (result == tecINVARIANT_FAILED)
        {
            // if invariants checking failed again, reset the context and
            // attempt to only claim a fee.
            auto const resetResult = reset(fee);
            if (!isTesSuccess(resetResult.first))
                result = resetResult.first;

            fee = resetResult.second;

            // Check invariants again to ensure the fee claiming doesn't
            // violate invariants.
            if (isTesSuccess(result) || isTecClaim(result))
                result = ctx_.checkInvariants(result, fee);
        }

        // We ran through the invariant checker, which can, in some cases,
        // return a tef error code. Don't apply the transaction in that case.
        if (!isTecClaim(result) && !isTesSuccess(result))
            applied = false;
    }

    std::optional<TxMeta> metadata;
    if (applied)
    {
        // Transaction succeeded fully or (retries are not allowed and the
        // transaction could claim a fee)

        // The transactor and invariant checkers guarantee that this will
        // *never* trigger but if it, somehow, happens, don't allow a tx
        // that charges a negative fee.
        if (fee < beast::zero)
            Throw<std::logic_error>("fee charged is negative!");

        // Charge whatever fee they specified. The fee has already been
        // deducted from the balance of the account that issued the
        // transaction. We just need to account for it in the ledger
        // header.
        if (!view().open() && fee != beast::zero)
            ctx_.destroyXRP(fee);

        // Once we call apply, we will no longer be able to look at view()
        metadata = ctx_.apply(result);
    }

    if (ctx_.flags() & tapDRY_RUN)
    {
        applied = false;
    }

    JLOG(j_.trace()) << (applied ? "applied " : "not applied ")
                     << transToken(result);

    return {result, applied, metadata};
}

}  // namespace ripple
