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

    if (int diff = ledger.seq() - seq; diff <= 256)
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
