//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/basics/Log.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/contract.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/st.h>
#include <cassert>
#include <optional>

namespace ripple {

namespace detail {

template <
    class V,
    class N,
    class = std::enable_if_t<
        std::is_same_v<std::remove_cv_t<N>, SLE> &&
        std::is_base_of_v<ReadView, V>>>
bool
internalDirNext(
    V& view,
    uint256 const& root,
    std::shared_ptr<N>& page,
    unsigned int& index,
    uint256& entry)
{
    auto const& svIndexes = page->getFieldV256(sfIndexes);
    assert(index <= svIndexes.size());

    if (index >= svIndexes.size())
    {
        auto const next = page->getFieldU64(sfIndexNext);

        if (!next)
        {
            entry.zero();
            return false;
        }

        if constexpr (std::is_const_v<N>)
            page = view.read(keylet::page(root, next));
        else
            page = view.peek(keylet::page(root, next));

        assert(page);

        if (!page)
            return false;

        index = 0;

        return internalDirNext(view, root, page, index, entry);
    }

    entry = svIndexes[index++];
    return true;
}

template <
    class V,
    class N,
    class = std::enable_if_t<
        std::is_same_v<std::remove_cv_t<N>, SLE> &&
        std::is_base_of_v<ReadView, V>>>
bool
internalDirFirst(
    V& view,
    uint256 const& root,
    std::shared_ptr<N>& page,
    unsigned int& index,
    uint256& entry)
{
    if constexpr (std::is_const_v<N>)
        page = view.read(keylet::page(root));
    else
        page = view.peek(keylet::page(root));

    if (!page)
        return false;

    index = 0;

    return internalDirNext(view, root, page, index, entry);
}

}  // namespace detail

bool
dirFirst(
    ApplyView& view,
    uint256 const& root,
    std::shared_ptr<SLE>& page,
    unsigned int& index,
    uint256& entry)
{
    return detail::internalDirFirst(view, root, page, index, entry);
}

bool
dirNext(
    ApplyView& view,
    uint256 const& root,
    std::shared_ptr<SLE>& page,
    unsigned int& index,
    uint256& entry)
{
    return detail::internalDirNext(view, root, page, index, entry);
}

bool
cdirFirst(
    ReadView const& view,
    uint256 const& root,
    std::shared_ptr<SLE const>& page,
    unsigned int& index,
    uint256& entry)
{
    return detail::internalDirFirst(view, root, page, index, entry);
}

bool
cdirNext(
    ReadView const& view,
    uint256 const& root,
    std::shared_ptr<SLE const>& page,
    unsigned int& index,
    uint256& entry)
{
    return detail::internalDirNext(view, root, page, index, entry);
}

//------------------------------------------------------------------------------
//
// Observers
//
//------------------------------------------------------------------------------

void
addRaw(LedgerInfo const& info, Serializer& s, bool includeHash)
{
    s.add32(info.seq);
    s.add64(info.drops.drops());
    s.addBitString(info.parentHash);
    s.addBitString(info.txHash);
    s.addBitString(info.accountHash);
    s.add32(info.parentCloseTime.time_since_epoch().count());
    s.add32(info.closeTime.time_since_epoch().count());
    s.add8(info.closeTimeResolution.count());
    s.add8(info.closeFlags);

    if (includeHash)
        s.addBitString(info.hash);
}

bool
hasExpired(ReadView const& view, std::optional<std::uint32_t> const& exp)
{
    using d = NetClock::duration;
    using tp = NetClock::time_point;

    return exp && (view.parentCloseTime() >= tp{d{*exp}});
}

STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer,
    beast::Journal j)
{
    return {xrpLiquid(view, account, 0, j)};
}

STAmount
accountFunds(
    ReadView const& view,
    AccountID const& id,
    STAmount const& saDefault,
    beast::Journal j)
{
    return accountHolds(
        view, id, saDefault.getCurrency(), saDefault.getIssuer(), j);
}

