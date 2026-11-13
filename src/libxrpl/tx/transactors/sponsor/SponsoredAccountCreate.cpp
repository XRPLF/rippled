#include <xrpl/tx/transactors/sponsor/SponsoredAccountCreate.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/transactors/sponsor/SponsorshipSet.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

namespace xrpl {

TxConsequences
SponsoredAccountCreate::makeTxConsequences(PreflightContext const& ctx)
{
    auto const feeAmount = ctx.tx[~sfFeeAmount].value_or(STAmount{0});
    return TxConsequences{ctx.tx, std::max(STAmount{0}, feeAmount).xrp()};
}

bool
SponsoredAccountCreate::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureSponsor);
}

NotTEC
SponsoredAccountCreate::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfDestination] == beast::kZero)
        return temDST_NEEDED;

    if (ctx.tx[sfDestination] == ctx.tx[sfAccount])
        return temDST_IS_SRC;

    // FeeAmount must be a positive XRP amount when present.
    if (auto const feeAmount = ctx.tx[~sfFeeAmount];
        feeAmount && (!isXRP(*feeAmount) || *feeAmount <= beast::kZero))
        return temBAD_AMOUNT;

    // MaxFee must be a positive XRP amount when present.
    if (auto const maxFee = ctx.tx[~sfMaxFee];
        maxFee && (!isXRP(*maxFee) || *maxFee <= beast::kZero))
        return temBAD_AMOUNT;

    // RemainingOwnerCount must be positive when present.
    if (auto const remainingOwnerCount = ctx.tx[~sfRemainingOwnerCount];
        remainingOwnerCount && *remainingOwnerCount == 0)
        return temINVALID;

    // A Sponsorship object needs a budget: a FeeAmount or a RemainingOwnerCount.
    if (ctx.tx.isFieldPresent(sfMaxFee) && !ctx.tx.isFieldPresent(sfFeeAmount) &&
        !ctx.tx.isFieldPresent(sfRemainingOwnerCount))
        return temMALFORMED;

    return tesSUCCESS;
}

TER
SponsoredAccountCreate::preclaim(PreclaimContext const& ctx)
{
    if (ctx.view.exists(keylet::account(ctx.tx[sfDestination])))
        return tecDUPLICATE;

    return tesSUCCESS;
}

