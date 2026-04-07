#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/st.h>

#include <type_traits>
#include <variant>

namespace xrpl {

//------------------------------------------------------------------------------
//
// Observers
//
//------------------------------------------------------------------------------

bool
hasExpired(ReadView const& view, std::optional<std::uint32_t> const& exp)
{
    using d = NetClock::duration;
    using tp = NetClock::time_point;

    return exp && (view.parentCloseTime() >= tp{d{*exp}});
}

bool
isVaultPseudoAccountFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptShare,
    int depth)
{
    if (!view.rules().enabled(featureSingleAssetVault))
        return false;

    if (depth >= maxAssetCheckDepth)
        return true;  // LCOV_EXCL_LINE

    auto const mptIssuance = view.read(keylet::mptIssuance(mptShare.getMptID()));
    if (mptIssuance == nullptr)
        return false;  // zero MPToken won't block deletion of MPTokenIssuance

    auto const issuer = mptIssuance->getAccountID(sfIssuer);
    auto const mptIssuer = view.read(keylet::account(issuer));
    if (mptIssuer == nullptr)
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::isVaultPseudoAccountFrozen : null MPToken issuer");
        return false;
        // LCOV_EXCL_STOP
    }

    if (!mptIssuer->isFieldPresent(sfVaultID))
        return false;  // not a Vault pseudo-account, common case

    auto const vault = view.read(keylet::vault(mptIssuer->getFieldH256(sfVaultID)));
    if (vault == nullptr)
    {  // LCOV_EXCL_START
        UNREACHABLE("xrpl::isVaultPseudoAccountFrozen : null vault");
        return false;
        // LCOV_EXCL_STOP
    }

    return isAnyFrozen(view, {issuer, account}, vault->at(sfAsset), depth + 1);
}

bool
isLPTokenFrozen(
    ReadView const& view,
    AccountID const& account,
    Issue const& asset,
    Issue const& asset2)
{
    return isFrozen(view, account, asset.currency, asset.account) ||
        isFrozen(view, account, asset2.currency, asset2.account);
}

bool
areCompatible(
    ReadView const& validLedger,
    ReadView const& testLedger,
    beast::Journal::Stream& s,
    char const* reason)
{
    bool ret = true;

    if (validLedger.header().seq < testLedger.header().seq)
    {
        // valid -> ... -> test
        auto hash = hashOfSeq(
            testLedger, validLedger.header().seq, beast::Journal{beast::Journal::getNullSink()});
        if (hash && (*hash != validLedger.header().hash))
        {
            JLOG(s) << reason << " incompatible with valid ledger";

            JLOG(s) << "Hash(VSeq): " << to_string(*hash);

            ret = false;
        }
    }
    else if (validLedger.header().seq > testLedger.header().seq)
    {
        // test -> ... -> valid
        auto hash = hashOfSeq(
            validLedger, testLedger.header().seq, beast::Journal{beast::Journal::getNullSink()});
        if (hash && (*hash != testLedger.header().hash))
        {
            JLOG(s) << reason << " incompatible preceding ledger";

            JLOG(s) << "Hash(NSeq): " << to_string(*hash);

            ret = false;
        }
    }
    else if (
        (validLedger.header().seq == testLedger.header().seq) &&
        (validLedger.header().hash != testLedger.header().hash))
    {
        // Same sequence number, different hash
        JLOG(s) << reason << " incompatible ledger";

        ret = false;
    }

    if (!ret)
    {
        JLOG(s) << "Val: " << validLedger.header().seq << " "
                << to_string(validLedger.header().hash);

        JLOG(s) << "New: " << testLedger.header().seq << " " << to_string(testLedger.header().hash);
    }

    return ret;
}

