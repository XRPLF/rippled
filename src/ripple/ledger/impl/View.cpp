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
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/Quality.h>
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

bool
isGlobalFrozen(ReadView const& view, AccountID const& issuer)
{
    if (isXRP(issuer))
        return false;
    if (auto const sle = view.read(keylet::account(issuer)))
        return sle->isFlag(lsfGlobalFreeze);
    return false;
}

// Can the specified account spend the specified currency issued by
// the specified issuer or does the freeze flag prohibit it?
bool
isFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer)
{
    if (isXRP(currency))
        return false;
    auto sle = view.read(keylet::account(issuer));
    if (sle && sle->isFlag(lsfGlobalFreeze))
        return true;
    if (issuer != account)
    {
        // Check if the issuer froze the line
        sle = view.read(keylet::line(account, issuer, currency));
        if (sle &&
            sle->isFlag((issuer > account) ? lsfHighFreeze : lsfLowFreeze))
            return true;
    }
    return false;
}

STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer,
    FreezeHandling zeroIfFrozen,
    beast::Journal j)
{
    STAmount amount;
    if (isXRP(currency))
    {
        return {xrpLiquid(view, account, 0, j)};
    }

    // IOU: Return balance on trust line modulo freeze
    auto const sle = view.read(keylet::line(account, issuer, currency));
    if (!sle)
    {
        amount.clear({currency, issuer});
    }
    else if (
        (zeroIfFrozen == fhZERO_IF_FROZEN) &&
        isFrozen(view, account, currency, issuer))
    {
        amount.clear(Issue(currency, issuer));
    }
    else
    {
        amount = sle->getFieldAmount(sfBalance);
        if (account > issuer)
        {
            // Put balance in account terms.
            amount.negate();
        }

        // If tokens can be escrowed then they can be locked in the trustline
        // which means we must never spend them until the escrow is released.
        if (view.rules().enabled(featurePaychanAndEscrowForTokens) &&
            sle->isFieldPresent(sfLockedBalance))
        {
            STAmount lockedBalance = sle->getFieldAmount(sfLockedBalance);
            STAmount spendableBalance =
                amount - (account > issuer ? -lockedBalance : lockedBalance);

            // RH NOTE: this is defensively programmed, it should never fire
            // if something bad does happen the trustline acts as a frozen line.
            if (spendableBalance < beast::zero || spendableBalance > amount)
            {
                JLOG(j.error())
                    << "SpendableBalance has illegal value in accountHolds "
                    << spendableBalance;
                amount.clear(Issue{currency, issuer});
            }
            else
                amount = spendableBalance;
        }

        amount.setIssuer(issuer);
    }
    JLOG(j.trace()) << "accountHolds:"
                    << " account=" << to_string(account)
                    << " amount=" << amount.getFullText();

    return view.balanceHook(account, issuer, amount);
}