TER
SponsoredAccountCreate::doApply()
{
    auto const dstAccountID = ctx_.tx[sfDestination];
    auto const feeAmount = ctx_.tx[~sfFeeAmount];
    auto const maxFee = ctx_.tx[~sfMaxFee];
    auto const remainingOwnerCount = ctx_.tx[~sfRemainingOwnerCount];
    bool const createSponsorship = feeAmount || remainingOwnerCount;

    auto const sponsorSle = view().peek(keylet::account(accountID_));
    if (!sponsorSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const dstKeylet = keylet::account(dstAccountID);
    if (view().exists(dstKeylet))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sponsoringAccountCount = sponsorSle->getFieldU32(sfSponsoringAccountCount);
    if (sponsoringAccountCount == std::numeric_limits<std::uint32_t>::max())
    {
        // LCOV_EXCL_START
        JLOG(j_.fatal()) << "Sponsoring account count overflow for account "
                         << to_string(accountID_);
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    // The sponsor covers the new account's base reserve, the Sponsorship
    // object's owner reserve, and the prefunded FeeAmount.
    auto const balanceAfterFeeAmount =
        sponsorSle->getFieldAmount(sfBalance).xrp() - feeAmount.value_or(STAmount{0}).xrp();
    if (auto const ret = checkReserve(
            ctx_.getApplyViewContext(),
            sponsorSle,
            balanceAfterFeeAmount,
            nullptr,
            {.ownerCountDelta = createSponsorship ? 1 : 0, .accountCountDelta = 1},
            j_);
        !isTesSuccess(ret))
        return ret;

    auto const sleDst = std::make_shared<SLE>(dstKeylet);
    sleDst->setAccountID(sfAccount, dstAccountID);
    sleDst->setFieldU32(sfSequence, view().seq());
    sleDst->setFieldAmount(sfBalance, XRPAmount(beast::kZero));
    addSponsorToLedgerEntry(sleDst, sponsorSle);
    view().insert(sleDst);

    sponsorSle->setFieldU32(sfSponsoringAccountCount, sponsoringAccountCount + 1);
    view().update(sponsorSle);

    if (createSponsorship)
    {
        return SponsorshipSet::createSponsorship(
            ctx_.getApplyViewContext(),
            sponsorSle,
            dstAccountID,
            nullptr,
            feeAmount,
            maxFee,
            remainingOwnerCount,
            0,
            j_);
    }

    return tesSUCCESS;
}

void
SponsoredAccountCreate::visitInvariantEntry(
    bool isDelete,
    SLE::ConstRef before,
    SLE::ConstRef after)
{
    auto const& sle = after ? after : before;
    if (!sle)
        return;  // LCOV_EXCL_LINE

    bool const created = !isDelete && !before;

    auto const& tx = ctx_.tx;
    auto const initiator = tx.getInitiator();
    std::optional<AccountID> const feeSponsor =
        isFeeSponsored(tx) ? std::optional<AccountID>{tx.getAccountID(sfSponsor)} : std::nullopt;

    // Besides the new account and Sponsorship, only the fee payment and
    // ticket consumption may touch the ledger. Existing account roots and
    // Sponsorships may only be modified as the submitter or the fee payer.
    // Consuming a proposal-reserved ticket also deletes that proposal and
    // updates its Owner or reserve sponsor; those accounts are checked in
    // finalizeInvariants, once the proposal has been visited.
    // NOLINTBEGIN(bugprone-branch-clone)
    switch (sle->getType())
    {
        case ltACCOUNT_ROOT: {
            if (isDelete)
            {
                unexpectedEntry_ = true;
            }
            else if (created)
            {
                accountsCreated_.insert(after->key());
                newAccountID_ = after->getAccountID(sfAccount);
                newAccountSponsor_ = getLedgerEntryReserveSponsorID(after);
                if (after->getFieldAmount(sfBalance) != beast::kZero)
                    nonZeroBalance_ = true;
            }
            else if (auto const id = after->getAccountID(sfAccount); id == accountID_)
            {
                sponsoringAccountCountDelta_ =
                    static_cast<std::int64_t>(after->getFieldU32(sfSponsoringAccountCount)) -
                    before->getFieldU32(sfSponsoringAccountCount);
            }
            else if (id != initiator && id != feeSponsor)
            {
                otherAccountsModified_.insert(id);
            }
        }
        break;
        case ltSPONSORSHIP: {
            if (isDelete)
            {
                unexpectedEntry_ = true;
            }
            else if (created)
            {
                sponsorshipsCreated_.insert(after->key());
                newSponsorship_ = after;
            }
            else if (!feeSponsor || after->key() != keylet::sponsorship(*feeSponsor, initiator).key)
            {
                unexpectedEntry_ = true;
            }
        }
        break;
        case ltDIR_NODE:
            break;
        case ltTICKET:
            if (!isDelete)
                unexpectedEntry_ = true;
            break;
        case ltTRANSACTION_PROPOSAL: {
            if (auto const seqProxy = tx.getSeqProxy(); isDelete && seqProxy.isTicket() &&
                before->key() == keylet::txProposal(accountID_, seqProxy.value()).key)
            {
                proposalOwner_ = before->getAccountID(sfOwner);
                proposalReserveSponsor_ = getLedgerEntryReserveSponsorID(before);
            }
            else
            {
                unexpectedEntry_ = true;
            }
        }
        break;
        default:
            unexpectedEntry_ = true;
            break;
    }
    // NOLINTEND(bugprone-branch-clone)
}

bool
SponsoredAccountCreate::finalizeInvariants(
    STTx const& tx,
    TER result,
    XRPAmount,
    ReadView const&,
    beast::Journal const& j)
{
    if (unexpectedEntry_ || std::ranges::any_of(otherAccountsModified_, [&](AccountID const& id) {
            return id != proposalOwner_ && id != proposalReserveSponsor_;
        }))
    {
        JLOG(j.fatal()) << "Invariant failed: SponsoredAccountCreate modified an "
                           "unexpected ledger entry";
        return false;
    }

    if (!isTesSuccess(result))
    {
        if (!accountsCreated_.empty() || !sponsorshipsCreated_.empty())
        {
            JLOG(j.fatal()) << "Invariant failed: SponsoredAccountCreate created ledger "
                               "entries on failure";
            return false;
        }
        return true;
    }

    if (accountsCreated_.size() != 1 || newAccountID_ != tx[sfDestination])
    {
        JLOG(j.fatal()) << "Invariant failed: SponsoredAccountCreate did not create "
                           "exactly the destination account";
        return false;
    }

    if (newAccountSponsor_ != tx[sfAccount])
    {
        JLOG(j.fatal()) << "Invariant failed: SponsoredAccountCreate created an account "
                           "not sponsored by the submitter";
        return false;
    }

    if (nonZeroBalance_)
    {
        JLOG(j.fatal()) << "Invariant failed: SponsoredAccountCreate created an account "
                           "with a non-zero balance";
        return false;
    }

    if (sponsoringAccountCountDelta_ != 1)
    {
        JLOG(j.fatal()) << "Invariant failed: SponsoredAccountCreate did not increment "
                           "the submitter's SponsoringAccountCount by one";
        return false;
    }

    bool const sponsorshipRequested =
        tx.isFieldPresent(sfFeeAmount) || tx.isFieldPresent(sfRemainingOwnerCount);
    if (sponsorshipsCreated_.size() != (sponsorshipRequested ? 1 : 0))
    {
        JLOG(j.fatal()) << "Invariant failed: SponsoredAccountCreate did not create "
                           "exactly the requested Sponsorship";
        return false;
    }

    if (newSponsorship_ &&
        (newSponsorship_->getAccountID(sfOwner) != tx[sfAccount] ||
         newSponsorship_->getAccountID(sfSponsee) != tx[sfDestination] ||
         (*newSponsorship_)[~sfFeeAmount] != tx[~sfFeeAmount] ||
         (*newSponsorship_)[~sfMaxFee] != tx[~sfMaxFee] ||
         newSponsorship_->getFieldU32(sfRemainingOwnerCount) !=
             tx[~sfRemainingOwnerCount].value_or(0) ||
         newSponsorship_->getFieldU32(sfFlags) != 0))
    {
        JLOG(j.fatal()) << "Invariant failed: SponsoredAccountCreate created a Sponsorship "
                           "that does not match the transaction";
        return false;
    }

    return true;
}

}  // namespace xrpl