bool
areCompatible(
    uint256 const& validHash,
    LedgerIndex validIndex,
    ReadView const& testLedger,
    beast::Journal::Stream& s,
    char const* reason)
{
    bool ret = true;

    if (testLedger.header().seq > validIndex)
    {
        // Ledger we are testing follows last valid ledger
        auto hash =
            hashOfSeq(testLedger, validIndex, beast::Journal{beast::Journal::getNullSink()});
        if (hash && (*hash != validHash))
        {
            JLOG(s) << reason << " incompatible following ledger";
            JLOG(s) << "Hash(VSeq): " << to_string(*hash);

            ret = false;
        }
    }
    else if ((validIndex == testLedger.header().seq) && (testLedger.header().hash != validHash))
    {
        JLOG(s) << reason << " incompatible ledger";

        ret = false;
    }

    if (!ret)
    {
        JLOG(s) << "Val: " << validIndex << " " << to_string(validHash);

        JLOG(s) << "New: " << testLedger.header().seq << " " << to_string(testLedger.header().hash);
    }

    return ret;
}

std::set<uint256>
getEnabledAmendments(ReadView const& view)
{
    std::set<uint256> amendments;

    if (auto const sle = view.read(keylet::amendments()))
    {
        if (sle->isFieldPresent(sfAmendments))
        {
            auto const& v = sle->getFieldV256(sfAmendments);
            amendments.insert(v.begin(), v.end());
        }
    }

    return amendments;
}

majorityAmendments_t
getMajorityAmendments(ReadView const& view)
{
    majorityAmendments_t ret;

    if (auto const sle = view.read(keylet::amendments()))
    {
        if (sle->isFieldPresent(sfMajorities))
        {
            using tp = NetClock::time_point;
            using d = tp::duration;

            auto const majorities = sle->getFieldArray(sfMajorities);

            for (auto const& m : majorities)
                ret[m.getFieldH256(sfAmendment)] = tp(d(m.getFieldU32(sfCloseTime)));
        }
    }

    return ret;
}

std::optional<uint256>
hashOfSeq(ReadView const& ledger, LedgerIndex seq, beast::Journal journal)
{
    // Easy cases...
    if (seq > ledger.seq())
    {
        JLOG(journal.warn()) << "Can't get seq " << seq << " from " << ledger.seq() << " future";
        return std::nullopt;
    }
    if (seq == ledger.seq())
        return ledger.header().hash;
    if (seq == (ledger.seq() - 1))
        return ledger.header().parentHash;

    if (int const diff = ledger.seq() - seq; diff <= 256)
    {
        // Within 256...
        auto const hashIndex = ledger.read(keylet::skip());
        if (hashIndex)
        {
            XRPL_ASSERT(
                hashIndex->getFieldU32(sfLastLedgerSequence) == (ledger.seq() - 1),
                "xrpl::hashOfSeq : matching ledger sequence");
            STVector256 vec = hashIndex->getFieldV256(sfHashes);
            if (vec.size() >= diff)
                return vec[vec.size() - diff];
            JLOG(journal.warn()) << "Ledger " << ledger.seq() << " missing hash for " << seq << " ("
                                 << vec.size() << "," << diff << ")";
        }
        else
        {
            JLOG(journal.warn()) << "Ledger " << ledger.seq() << ":" << ledger.header().hash
                                 << " missing normal list";
        }
    }

    if ((seq & 0xff) != 0)
    {
        JLOG(journal.debug()) << "Can't get seq " << seq << " from " << ledger.seq() << " past";
        return std::nullopt;
    }

    // in skiplist
    auto const hashIndex = ledger.read(keylet::skip(seq));
    if (hashIndex)
    {
        auto const lastSeq = hashIndex->getFieldU32(sfLastLedgerSequence);
        XRPL_ASSERT(lastSeq >= seq, "xrpl::hashOfSeq : minimum last ledger");
        XRPL_ASSERT((lastSeq & 0xff) == 0, "xrpl::hashOfSeq : valid last ledger");
        auto const diff = (lastSeq - seq) >> 8;
        STVector256 vec = hashIndex->getFieldV256(sfHashes);
        if (vec.size() > diff)
            return vec[vec.size() - diff - 1];
    }
    JLOG(journal.warn()) << "Can't get seq " << seq << " from " << ledger.seq() << " error";
    return std::nullopt;
}