// Prevent ownerCount from wrapping under error conditions.
//
// adjustment allows the ownerCount to be adjusted up or down in multiple steps.
// If id != std::nullopt, then do error reporting.
//
// Returns adjusted owner count.
static std::uint32_t
confineOwnerCount(
    std::uint32_t current,
    std::int32_t adjustment,
    std::optional<AccountID> const& id = std::nullopt,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
{
    std::uint32_t adjusted{current + adjustment};
    if (adjustment > 0)
    {
        // Overflow is well defined on unsigned
        if (adjusted < current)
        {
            if (id)
            {
                JLOG(j.fatal())
                    << "Account " << *id << " owner count exceeds max!";
            }
            adjusted = std::numeric_limits<std::uint32_t>::max();
        }
    }
    else
    {
        // Underflow is well defined on unsigned
        if (adjusted > current)
        {
            if (id)
            {
                JLOG(j.fatal())
                    << "Account " << *id << " owner count set below 0!";
            }
            adjusted = 0;
            assert(!id);
        }
    }
    return adjusted;
}

XRPAmount
xrpLiquid(
    ReadView const& view,
    AccountID const& id,
    std::int32_t ownerCountAdj,
    beast::Journal j)
{
    auto const sle = view.read(keylet::account(id));
    if (sle == nullptr)
        return beast::zero;

    // Return balance minus reserve
    std::uint32_t const ownerCount = confineOwnerCount(
        view.ownerCountHook(id, sle->getFieldU32(sfOwnerCount)), ownerCountAdj);

    auto const reserve = view.fees().accountReserve(ownerCount);

    auto const fullBalance = sle->getFieldAmount(sfBalance);

    auto const balance = view.balanceHook(id, xrpAccount(), fullBalance);

    STAmount amount = balance - reserve;
    if (balance < reserve)
        amount.clear();

    JLOG(j.trace()) << "accountHolds:"
                    << " account=" << to_string(id)
                    << " amount=" << amount.getFullText()
                    << " fullBalance=" << fullBalance.getFullText()
                    << " balance=" << balance.getFullText()
                    << " reserve=" << reserve << " ownerCount=" << ownerCount
                    << " ownerCountAdj=" << ownerCountAdj;

    return amount.xrp();
}

void
forEachItem(
    ReadView const& view,
    Keylet const& root,
    std::function<void(std::shared_ptr<SLE const> const&)> const& f)
{
    assert(root.type == ltDIR_NODE);

    if (root.type != ltDIR_NODE)
        return;

    auto pos = root;

    while (true)
    {
        auto sle = view.read(pos);
        if (!sle)
            return;
        for (auto const& key : sle->getFieldV256(sfIndexes))
            f(view.read(keylet::child(key)));
        auto const next = sle->getFieldU64(sfIndexNext);
        if (!next)
            return;
        pos = keylet::page(root, next);
    }
}

bool
forEachItemAfter(
    ReadView const& view,
    Keylet const& root,
    uint256 const& after,
    std::uint64_t const hint,
    unsigned int limit,
    std::function<bool(std::shared_ptr<SLE const> const&)> const& f)
{
    assert(root.type == ltDIR_NODE);

    if (root.type != ltDIR_NODE)
        return false;

    auto currentIndex = root;

    // If startAfter is not zero try jumping to that page using the hint
    if (after.isNonZero())
    {
        auto const hintIndex = keylet::page(root, hint);

        if (auto hintDir = view.read(hintIndex))
        {
            for (auto const& key : hintDir->getFieldV256(sfIndexes))
            {
                if (key == after)
                {
                    // We found the hint, we can start here
                    currentIndex = hintIndex;
                    break;
                }
            }
        }

        bool found = false;
        for (;;)
        {
            auto const ownerDir = view.read(currentIndex);
            if (!ownerDir)
                return found;
            for (auto const& key : ownerDir->getFieldV256(sfIndexes))
            {
                if (!found)
                {
                    if (key == after)
                        found = true;
                }
                else if (f(view.read(keylet::child(key))) && limit-- <= 1)
                {
                    return found;
                }
            }

            auto const uNodeNext = ownerDir->getFieldU64(sfIndexNext);
            if (uNodeNext == 0)
                return found;
            currentIndex = keylet::page(root, uNodeNext);
        }
    }
    else
    {
        for (;;)
        {
            auto const ownerDir = view.read(currentIndex);
            if (!ownerDir)
                return true;
            for (auto const& key : ownerDir->getFieldV256(sfIndexes))
                if (f(view.read(keylet::child(key))) && limit-- <= 1)
                    return true;
            auto const uNodeNext = ownerDir->getFieldU64(sfIndexNext);
            if (uNodeNext == 0)
                return true;
            currentIndex = keylet::page(root, uNodeNext);
        }
    }
}

bool
areCompatible(
    ReadView const& validLedger,
    ReadView const& testLedger,
    beast::Journal::Stream& s,
    const char* reason)
{
    bool ret = true;

    if (validLedger.info().seq < testLedger.info().seq)
    {
        // valid -> ... -> test
        auto hash = hashOfSeq(
            testLedger,
            validLedger.info().seq,
            beast::Journal{beast::Journal::getNullSink()});
        if (hash && (*hash != validLedger.info().hash))
        {
            JLOG(s) << reason << " incompatible with valid ledger";

            JLOG(s) << "Hash(VSeq): " << to_string(*hash);

            ret = false;
        }
    }
    else if (validLedger.info().seq > testLedger.info().seq)
    {
        // test -> ... -> valid
        auto hash = hashOfSeq(
            validLedger,
            testLedger.info().seq,
            beast::Journal{beast::Journal::getNullSink()});
        if (hash && (*hash != testLedger.info().hash))
        {
            JLOG(s) << reason << " incompatible preceding ledger";

            JLOG(s) << "Hash(NSeq): " << to_string(*hash);

            ret = false;
        }
    }
    else if (
        (validLedger.info().seq == testLedger.info().seq) &&
        (validLedger.info().hash != testLedger.info().hash))
    {
        // Same sequence number, different hash
        JLOG(s) << reason << " incompatible ledger";

        ret = false;
    }

    if (!ret)
    {
        JLOG(s) << "Val: " << validLedger.info().seq << " "
                << to_string(validLedger.info().hash);

        JLOG(s) << "New: " << testLedger.info().seq << " "
                << to_string(testLedger.info().hash);
    }

    return ret;
}

bool
areCompatible(
    uint256 const& validHash,
    LedgerIndex validIndex,
    ReadView const& testLedger,
    beast::Journal::Stream& s,
    const char* reason)
{
    bool ret = true;

    if (testLedger.info().seq > validIndex)
    {
        // Ledger we are testing follows last valid ledger
        auto hash = hashOfSeq(
            testLedger,
            validIndex,
            beast::Journal{beast::Journal::getNullSink()});
        if (hash && (*hash != validHash))
        {
            JLOG(s) << reason << " incompatible following ledger";
            JLOG(s) << "Hash(VSeq): " << to_string(*hash);

            ret = false;
        }
    }
    else if (
        (validIndex == testLedger.info().seq) &&
        (testLedger.info().hash != validHash))
    {
        JLOG(s) << reason << " incompatible ledger";

        ret = false;
    }

    if (!ret)
    {
        JLOG(s) << "Val: " << validIndex << " " << to_string(validHash);

        JLOG(s) << "New: " << testLedger.info().seq << " "
                << to_string(testLedger.info().hash);
    }

    return ret;
}

bool
dirIsEmpty(ReadView const& view, Keylet const& k)
{
    auto const sleNode = view.read(k);
    if (!sleNode)
        return true;
    if (!sleNode->getFieldV256(sfIndexes).empty())
        return false;
    // The first page of a directory may legitimately be empty even if there
    // are other pages (the first page is the anchor page) so check to see if
    // there is another page. If there is, the directory isn't empty.
    return sleNode->getFieldU64(sfIndexNext) == 0;
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
                ret[m.getFieldH256(sfAmendment)] =
                    tp(d(m.getFieldU32(sfCloseTime)));
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
        JLOG(journal.warn())
            << "Can't get seq " << seq << " from " << ledger.seq() << " future";
        return std::nullopt;
    }
    if (seq == ledger.seq())
        return ledger.info().hash;
    if (seq == (ledger.seq() - 1))
        return ledger.info().parentHash;

    if (int diff = ledger.seq() - seq; diff <= 256)
    {
        // Within 256...
        auto const hashIndex = ledger.read(keylet::skip());
        if (hashIndex)
        {
            assert(
                hashIndex->getFieldU32(sfLastLedgerSequence) ==
                (ledger.seq() - 1));
            STVector256 vec = hashIndex->getFieldV256(sfHashes);
            if (vec.size() >= diff)
                return vec[vec.size() - diff];
            JLOG(journal.warn())
                << "Ledger " << ledger.seq() << " missing hash for " << seq
                << " (" << vec.size() << "," << diff << ")";
        }
        else
        {
            JLOG(journal.warn())
                << "Ledger " << ledger.seq() << ":" << ledger.info().hash
                << " missing normal list";
        }
    }

    if ((seq & 0xff) != 0)
    {
        JLOG(journal.debug())
            << "Can't get seq " << seq << " from " << ledger.seq() << " past";
        return std::nullopt;
    }

    // in skiplist
    auto const hashIndex = ledger.read(keylet::skip(seq));
    if (hashIndex)
    {
        auto const lastSeq = hashIndex->getFieldU32(sfLastLedgerSequence);
        assert(lastSeq >= seq);
        assert((lastSeq & 0xff) == 0);
        auto const diff = (lastSeq - seq) >> 8;
        STVector256 vec = hashIndex->getFieldV256(sfHashes);
        if (vec.size() > diff)
            return vec[vec.size() - diff - 1];
    }
    JLOG(journal.warn()) << "Can't get seq " << seq << " from " << ledger.seq()
                         << " error";
    return std::nullopt;
}