STAmount
accountFunds(
    ReadView const& view,
    AccountID const& id,
    STAmount const& saDefault,
    FreezeHandling freezeHandling,
    beast::Journal j)
{
    if (!saDefault.native() && saDefault.getIssuer() == id)
        return saDefault;

    return accountHolds(
        view,
        id,
        saDefault.getCurrency(),
        saDefault.getIssuer(),
        freezeHandling,
        j);
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

Rate
transferRate(ReadView const& view, AccountID const& issuer)
{
    auto const sle = view.read(keylet::account(issuer));

    if (sle && sle->isFieldPresent(sfTransferRate))
        return Rate{sle->getFieldU32(sfTransferRate)};

    return parityRate;
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

TER
trustCreate(
    ApplyView& view,
    const bool bSrcHigh,
    AccountID const& uSrcAccountID,
    AccountID const& uDstAccountID,
    uint256 const& uIndex,      // --> ripple state entry
    SLE::ref sleAccount,        // --> the account being set.
    const bool bAuth,           // --> authorize account.
    const bool bNoRipple,       // --> others cannot ripple through
    const bool bFreeze,         // --> funds cannot leave
    STAmount const& saBalance,  // --> balance of account being set.
                                // Issuer should be noAccount()
    STAmount const& saLimit,    // --> limit for account being set.
                                // Issuer should be the account being set.
    std::uint32_t uQualityIn,
    std::uint32_t uQualityOut,
    beast::Journal j)
{
    JLOG(j.trace()) << "trustCreate: " << to_string(uSrcAccountID) << ", "
                    << to_string(uDstAccountID) << ", "
                    << saBalance.getFullText();

    auto const& uLowAccountID = !bSrcHigh ? uSrcAccountID : uDstAccountID;
    auto const& uHighAccountID = bSrcHigh ? uSrcAccountID : uDstAccountID;

    auto const sleRippleState = std::make_shared<SLE>(ltRIPPLE_STATE, uIndex);
    view.insert(sleRippleState);

    auto lowNode = view.dirInsert(
        keylet::ownerDir(uLowAccountID),
        sleRippleState->key(),
        describeOwnerDir(uLowAccountID));

    if (!lowNode)
        return tecDIR_FULL;

    auto highNode = view.dirInsert(
        keylet::ownerDir(uHighAccountID),
        sleRippleState->key(),
        describeOwnerDir(uHighAccountID));

    if (!highNode)
        return tecDIR_FULL;

    const bool bSetDst = saLimit.getIssuer() == uDstAccountID;
    const bool bSetHigh = bSrcHigh ^ bSetDst;

    assert(sleAccount);
    if (!sleAccount)
        return tefINTERNAL;

    assert(
        sleAccount->getAccountID(sfAccount) ==
        (bSetHigh ? uHighAccountID : uLowAccountID));
    auto const slePeer =
        view.peek(keylet::account(bSetHigh ? uLowAccountID : uHighAccountID));
    if (!slePeer)
        return tecNO_TARGET;

    // Remember deletion hints.
    sleRippleState->setFieldU64(sfLowNode, *lowNode);
    sleRippleState->setFieldU64(sfHighNode, *highNode);

    sleRippleState->setFieldAmount(
        bSetHigh ? sfHighLimit : sfLowLimit, saLimit);
    sleRippleState->setFieldAmount(
        bSetHigh ? sfLowLimit : sfHighLimit,
        STAmount(
            {saBalance.getCurrency(),
             bSetDst ? uSrcAccountID : uDstAccountID}));

    if (uQualityIn)
        sleRippleState->setFieldU32(
            bSetHigh ? sfHighQualityIn : sfLowQualityIn, uQualityIn);

    if (uQualityOut)
        sleRippleState->setFieldU32(
            bSetHigh ? sfHighQualityOut : sfLowQualityOut, uQualityOut);

    std::uint32_t uFlags = bSetHigh ? lsfHighReserve : lsfLowReserve;

    if (bAuth)
    {
        uFlags |= (bSetHigh ? lsfHighAuth : lsfLowAuth);
    }
    if (bNoRipple)
    {
        uFlags |= (bSetHigh ? lsfHighNoRipple : lsfLowNoRipple);
    }
    if (bFreeze)
    {
        uFlags |= (!bSetHigh ? lsfLowFreeze : lsfHighFreeze);
    }

    if ((slePeer->getFlags() & lsfDefaultRipple) == 0)
    {
        // The other side's default is no rippling
        uFlags |= (bSetHigh ? lsfLowNoRipple : lsfHighNoRipple);
    }

    sleRippleState->setFieldU32(sfFlags, uFlags);
    adjustOwnerCount(view, sleAccount, 1, j);

    // ONLY: Create ripple balance.
    sleRippleState->setFieldAmount(
        sfBalance, bSetHigh ? -saBalance : saBalance);

    view.creditHook(
        uSrcAccountID, uDstAccountID, saBalance, saBalance.zeroed());

    return tesSUCCESS;
}

TER
trustDelete(
    ApplyView& view,
    std::shared_ptr<SLE> const& sleRippleState,
    AccountID const& uLowAccountID,
    AccountID const& uHighAccountID,
    beast::Journal j)
{
    // Detect legacy dirs.
    std::uint64_t uLowNode = sleRippleState->getFieldU64(sfLowNode);
    std::uint64_t uHighNode = sleRippleState->getFieldU64(sfHighNode);

    JLOG(j.trace()) << "trustDelete: Deleting ripple line: low";

    if (!view.dirRemove(
            keylet::ownerDir(uLowAccountID),
            uLowNode,
            sleRippleState->key(),
            false))
    {
        return tefBAD_LEDGER;
    }

    JLOG(j.trace()) << "trustDelete: Deleting ripple line: high";

    if (!view.dirRemove(
            keylet::ownerDir(uHighAccountID),
            uHighNode,
            sleRippleState->key(),
            false))
    {
        return tefBAD_LEDGER;
    }

    JLOG(j.trace()) << "trustDelete: Deleting ripple line: state";
    view.erase(sleRippleState);

    return tesSUCCESS;
}

bool
isTrustDefault(
    std::shared_ptr<SLE> const& acc,
    std::shared_ptr<SLE> const& line)
{
    assert(acc && line);

    uint32_t tlFlags = line->getFieldU32(sfFlags);

    AccountID highAccID = line->getFieldAmount(sfHighLimit).issue().account;
    AccountID lowAccID = line->getFieldAmount(sfLowLimit).issue().account;

    AccountID accID = acc->getAccountID(sfAccount);

    assert(accID == highAccID || accID == lowAccID);

    bool high = accID == highAccID;

    uint32_t acFlags = line->getFieldU32(sfFlags);

    const auto fNoRipple{high ? lsfHighNoRipple : lsfLowNoRipple};
    const auto fFreeze{high ? lsfHighFreeze : lsfLowFreeze};

    if (tlFlags & fFreeze)
        return false;

    if ((acFlags & lsfDefaultRipple) && (tlFlags & fNoRipple))
        return false;

    if (line->getFieldAmount(sfBalance) != beast::zero)
        return false;

    if (line->isFieldPresent(sfLockedBalance))
        return false;

    if (line->getFieldAmount(high ? sfHighLimit : sfLowLimit) != beast::zero)
        return false;

    uint32_t qualityIn =
        line->getFieldU32(high ? sfHighQualityIn : sfLowQualityIn);
    uint32_t qualityOut =
        line->getFieldU32(high ? sfHighQualityOut : sfLowQualityOut);

    if (qualityIn && qualityIn != QUALITY_ONE)
        return false;

    if (qualityOut && qualityOut != QUALITY_ONE)
        return false;

    return true;
}

TER
offerDelete(ApplyView& view, std::shared_ptr<SLE> const& sle, beast::Journal j)
{
    if (!sle)
        return tesSUCCESS;
    auto offerIndex = sle->key();
    auto owner = sle->getAccountID(sfAccount);

    // Detect legacy directories.
    uint256 uDirectory = sle->getFieldH256(sfBookDirectory);

    if (!view.dirRemove(
            keylet::ownerDir(owner),
            sle->getFieldU64(sfOwnerNode),
            offerIndex,
            false))
    {
        return tefBAD_LEDGER;
    }

    if (!view.dirRemove(
            keylet::page(uDirectory),
            sle->getFieldU64(sfBookNode),
            offerIndex,
            false))
    {
        return tefBAD_LEDGER;
    }

    adjustOwnerCount(view, view.peek(keylet::account(owner)), -1, j);

    view.erase(sle);

    return tesSUCCESS;
}

// Direct send w/o fees:
// - Redeeming IOUs and/or sending sender's own IOUs.
// - Create trust line if needed.
// --> bCheckIssuer : normally require issuer to be involved.
TER
rippleCredit(
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
    assert(!bCheckIssuer || uSenderID == issuer || uReceiverID == issuer);
    (void)issuer;

    // Disallow sending to self.
    assert(uSenderID != uReceiverID);

    bool const bSenderHigh = uSenderID > uReceiverID;
    auto const index = keylet::line(uSenderID, uReceiverID, currency);

    assert(!isXRP(uSenderID) && uSenderID != noAccount());
    assert(!isXRP(uReceiverID) && uReceiverID != noAccount());

    // If the line exists, modify it accordingly.
    if (auto const sleRippleState = view.peek(index))
    {
        STAmount saBalance = sleRippleState->getFieldAmount(sfBalance);

        if (bSenderHigh)
            saBalance.negate();  // Put balance in sender terms.

        view.creditHook(uSenderID, uReceiverID, saAmount, saBalance);

        STAmount const saBefore = saBalance;

        saBalance -= saAmount;

        JLOG(j.trace()) << "rippleCredit: " << to_string(uSenderID) << " -> "
                        << to_string(uReceiverID)
                        << " : before=" << saBefore.getFullText()
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
            &&
            static_cast<bool>(
                uFlags & (!bSenderHigh ? lsfLowNoRipple : lsfHighNoRipple)) !=
                static_cast<bool>(
                    view.read(keylet::account(uSenderID))->getFlags() &
                    lsfDefaultRipple) &&
            !(uFlags & (!bSenderHigh ? lsfLowFreeze : lsfHighFreeze)) &&
            !sleRippleState->getFieldAmount(
                !bSenderHigh ? sfLowLimit : sfHighLimit)
            // Sender trust limit is 0.
            && !sleRippleState->getFieldU32(
                   !bSenderHigh ? sfLowQualityIn : sfHighQualityIn)
            // Sender quality in is 0.
            && !sleRippleState->getFieldU32(
                   !bSenderHigh ? sfLowQualityOut : sfHighQualityOut))
        // Sender quality out is 0.
        {
            // Clear the reserve of the sender, possibly delete the line!
            adjustOwnerCount(
                view, view.peek(keylet::account(uSenderID)), -1, j);

            // Clear reserve flag.
            sleRippleState->setFieldU32(
                sfFlags,
                uFlags & (!bSenderHigh ? ~lsfLowReserve : ~lsfHighReserve));

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

    STAmount const saReceiverLimit({currency, uReceiverID});
    STAmount saBalance{saAmount};

    saBalance.setIssuer(noAccount());

    JLOG(j.debug()) << "rippleCredit: "
                       "create line: "
                    << to_string(uSenderID) << " -> " << to_string(uReceiverID)
                    << " : " << saAmount.getFullText();

    auto const sleAccount = view.peek(keylet::account(uReceiverID));
    if (!sleAccount)
        return tefINTERNAL;

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
rippleSend(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    STAmount& saActual,
    beast::Journal j)
{
    auto const issuer = saAmount.getIssuer();

    assert(!isXRP(uSenderID) && !isXRP(uReceiverID));
    assert(uSenderID != uReceiverID);

    if (uSenderID == issuer || uReceiverID == issuer || issuer == noAccount())
    {
        // Direct send: redeeming IOUs and/or sending own IOUs.
        auto const ter =
            rippleCredit(view, uSenderID, uReceiverID, saAmount, false, j);
        if (view.rules().enabled(featureDeletableAccounts) && ter != tesSUCCESS)
            return ter;
        saActual = saAmount;
        return tesSUCCESS;
    }

    // Sending 3rd party IOUs: transit.

    // Calculate the amount to transfer accounting
    // for any transfer fees:
    saActual = multiply(saAmount, transferRate(view, issuer));

    JLOG(j.debug()) << "rippleSend> " << to_string(uSenderID) << " - > "
                    << to_string(uReceiverID)
                    << " : deliver=" << saAmount.getFullText()
                    << " cost=" << saActual.getFullText();

    TER terResult = rippleCredit(view, issuer, uReceiverID, saAmount, true, j);

    if (tesSUCCESS == terResult)
        terResult = rippleCredit(view, uSenderID, issuer, saActual, true, j);

    return terResult;
}

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

    if (!saAmount.native())
    {
        STAmount saActual;

        JLOG(j.trace()) << "accountSend: " << to_string(uSenderID) << " -> "
                        << to_string(uReceiverID) << " : "
                        << saAmount.getFullText();

        return rippleSend(view, uSenderID, uReceiverID, saAmount, saActual, j);
    }

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

static bool
updateTrustLine(
    ApplyView& view,
    SLE::pointer state,
    bool bSenderHigh,
    AccountID const& sender,
    STAmount const& before,
    STAmount const& after,
    beast::Journal j)
{
    if (!state)
        return false;
    std::uint32_t const flags(state->getFieldU32(sfFlags));

    auto sle = view.peek(keylet::account(sender));
    if (!sle)
        return false;

    // YYY Could skip this if rippling in reverse.
    if (before > beast::zero
        // Sender balance was positive.
        && after <= beast::zero
        // Sender is zero or negative.
        && (flags & (!bSenderHigh ? lsfLowReserve : lsfHighReserve))
        // Sender reserve is set.
        && static_cast<bool>(
               flags & (!bSenderHigh ? lsfLowNoRipple : lsfHighNoRipple)) !=
            static_cast<bool>(sle->getFlags() & lsfDefaultRipple) &&
        !(flags & (!bSenderHigh ? lsfLowFreeze : lsfHighFreeze)) &&
        !state->getFieldAmount(!bSenderHigh ? sfLowLimit : sfHighLimit)
        // Sender trust limit is 0.
        && !state->getFieldU32(!bSenderHigh ? sfLowQualityIn : sfHighQualityIn)
        // Sender quality in is 0.
        &&
        !state->getFieldU32(!bSenderHigh ? sfLowQualityOut : sfHighQualityOut))
    // Sender quality out is 0.
    {
        // VFALCO Where is the line being deleted?
        // Clear the reserve of the sender, possibly delete the line!
        adjustOwnerCount(view, sle, -1, j);

        // Clear reserve flag.
        state->setFieldU32(
            sfFlags, flags & (!bSenderHigh ? ~lsfLowReserve : ~lsfHighReserve));

        // Balance is zero, receiver reserve is clear.
        if (!after  // Balance is zero.
            && !(flags & (bSenderHigh ? lsfLowReserve : lsfHighReserve)))
            return true;
    }
    return false;
}

TER
issueIOU(
    ApplyView& view,
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue,
    beast::Journal j)
{
    assert(!isXRP(account) && !isXRP(issue.account));

    // Consistency check
    assert(issue == amount.issue());

    // Can't send to self!
    assert(issue.account != account);

    JLOG(j.trace()) << "issueIOU: " << to_string(account) << ": "
                    << amount.getFullText();

    bool bSenderHigh = issue.account > account;

    auto const index = keylet::line(issue.account, account, issue.currency);

    if (auto state = view.peek(index))
    {
        STAmount final_balance = state->getFieldAmount(sfBalance);

        if (bSenderHigh)
            final_balance.negate();  // Put balance in sender terms.

        STAmount const start_balance = final_balance;

        final_balance -= amount;

        auto const must_delete = updateTrustLine(
            view,
            state,
            bSenderHigh,
            issue.account,
            start_balance,
            final_balance,
            j);

        view.creditHook(issue.account, account, amount, start_balance);

        if (bSenderHigh)
            final_balance.negate();

        // Adjust the balance on the trust line if necessary. We do this even if
        // we are going to delete the line to reflect the correct balance at the
        // time of deletion.
        state->setFieldAmount(sfBalance, final_balance);
        if (must_delete)
            return trustDelete(
                view,
                state,
                bSenderHigh ? account : issue.account,
                bSenderHigh ? issue.account : account,
                j);

        view.update(state);

        return tesSUCCESS;
    }

    // NIKB TODO: The limit uses the receiver's account as the issuer and
    // this is unnecessarily inefficient as copying which could be avoided
    // is now required. Consider available options.
    STAmount const limit({issue.currency, account});
    STAmount final_balance = amount;

    final_balance.setIssuer(noAccount());

    auto const receiverAccount = view.peek(keylet::account(account));
    if (!receiverAccount)
        return tefINTERNAL;

    bool noRipple = (receiverAccount->getFlags() & lsfDefaultRipple) == 0;

    return trustCreate(
        view,
        bSenderHigh,
        issue.account,
        account,
        index.key,
        receiverAccount,
        false,
        noRipple,
        false,
        final_balance,
        limit,
        0,
        0,
        j);
}

TER
redeemIOU(
    ApplyView& view,
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue,
    beast::Journal j)
{
    assert(!isXRP(account) && !isXRP(issue.account));

    // Consistency check
    assert(issue == amount.issue());

    // Can't send to self!
    assert(issue.account != account);

    JLOG(j.trace()) << "redeemIOU: " << to_string(account) << ": "
                    << amount.getFullText();

    bool bSenderHigh = account > issue.account;

    if (auto state =
            view.peek(keylet::line(account, issue.account, issue.currency)))
    {
        STAmount final_balance = state->getFieldAmount(sfBalance);

        if (bSenderHigh)
            final_balance.negate();  // Put balance in sender terms.

        STAmount const start_balance = final_balance;

        final_balance -= amount;

        auto const must_delete = updateTrustLine(
            view, state, bSenderHigh, account, start_balance, final_balance, j);

        view.creditHook(account, issue.account, amount, start_balance);

        if (bSenderHigh)
            final_balance.negate();

        // Adjust the balance on the trust line if necessary. We do this even if
        // we are going to delete the line to reflect the correct balance at the
        // time of deletion.
        state->setFieldAmount(sfBalance, final_balance);

        if (must_delete)
        {
            return trustDelete(
                view,
                state,
                bSenderHigh ? issue.account : account,
                bSenderHigh ? account : issue.account,
                j);
        }

        view.update(state);
        return tesSUCCESS;
    }

    // In order to hold an IOU, a trust line *MUST* exist to track the
    // balance. If it doesn't, then something is very wrong. Don't try
    // to continue.
    JLOG(j.fatal()) << "redeemIOU: " << to_string(account)
                    << " attempts to redeem " << amount.getFullText()
                    << " but no trust line exists!";

    return tefINTERNAL;
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
    assert(amount.native());

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