//------------------------------------------------------------------------------
//
// Modifiers
//
//------------------------------------------------------------------------------

TER
dirLink(
    ApplyView& view,
    AccountID const& owner,
    std::shared_ptr<SLE>& object,
    SF_UINT64 const& node)
{
    auto const page =
        view.dirInsert(keylet::ownerDir(owner), object->key(), describeOwnerDir(owner));
    if (!page)
        return tecDIR_FULL;  // LCOV_EXCL_LINE
    object->setFieldU64(node, *page);
    return tesSUCCESS;
}

/*
 * Checks if a withdrawal amount into the destination account exceeds
 * any applicable receiving limit.
 * Called by VaultWithdraw and LoanBrokerCoverWithdraw.
 *
 * IOU : Performs the trustline check against the destination account's
 * credit limit to ensure the account's trust maximum is not exceeded.
 *
 * MPT: The limit check is effectively skipped (returns true). This is
 * because MPT MaximumAmount relates to token supply, and withdrawal does not
 * involve minting new tokens that could exceed the global cap.
 * On withdrawal, tokens are simply transferred from the vault's pseudo-account
 * to the destination account. Since no new MPT tokens are minted during this
 * transfer, the withdrawal cannot violate the MPT MaximumAmount/supply cap
 * even if `from` is the issuer.
 */
static TER
withdrawToDestExceedsLimit(
    ReadView const& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount)
{
    auto const& issuer = amount.getIssuer();
    if (from == to || to == issuer || isXRP(issuer))
        return tesSUCCESS;

    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) -> TER {
            if constexpr (std::is_same_v<TIss, Issue>)
            {
                auto const& currency = issue.currency;
                auto const owed = creditBalance(view, to, issuer, currency);
                if (owed <= beast::zero)
                {
                    auto const limit = creditLimit(view, to, issuer, currency);
                    if (-owed >= limit || amount > (limit + owed))
                        return tecNO_LINE;
                }
            }
            return tesSUCCESS;
        },
        amount.asset().value());
}

[[nodiscard]] TER
canWithdraw(
    ReadView const& view,
    AccountID const& from,
    AccountID const& to,
    SLE::const_ref toSle,
    STAmount const& amount,
    bool hasDestinationTag)
{
    if (auto const ret = checkDestinationAndTag(toSle, hasDestinationTag))
        return ret;

    if (from == to)
        return tesSUCCESS;

    if (toSle->isFlag(lsfDepositAuth))
    {
        if (!view.exists(keylet::depositPreauth(to, from)))
            return tecNO_PERMISSION;
    }

    return withdrawToDestExceedsLimit(view, from, to, amount);
}

[[nodiscard]] TER
canWithdraw(
    ReadView const& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    bool hasDestinationTag)
{
    auto const toSle = view.read(keylet::account(to));

    return canWithdraw(view, from, to, toSle, amount, hasDestinationTag);
}

[[nodiscard]] TER
canWithdraw(ReadView const& view, STTx const& tx)
{
    auto const from = tx[sfAccount];
    auto const to = tx[~sfDestination].value_or(from);

    return canWithdraw(view, from, to, tx[sfAmount], tx.isFieldPresent(sfDestinationTag));
}