//------------------------------------------------------------------------------
//
// Modifiers
//
//------------------------------------------------------------------------------

void
adjustOwnerCount(
    ApplyView& view,
    std::shared_ptr<SLE> const& sle,
    std::int32_t amount,
    beast::Journal j)
{
    if (!sle)
        return;
    assert(amount != 0);
    std::uint32_t const current{sle->getFieldU32(sfOwnerCount)};
    AccountID const id = (*sle)[sfAccount];
    std::uint32_t const adjusted = confineOwnerCount(current, amount, id, j);
    view.adjustOwnerCountHook(id, current, adjusted);
    sle->setFieldU32(sfOwnerCount, adjusted);
    view.update(sle);
}

std::function<void(SLE::ref)>
describeOwnerDir(AccountID const& account)
{
    return [&account](std::shared_ptr<SLE> const& sle) {
        (*sle)[sfOwner] = account;
    };
}

//// Send regardless of limits.
//// --> saAmount: Amount/currency/issuer to deliver to receiver.
//// <-- saActual: Amount actually cost.  Sender pays fees.
// static TER
// rippleSend(
//     ApplyView& view,
//     AccountID const& uSenderID,
//     AccountID const& uReceiverID,
//     STAmount const& saAmount,
//     STAmount& saActual,
//     beast::Journal j)
//{
//     auto const issuer = saAmount.getIssuer();
//
//     assert(!isXRP(uSenderID) && !isXRP(uReceiverID));
//     assert(uSenderID != uReceiverID);
//
//     if (uSenderID == issuer || uReceiverID == issuer || issuer ==
//     noAccount())
//     {
//         // Direct send: redeeming IOUs and/or sending own IOUs.
//         auto const ter =
//             rippleCredit(view, uSenderID, uReceiverID, saAmount, false, j);
//         if (view.rules().enabled(featureDeletableAccounts) && ter !=
//         tesSUCCESS)
//             return ter;
//         saActual = saAmount;
//         return tesSUCCESS;
//     }
//
//     // Sending 3rd party IOUs: transit.
//
//     // Calculate the amount to transfer accounting
//     // for any transfer fees:
//     saActual = multiply(saAmount, transferRate(view, issuer));
//
//     JLOG(j.debug()) << "rippleSend> " << to_string(uSenderID) << " - > "
//                     << to_string(uReceiverID)
//                     << " : deliver=" << saAmount.getFullText()
//                     << " cost=" << saActual.getFullText();
//
//     TER terResult = rippleCredit(view, issuer, uReceiverID, saAmount, true,
//     j);
//
//     if (tesSUCCESS == terResult)
//         terResult = rippleCredit(view, uSenderID, issuer, saActual, true, j);
//
//     return terResult;
// }