TER
doWithdraw(
    ApplyView& view,
    STTx const& tx,
    AccountID const& senderAcct,
    AccountID const& dstAcct,
    AccountID const& sourceAcct,
    XRPAmount priorBalance,
    STAmount const& amount,
    beast::Journal j)
{
    // Create trust line or MPToken for the receiving account
    if (dstAcct == senderAcct)
    {
        if (auto const ter = addEmptyHolding(view, senderAcct, priorBalance, amount.asset(), j);
            !isTesSuccess(ter) && ter != tecDUPLICATE)
            return ter;
    }
    else
    {
        auto dstSle = view.read(keylet::account(dstAcct));
        if (auto err = verifyDepositPreauth(tx, view, senderAcct, dstAcct, dstSle, j))
            return err;
    }

    // Sanity check
    if (accountHolds(
            view,
            sourceAcct,
            amount.asset(),
            FreezeHandling::fhIGNORE_FREEZE,
            AuthHandling::ahIGNORE_AUTH,
            j) < amount)
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "doWithdraw: negative balance of broker cover assets.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    // Move the funds directly from the broker's pseudo-account to the
    // dstAcct
    return accountSend(view, sourceAcct, dstAcct, amount, j, WaiveTransferFee::Yes);
}

// Direct send w/o fees:
// - Redeeming IOUs and/or sending sender's own IOUs.
// - Create trust line if needed.
// --> bCheckIssuer : normally require issuer to be involved.
static TER
rippleCreditIOU(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    bool bCheckIssuer,
    beast::Journal j)
{
    AccountID const& issuer = saAmount.getIssuer();
    Currency const& currency = saAmount.getCurrency();

    // Make sure issuer is involved.
    XRPL_ASSERT(
        !bCheckIssuer || uSenderID == issuer || uReceiverID == issuer,
        "xrpl::rippleCreditIOU : matching issuer or don't care");
    (void)issuer;

    // Disallow sending to self.
    XRPL_ASSERT(uSenderID != uReceiverID, "xrpl::rippleCreditIOU : sender is not receiver");

    bool const bSenderHigh = uSenderID > uReceiverID;
    auto const index = keylet::line(uSenderID, uReceiverID, currency);

    XRPL_ASSERT(
        !isXRP(uSenderID) && uSenderID != noAccount(), "xrpl::rippleCreditIOU : sender is not XRP");
    XRPL_ASSERT(
        !isXRP(uReceiverID) && uReceiverID != noAccount(),
        "xrpl::rippleCreditIOU : receiver is not XRP");

    // If the line exists, modify it accordingly.
    if (auto const sleRippleState = view.peek(index))
    {
        STAmount saBalance = sleRippleState->getFieldAmount(sfBalance);

        if (bSenderHigh)
            saBalance.negate();  // Put balance in sender terms.

        view.creditHook(uSenderID, uReceiverID, saAmount, saBalance);

        STAmount const saBefore = saBalance;

        saBalance -= saAmount;

        JLOG(j.trace()) << "rippleCreditIOU: " << to_string(uSenderID) << " -> "
                        << to_string(uReceiverID) << " : before=" << saBefore.getFullText()
                        << " amount=" << saAmount.getFullText()
                        << " after=" << saBalance.getFullText();

        std::uint32_t const uFlags(sleRippleState->getFieldU32(sfFlags));
        bool bDelete = false;

        // FIXME This NEEDS to be cleaned up and simplified. It's impossible
        //       for anyone to understand.
        if (saBefore > beast::zero
            // Sender balance was positive.
            && saBalance <= beast::zero
            // Sender is zero or negative.
            && (uFlags & (!bSenderHigh ? lsfLowReserve : lsfHighReserve))
            // Sender reserve is set.
            && static_cast<bool>(uFlags & (!bSenderHigh ? lsfLowNoRipple : lsfHighNoRipple)) !=
                static_cast<bool>(
                    view.read(keylet::account(uSenderID))->getFlags() & lsfDefaultRipple) &&
            !(uFlags & (!bSenderHigh ? lsfLowFreeze : lsfHighFreeze)) &&
            !sleRippleState->getFieldAmount(!bSenderHigh ? sfLowLimit : sfHighLimit)
            // Sender trust limit is 0.
            && !sleRippleState->getFieldU32(!bSenderHigh ? sfLowQualityIn : sfHighQualityIn)
            // Sender quality in is 0.
            && !sleRippleState->getFieldU32(!bSenderHigh ? sfLowQualityOut : sfHighQualityOut))
        // Sender quality out is 0.
        {
            // Clear the reserve of the sender, possibly delete the line!
            adjustOwnerCount(view, view.peek(keylet::account(uSenderID)), -1, j);

            // Clear reserve flag.
            sleRippleState->setFieldU32(
                sfFlags, uFlags & (!bSenderHigh ? ~lsfLowReserve : ~lsfHighReserve));

            // Balance is zero, receiver reserve is clear.
            bDelete = !saBalance  // Balance is zero.
                && !(uFlags & (bSenderHigh ? lsfLowReserve : lsfHighReserve));
            // Receiver reserve is clear.
        }

        if (bSenderHigh)
            saBalance.negate();

        // Want to reflect balance to zero even if we are deleting line.
        sleRippleState->setFieldAmount(sfBalance, saBalance);
        // ONLY: Adjust ripple balance.

        if (bDelete)
        {
            return trustDelete(
                view,
                sleRippleState,
                bSenderHigh ? uReceiverID : uSenderID,
                !bSenderHigh ? uReceiverID : uSenderID,
                j);
        }

        view.update(sleRippleState);
        return tesSUCCESS;
    }

    STAmount const saReceiverLimit(Issue{currency, uReceiverID});
    STAmount saBalance{saAmount};

    saBalance.setIssuer(noAccount());

    JLOG(j.debug()) << "rippleCreditIOU: "
                       "create line: "
                    << to_string(uSenderID) << " -> " << to_string(uReceiverID) << " : "
                    << saAmount.getFullText();

    auto const sleAccount = view.peek(keylet::account(uReceiverID));
    if (!sleAccount)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    bool const noRipple = (sleAccount->getFlags() & lsfDefaultRipple) == 0;

    return trustCreate(
        view,
        bSenderHigh,
        uSenderID,
        uReceiverID,
        index.key,
        sleAccount,
        false,
        noRipple,
        false,
        false,
        saBalance,
        saReceiverLimit,
        0,
        0,
        j);
}

// Send regardless of limits.
// --> saAmount: Amount/currency/issuer to deliver to receiver.
// <-- saActual: Amount actually cost.  Sender pays fees.
static TER
rippleSendIOU(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    STAmount& saActual,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    auto const& issuer = saAmount.getIssuer();

    XRPL_ASSERT(
        !isXRP(uSenderID) && !isXRP(uReceiverID),
        "xrpl::rippleSendIOU : neither sender nor receiver is XRP");
    XRPL_ASSERT(uSenderID != uReceiverID, "xrpl::rippleSendIOU : sender is not receiver");

    if (uSenderID == issuer || uReceiverID == issuer || issuer == noAccount())
    {
        // Direct send: redeeming IOUs and/or sending own IOUs.
        auto const ter = rippleCreditIOU(view, uSenderID, uReceiverID, saAmount, false, j);
        if (ter != tesSUCCESS)
            return ter;
        saActual = saAmount;
        return tesSUCCESS;
    }

    // Sending 3rd party IOUs: transit.

    // Calculate the amount to transfer accounting
    // for any transfer fees if the fee is not waived:
    saActual = (waiveFee == WaiveTransferFee::Yes) ? saAmount
                                                   : multiply(saAmount, transferRate(view, issuer));

    JLOG(j.debug()) << "rippleSendIOU> " << to_string(uSenderID) << " - > "
                    << to_string(uReceiverID) << " : deliver=" << saAmount.getFullText()
                    << " cost=" << saActual.getFullText();

    TER terResult = rippleCreditIOU(view, issuer, uReceiverID, saAmount, true, j);

    if (tesSUCCESS == terResult)
        terResult = rippleCreditIOU(view, uSenderID, issuer, saActual, true, j);

    return terResult;
}