TER
accountSend(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    beast::Journal j)
{
    assert(saAmount >= beast::zero);

    /* If we aren't sending anything or if the sender is the same as the
     * receiver then we don't need to do anything.
     */
    if (!saAmount || (uSenderID == uReceiverID))
        return tesSUCCESS;

    //    if (!saAmount.native())
    //    {
    //        STAmount saActual;
    //
    //        JLOG(j.trace()) << "accountSend: " << to_string(uSenderID) << " ->
    //        "
    //                        << to_string(uReceiverID) << " : "
    //                        << saAmount.getFullText();
    //
    //        return rippleSend(view, uSenderID, uReceiverID, saAmount,
    //        saActual, j);
    //    }

    /* XRP send which does not check reserve and can do pure adjustment.
     * Note that sender or receiver may be null and this not a mistake; this
     * setup is used during pathfinding and it is carefully controlled to
     * ensure that transfers are balanced.
     */
    TER terResult(tesSUCCESS);

    SLE::pointer sender = uSenderID != beast::zero
        ? view.peek(keylet::account(uSenderID))
        : SLE::pointer();
    SLE::pointer receiver = uReceiverID != beast::zero
        ? view.peek(keylet::account(uReceiverID))
        : SLE::pointer();

    if (auto stream = j.trace())
    {
        std::string sender_bal("-");
        std::string receiver_bal("-");

        if (sender)
            sender_bal = sender->getFieldAmount(sfBalance).getFullText();

        if (receiver)
            receiver_bal = receiver->getFieldAmount(sfBalance).getFullText();

        stream << "accountSend> " << to_string(uSenderID) << " (" << sender_bal
               << ") -> " << to_string(uReceiverID) << " (" << receiver_bal
               << ") : " << saAmount.getFullText();
    }

    if (sender)
    {
        if (sender->getFieldAmount(sfBalance) < saAmount)
        {
            // VFALCO Its laborious to have to mutate the
            //        TER based on params everywhere
            terResult = view.open() ? TER{telFAILED_PROCESSING}
                                    : TER{tecFAILED_PROCESSING};
        }
        else
        {
            auto const sndBal = sender->getFieldAmount(sfBalance);
            view.creditHook(uSenderID, xrpAccount(), saAmount, sndBal);

            // Decrement XRP balance.
            sender->setFieldAmount(sfBalance, sndBal - saAmount);
            view.update(sender);
        }
    }

    if (tesSUCCESS == terResult && receiver)
    {
        // Increment XRP balance.
        auto const rcvBal = receiver->getFieldAmount(sfBalance);
        receiver->setFieldAmount(sfBalance, rcvBal + saAmount);
        view.creditHook(xrpAccount(), uReceiverID, saAmount, -rcvBal);

        view.update(receiver);
    }

    if (auto stream = j.trace())
    {
        std::string sender_bal("-");
        std::string receiver_bal("-");

        if (sender)
            sender_bal = sender->getFieldAmount(sfBalance).getFullText();

        if (receiver)
            receiver_bal = receiver->getFieldAmount(sfBalance).getFullText();

        stream << "accountSend< " << to_string(uSenderID) << " (" << sender_bal
               << ") -> " << to_string(uReceiverID) << " (" << receiver_bal
               << ") : " << saAmount.getFullText();
    }

    return terResult;
}

TER
transferXRP(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    beast::Journal j)
{
    assert(from != beast::zero);
    assert(to != beast::zero);
    assert(from != to);
    //    assert(amount.native());

    SLE::pointer const sender = view.peek(keylet::account(from));
    SLE::pointer const receiver = view.peek(keylet::account(to));
    if (!sender || !receiver)
        return tefINTERNAL;

    JLOG(j.trace()) << "transferXRP: " << to_string(from) << " -> "
                    << to_string(to) << ") : " << amount.getFullText();

    if (sender->getFieldAmount(sfBalance) < amount)
    {
        // VFALCO Its unfortunate we have to keep
        //        mutating these TER everywhere
        // FIXME: this logic should be moved to callers maybe?
        return view.open() ? TER{telFAILED_PROCESSING}
                           : TER{tecFAILED_PROCESSING};
    }

    // Decrement XRP balance.
    sender->setFieldAmount(
        sfBalance, sender->getFieldAmount(sfBalance) - amount);
    view.update(sender);

    receiver->setFieldAmount(
        sfBalance, receiver->getFieldAmount(sfBalance) + amount);
    view.update(receiver);

    return tesSUCCESS;
}

}  // namespace ripple