template <class TAsset>
static TER
doSendMulti(
    std::string const& name,
    ApplyView& view,
    AccountID const& senderID,
    TAsset const& issue,
    MultiplePaymentDestinations const& receivers,
    STAmount& actual,
    beast::Journal j,
    WaiveTransferFee waiveFee,
    // Don't pass back parameters that the caller already has
    std::function<
        TER(AccountID const& senderID,
            AccountID const& receiverID,
            STAmount const& amount,
            bool checkIssuer)> doCredit,
    std::function<
        TER(AccountID const& issuer, STAmount const& takeFromSender, STAmount const& amount)>
        preMint = {})
{
    // Use the same pattern for all the SendMulti functions to help avoid
    // divergence and copy/paste errors.
    auto const& issuer = issue.getIssuer();

    // These values may not stay in sync
    STAmount takeFromSender{issue};
    actual = takeFromSender;

    // Failures return immediately.
    for (auto const& r : receivers)
    {
        auto const& receiverID = r.first;
        STAmount amount{issue, r.second};

        if (amount < beast::zero)
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }

        /* If we aren't sending anything or if the sender is the same as the
         * receiver then we don't need to do anything.
         */
        if (!amount || (senderID == receiverID))
            continue;

        using namespace std::string_literals;
        XRPL_ASSERT(!isXRP(receiverID), ("xrpl::"s + name + " : receiver is not XRP").c_str());

        if (senderID == issuer || receiverID == issuer || issuer == noAccount())
        {
            if (preMint)
            {
                if (auto const ter = preMint(issuer, takeFromSender, amount))
                    return ter;
            }
            // Direct send: redeeming IOUs and/or sending own IOUs.
            if (auto const ter = doCredit(senderID, receiverID, amount, false))
                return ter;
            actual += amount;
            // Do not add amount to takeFromSender, because doCredit took
            // it.

            continue;
        }

        // Sending 3rd party: transit.

        // Calculate the amount to transfer accounting
        // for any transfer fees if the fee is not waived:
        STAmount actualSend = (waiveFee == WaiveTransferFee::Yes || issue.native())
            ? amount
            : multiply(amount, transferRate(view, amount));
        actual += actualSend;
        takeFromSender += actualSend;

        JLOG(j.debug()) << name << "> " << to_string(senderID) << " - > " << to_string(receiverID)
                        << " : deliver=" << amount.getFullText()
                        << " cost=" << actualSend.getFullText();

        if (TER const terResult = doCredit(issuer, receiverID, amount, true))
            return terResult;
    }

    if (senderID != issuer && takeFromSender)
    {
        if (TER const terResult = doCredit(senderID, issuer, takeFromSender, true))
            return terResult;
    }

    return tesSUCCESS;
}

// Send regardless of limits.
// --> receivers: Amount/currency/issuer to deliver to receivers.
// <-- saActual: Amount actually cost to sender.  Sender pays fees.
static TER
rippleSendMultiIOU(
    ApplyView& view,
    AccountID const& senderID,
    Issue const& issue,
    MultiplePaymentDestinations const& receivers,
    STAmount& actual,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    XRPL_ASSERT(!isXRP(senderID), "xrpl::rippleSendMultiIOU : sender is not XRP");

    auto doCredit = [&view, j](
                        AccountID const& senderID,
                        AccountID const& receiverID,
                        STAmount const& amount,
                        bool checkIssuer) {
        return rippleCreditIOU(view, senderID, receiverID, amount, checkIssuer, j);
    };

    return doSendMulti(
        "rippleSendMultiIOU", view, senderID, issue, receivers, actual, j, waiveFee, doCredit);
}

static TER
rippleCreditMPT(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    beast::Journal j)
{
    // Do not check MPT authorization here - it must have been checked earlier
    auto const mptID = keylet::mptIssuance(saAmount.get<MPTIssue>().getMptID());
    auto const& issuer = saAmount.getIssuer();
    auto sleIssuance = view.peek(mptID);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;
    if (uSenderID == issuer)
    {
        (*sleIssuance)[sfOutstandingAmount] += saAmount.mpt().value();
        view.update(sleIssuance);
    }
    else
    {
        auto const mptokenID = keylet::mptoken(mptID.key, uSenderID);
        if (auto sle = view.peek(mptokenID))
        {
            auto const amt = sle->getFieldU64(sfMPTAmount);
            auto const pay = saAmount.mpt().value();
            if (amt < pay)
                return tecINSUFFICIENT_FUNDS;
            (*sle)[sfMPTAmount] = amt - pay;
            view.update(sle);
        }
        else
            return tecNO_AUTH;
    }

    if (uReceiverID == issuer)
    {
        auto const outstanding = sleIssuance->getFieldU64(sfOutstandingAmount);
        auto const redeem = saAmount.mpt().value();
        if (outstanding >= redeem)
        {
            sleIssuance->setFieldU64(sfOutstandingAmount, outstanding - redeem);
            view.update(sleIssuance);
        }
        else
            return tecINTERNAL;  // LCOV_EXCL_LINE
    }
    else
    {
        auto const mptokenID = keylet::mptoken(mptID.key, uReceiverID);
        if (auto sle = view.peek(mptokenID))
        {
            (*sle)[sfMPTAmount] += saAmount.mpt().value();
            view.update(sle);
        }
        else
            return tecNO_AUTH;
    }

    return tesSUCCESS;
}

static TER
rippleSendMPT(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    STAmount& saActual,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    XRPL_ASSERT(uSenderID != uReceiverID, "xrpl::rippleSendMPT : sender is not receiver");

    // Safe to get MPT since rippleSendMPT is only called by accountSendMPT
    auto const& issuer = saAmount.getIssuer();

    auto const sle = view.read(keylet::mptIssuance(saAmount.get<MPTIssue>().getMptID()));
    if (!sle)
        return tecOBJECT_NOT_FOUND;

    if (uSenderID == issuer || uReceiverID == issuer)
    {
        // if sender is issuer, check that the new OutstandingAmount will not
        // exceed MaximumAmount
        if (uSenderID == issuer)
        {
            auto const sendAmount = saAmount.mpt().value();
            auto const maximumAmount = sle->at(~sfMaximumAmount).value_or(maxMPTokenAmount);
            if (sendAmount > maximumAmount ||
                sle->getFieldU64(sfOutstandingAmount) > maximumAmount - sendAmount)
                return tecPATH_DRY;
        }

        // Direct send: redeeming MPTs and/or sending own MPTs.
        auto const ter = rippleCreditMPT(view, uSenderID, uReceiverID, saAmount, j);
        if (ter != tesSUCCESS)
            return ter;
        saActual = saAmount;
        return tesSUCCESS;
    }

    // Sending 3rd party MPTs: transit.
    saActual = (waiveFee == WaiveTransferFee::Yes)
        ? saAmount
        : multiply(saAmount, transferRate(view, saAmount.get<MPTIssue>().getMptID()));

    JLOG(j.debug()) << "rippleSendMPT> " << to_string(uSenderID) << " - > "
                    << to_string(uReceiverID) << " : deliver=" << saAmount.getFullText()
                    << " cost=" << saActual.getFullText();

    if (auto const terResult = rippleCreditMPT(view, issuer, uReceiverID, saAmount, j);
        terResult != tesSUCCESS)
        return terResult;

    return rippleCreditMPT(view, uSenderID, issuer, saActual, j);
}

static TER
rippleSendMultiMPT(
    ApplyView& view,
    AccountID const& senderID,
    MPTIssue const& mptIssue,
    MultiplePaymentDestinations const& receivers,
    STAmount& actual,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    auto const sle = view.read(keylet::mptIssuance(mptIssue.getMptID()));
    if (!sle)
        return tecOBJECT_NOT_FOUND;

    auto preMint = [&](AccountID const& issuer,
                       STAmount const& takeFromSender,
                       STAmount const& amount) -> TER {
        // if sender is issuer, check that the new OutstandingAmount will
        // not exceed MaximumAmount
        if (senderID == issuer)
        {
            XRPL_ASSERT_PARTS(
                takeFromSender == beast::zero,
                "rippler::rippleSendMultiMPT",
                "sender == issuer, takeFromSender == zero");
            auto const sendAmount = amount.mpt().value();
            auto const maximumAmount = sle->at(~sfMaximumAmount).value_or(maxMPTokenAmount);
            if (sendAmount > maximumAmount ||
                sle->getFieldU64(sfOutstandingAmount) > maximumAmount - sendAmount)
                return tecPATH_DRY;
        }

        return tesSUCCESS;
    };
    auto doCredit =
        [&view, j](
            AccountID const& senderID, AccountID const& receiverID, STAmount const& amount, bool) {
            return rippleCreditMPT(view, senderID, receiverID, amount, j);
        };

    return doSendMulti(
        "rippleSendMultiMPT",
        view,
        senderID,
        mptIssue,
        receivers,
        actual,
        j,
        waiveFee,
        doCredit,
        preMint);
}

TER
cleanupOnAccountDelete(
    ApplyView& view,
    Keylet const& ownerDirKeylet,
    EntryDeleter const& deleter,
    beast::Journal j,
    std::optional<uint16_t> maxNodesToDelete)
{
    // Delete all the entries in the account directory.
    std::shared_ptr<SLE> sleDirNode{};
    unsigned int uDirEntry{0};
    uint256 dirEntry{beast::zero};
    std::uint32_t deleted = 0;

    if (view.exists(ownerDirKeylet) &&
        dirFirst(view, ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry))
    {
        do
        {
            if (maxNodesToDelete && ++deleted > *maxNodesToDelete)
                return tecINCOMPLETE;

            // Choose the right way to delete each directory node.
            auto sleItem = view.peek(keylet::child(dirEntry));
            if (!sleItem)
            {
                // Directory node has an invalid index.  Bail out.
                // LCOV_EXCL_START
                JLOG(j.fatal()) << "DeleteAccount: Directory node in ledger " << view.seq()
                                << " has index to object that is missing: " << to_string(dirEntry);
                return tefBAD_LEDGER;
                // LCOV_EXCL_STOP
            }

            LedgerEntryType const nodeType{
                safe_cast<LedgerEntryType>(sleItem->getFieldU16(sfLedgerEntryType))};

            // Deleter handles the details of specific account-owned object
            // deletion
            auto const [ter, skipEntry] = deleter(nodeType, dirEntry, sleItem);
            if (!isTesSuccess(ter))
                return ter;

            // dirFirst() and dirNext() are like iterators with exposed
            // internal state.  We'll take advantage of that exposed state
            // to solve a common C++ problem: iterator invalidation while
            // deleting elements from a container.
            //
            // We have just deleted one directory entry, which means our
            // "iterator state" is invalid.
            //
            //  1. During the process of getting an entry from the
            //     directory uDirEntry was incremented from 'it' to 'it'+1.
            //
            //  2. We then deleted the entry at index 'it', which means the
            //     entry that was at 'it'+1 has now moved to 'it'.
            //
            //  3. So we verify that uDirEntry is indeed 'it'+1.  Then we jam it
            //     back to 'it' to "un-invalidate" the iterator.
            XRPL_ASSERT(uDirEntry >= 1, "xrpl::cleanupOnAccountDelete : minimum dir entries");
            if (uDirEntry == 0)
            {
                // LCOV_EXCL_START
                JLOG(j.error()) << "DeleteAccount iterator re-validation failed.";
                return tefBAD_LEDGER;
                // LCOV_EXCL_STOP
            }
            if (skipEntry == SkipEntry::No)
                uDirEntry--;

        } while (dirNext(view, ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry));
    }

    return tesSUCCESS;
}

bool
after(NetClock::time_point now, std::uint32_t mark)
{
    return now.time_since_epoch().count() > mark;
}

}  // namespace xrpl
