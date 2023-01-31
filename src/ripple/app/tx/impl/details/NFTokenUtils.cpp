//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2021 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/details/NFTokenUtils.h>
#include <ripple/basics/algorithm.h>
#include <ripple/ledger/Directory.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/nftPageMask.h>
#include <functional>
#include <memory>

namespace ripple {

namespace nft {

static std::shared_ptr<SLE const>
locatePage(ReadView const& view, AccountID owner, uint256 const& id)
{
    auto const first = keylet::nftpage(keylet::nftpage_min(owner), id);
    auto const last = keylet::nftpage_max(owner);

    // This NFT can only be found in the first page with a key that's strictly
    // greater than `first`, so look for that, up until the maximum possible
    // page.
    return view.read(Keylet(
        ltNFTOKEN_PAGE,
        view.succ(first.key, last.key.next()).value_or(last.key)));
}

static std::shared_ptr<SLE>
locatePage(ApplyView& view, AccountID owner, uint256 const& id)
{
    auto const first = keylet::nftpage(keylet::nftpage_min(owner), id);
    auto const last = keylet::nftpage_max(owner);

    // This NFT can only be found in the first page with a key that's strictly
    // greater than `first`, so look for that, up until the maximum possible
    // page.
    return view.peek(Keylet(
        ltNFTOKEN_PAGE,
        view.succ(first.key, last.key.next()).value_or(last.key)));
}

static std::shared_ptr<SLE>
getPageForToken(
    ApplyView& view,
    AccountID const& owner,
    uint256 const& id,
    std::function<void(ApplyView&, AccountID const&)> const& createCallback)
{
    auto const base = keylet::nftpage_min(owner);
    auto const first = keylet::nftpage(base, id);
    auto const last = keylet::nftpage_max(owner);

    // This NFT can only be found in the first page with a key that's strictly
    // greater than `first`, so look for that, up until the maximum possible
    // page.
    auto cp = view.peek(Keylet(
        ltNFTOKEN_PAGE,
        view.succ(first.key, last.key.next()).value_or(last.key)));

    // A suitable page doesn't exist; we'll have to create one.
    if (!cp)
    {
        STArray arr;
        cp = std::make_shared<SLE>(last);
        cp->setFieldArray(sfNFTokens, arr);
        view.insert(cp);
        createCallback(view, owner);
        return cp;
    }

    STArray narr = cp->getFieldArray(sfNFTokens);

    // The right page still has space: we're good.
    if (narr.size() != dirMaxTokensPerPage)
        return cp;

    // We need to split the page in two: the first half of the items in this
    // page will go into the new page; the rest will stay with the existing
    // page.
    //
    // Note we can't always split the page exactly in half.  All equivalent
    // NFTs must be kept on the same page.  So when the page contains
    // equivalent NFTs, the split may be lopsided in order to keep equivalent
    // NFTs on the same page.
    STArray carr;
    {
        // We prefer to keep equivalent NFTs on a page boundary.  That gives
        // any additional equivalent NFTs maximum room for expansion.
        // Round up the boundary until there's a non-equivalent entry.
        uint256 const cmp =
            narr[(dirMaxTokensPerPage / 2) - 1].getFieldH256(sfNFTokenID) &
            nft::pageMask;

        // Note that the calls to find_if_not() and (later) find_if()
        // rely on the fact that narr is kept in sorted order.
        auto splitIter = std::find_if_not(
            narr.begin() + (dirMaxTokensPerPage / 2),
            narr.end(),
            [&cmp](STObject const& obj) {
                return (obj.getFieldH256(sfNFTokenID) & nft::pageMask) == cmp;
            });

        // If we get all the way from the middle to the end with only
        // equivalent NFTokens then check the front of the page for a
        // place to make the split.
        if (splitIter == narr.end())
            splitIter = std::find_if(
                narr.begin(), narr.end(), [&cmp](STObject const& obj) {
                    return (obj.getFieldH256(sfNFTokenID) & nft::pageMask) ==
                        cmp;
                });

        // There should be no circumstance when splitIter == end(), but if it
        // were to happen we should bail out because something is confused.
        if (splitIter == narr.end())
            return nullptr;

        // If splitIter == begin(), then the entire page is filled with
        // equivalent tokens.  This requires special handling.
        if (splitIter == narr.begin())
        {
            // Prior to fixNFTokenDirV1 we simply stopped.
            if (!view.rules().enabled(fixNFTokenDirV1))
                return nullptr;
            else
            {
                // This would be an ideal place for the spaceship operator...
                int const relation = compare(id & nft::pageMask, cmp);
                if (relation == 0)
                    // If the passed in id belongs exactly on this (full) page
                    // this account simply cannot store the NFT.
                    return nullptr;

                else if (relation > 0)
                    // We need to leave the entire contents of this page in
                    // narr so carr stays empty.  The new NFT will be
                    // inserted in carr.  This keeps the NFTs that must be
                    // together all on their own page.
                    splitIter = narr.end();

                // If neither of those conditions apply then put all of
                // narr into carr and produce an empty narr where the new NFT
                // will be inserted.  Leave the split at narr.begin().
            }
        }

        // Split narr at splitIter.
        STArray newCarr(
            std::make_move_iterator(splitIter),
            std::make_move_iterator(narr.end()));
        narr.erase(splitIter, narr.end());
        std::swap(carr, newCarr);
    }

    // Determine the ID for the page index.  This decision is conditional on
    // fixNFTokenDirV1 being enabled.  But the condition for the decision
    // is not possible unless fixNFTokenDirV1 is enabled.
    //
    // Note that we use uint256::next() because there's a subtlety in the way
    // NFT pages are structured.  The low 96-bits of NFT ID must be strictly
    // less than the low 96-bits of the enclosing page's index.  In order to
    // accommodate that requirement we use an index one higher than the
    // largest NFT in the page.
    uint256 const tokenIDForNewPage = narr.size() == dirMaxTokensPerPage
        ? narr[dirMaxTokensPerPage - 1].getFieldH256(sfNFTokenID).next()
        : carr[0].getFieldH256(sfNFTokenID);

    auto np = std::make_shared<SLE>(keylet::nftpage(base, tokenIDForNewPage));
    np->setFieldArray(sfNFTokens, narr);
    np->setFieldH256(sfNextPageMin, cp->key());

    if (auto ppm = (*cp)[~sfPreviousPageMin])
    {
        np->setFieldH256(sfPreviousPageMin, *ppm);

        if (auto p3 = view.peek(Keylet(ltNFTOKEN_PAGE, *ppm)))
        {
            p3->setFieldH256(sfNextPageMin, np->key());
            view.update(p3);
        }
    }

    view.insert(np);

    cp->setFieldArray(sfNFTokens, carr);
    cp->setFieldH256(sfPreviousPageMin, np->key());
    view.update(cp);

    createCallback(view, owner);

    // fixNFTokenDirV1 corrects a bug in the initial implementation that
    // would put an NFT in the wrong page.  The problem was caused by an
    // off-by-one subtlety that the NFT can only be stored in the first page
    // with a key that's strictly greater than `first`
    if (!view.rules().enabled(fixNFTokenDirV1))
        return (first.key <= np->key()) ? np : cp;

    return (first.key < np->key()) ? np : cp;
}

bool
compareTokens(uint256 const& a, uint256 const& b)
{
    // The sort of NFTokens needs to be fully deterministic, but the sort
    // is weird because we sort on the low 96-bits first. But if the low
    // 96-bits are identical we still need a fully deterministic sort.
    // So we sort on the low 96-bits first. If those are equal we sort on
    // the whole thing.
    if (auto const lowBitsCmp = compare(a & nft::pageMask, b & nft::pageMask);
        lowBitsCmp != 0)
        return lowBitsCmp < 0;

    return a < b;
}

/** Insert the token in the owner's token directory. */
TER
insertToken(ApplyView& view, AccountID owner, STObject&& nft)
{
    assert(nft.isFieldPresent(sfNFTokenID));

    // First, we need to locate the page the NFT belongs to, creating it
    // if necessary. This operation may fail if it is impossible to insert
    // the NFT.
    std::shared_ptr<SLE> page = getPageForToken(
        view,
        owner,
        nft[sfNFTokenID],
        [](ApplyView& view, AccountID const& owner) {
            adjustOwnerCount(
                view,
                view.peek(keylet::account(owner)),
                1,
                beast::Journal{beast::Journal::getNullSink()});
        });

    if (!page)
        return tecNO_SUITABLE_NFTOKEN_PAGE;

    {
        auto arr = page->getFieldArray(sfNFTokens);
        arr.push_back(std::move(nft));

        arr.sort([](STObject const& o1, STObject const& o2) {
            return compareTokens(
                o1.getFieldH256(sfNFTokenID), o2.getFieldH256(sfNFTokenID));
        });

        page->setFieldArray(sfNFTokens, arr);
    }

    view.update(page);

    return tesSUCCESS;
}

static bool
mergePages(
    ApplyView& view,
    std::shared_ptr<SLE> const& p1,
    std::shared_ptr<SLE> const& p2)
{
    if (p1->key() >= p2->key())
        Throw<std::runtime_error>("mergePages: pages passed in out of order!");

    if ((*p1)[~sfNextPageMin] != p2->key())
        Throw<std::runtime_error>("mergePages: next link broken!");

    if ((*p2)[~sfPreviousPageMin] != p1->key())
        Throw<std::runtime_error>("mergePages: previous link broken!");

    auto const p1arr = p1->getFieldArray(sfNFTokens);
    auto const p2arr = p2->getFieldArray(sfNFTokens);

    // Now check whether to merge the two pages; it only makes sense to do
    // this it would mean that one of them can be deleted as a result of
    // the merge.

    if (p1arr.size() + p2arr.size() > dirMaxTokensPerPage)
        return false;

    STArray x(p1arr.size() + p2arr.size());

    std::merge(
        p1arr.begin(),
        p1arr.end(),
        p2arr.begin(),
        p2arr.end(),
        std::back_inserter(x),
        [](STObject const& a, STObject const& b) {
            return compareTokens(
                a.getFieldH256(sfNFTokenID), b.getFieldH256(sfNFTokenID));
        });

    p2->setFieldArray(sfNFTokens, x);

    // So, at this point we need to unlink "p1" (since we just emptied it) but
    // we need to first relink the directory: if p1 has a previous page (p0),
    // load it, point it to p2 and point p2 to it.

    p2->makeFieldAbsent(sfPreviousPageMin);

    if (auto const ppm = (*p1)[~sfPreviousPageMin])
    {
        auto p0 = view.peek(Keylet(ltNFTOKEN_PAGE, *ppm));

        if (!p0)
            Throw<std::runtime_error>("mergePages: p0 can't be located!");

        p0->setFieldH256(sfNextPageMin, p2->key());
        view.update(p0);

        p2->setFieldH256(sfPreviousPageMin, *ppm);
    }

    view.update(p2);
    view.erase(p1);

    return true;
}

/** Remove the token from the owner's token directory. */
TER
removeToken(ApplyView& view, AccountID const& owner, uint256 const& nftokenID)
{
    std::shared_ptr<SLE> page = locatePage(view, owner, nftokenID);

    // If the page couldn't be found, the given NFT isn't owned by this account
    if (!page)
        return tecNO_ENTRY;

    return removeToken(view, owner, nftokenID, std::move(page));
}

/** Remove the token from the owner's token directory. */
TER
removeToken(
    ApplyView& view,
    AccountID const& owner,
    uint256 const& nftokenID,
    std::shared_ptr<SLE>&& curr)
{
    // We found a page, but the given NFT may not be in it.
    auto arr = curr->getFieldArray(sfNFTokens);

    {
        auto x = std::find_if(
            arr.begin(), arr.end(), [&nftokenID](STObject const& obj) {
                return (obj[sfNFTokenID] == nftokenID);
            });

        if (x == arr.end())
            return tecNO_ENTRY;

        arr.erase(x);
    }

    // Page management:
    auto const loadPage = [&view](
                              std::shared_ptr<SLE> const& page1,
                              SF_UINT256 const& field) {
        std::shared_ptr<SLE> page2;

        if (auto const id = (*page1)[~field])
        {
            page2 = view.peek(Keylet(ltNFTOKEN_PAGE, *id));

            if (!page2)
                Throw<std::runtime_error>(
                    "page " + to_string(page1->key()) + " has a broken " +
                    field.getName() + " field pointing to " + to_string(*id));
        }

        return page2;
    };

    auto const prev = loadPage(curr, sfPreviousPageMin);
    auto const next = loadPage(curr, sfNextPageMin);

    if (!arr.empty())
    {
        // The current page isn't empty. Update it and then try to consolidate
        // pages. Note that this consolidation attempt may actually merge three
        // pages into one!
        curr->setFieldArray(sfNFTokens, arr);
        view.update(curr);

        int cnt = 0;

        if (prev && mergePages(view, prev, curr))
            cnt--;

        if (next && mergePages(view, curr, next))
            cnt--;

        if (cnt != 0)
            adjustOwnerCount(
                view,
                view.peek(keylet::account(owner)),
                cnt,
                beast::Journal{beast::Journal::getNullSink()});

        return tesSUCCESS;
    }

    // The page is empty, so we can just unlink it and then remove it.
    if (prev)
    {
        // Make our previous page point to our next page:
        if (next)
            prev->setFieldH256(sfNextPageMin, next->key());
        else
            prev->makeFieldAbsent(sfNextPageMin);

        view.update(prev);
    }

    if (next)
    {
        // Make our next page point to our previous page:
        if (prev)
            next->setFieldH256(sfPreviousPageMin, prev->key());
        else
            next->makeFieldAbsent(sfPreviousPageMin);

        view.update(next);
    }

    view.erase(curr);

    int cnt = 1;

    // Since we're here, try to consolidate the previous and current pages
    // of the page we removed (if any) into one.  mergePages() _should_
    // always return false.  Since tokens are burned one at a time, there
    // should never be a page containing one token sitting between two pages
    // that have few enough tokens that they can be merged.
    //
    // But, in case that analysis is wrong, it's good to leave this code here
    // just in case.
    if (prev && next &&
        mergePages(
            view,
            view.peek(Keylet(ltNFTOKEN_PAGE, prev->key())),
            view.peek(Keylet(ltNFTOKEN_PAGE, next->key()))))
        cnt++;

    adjustOwnerCount(
        view,
        view.peek(keylet::account(owner)),
        -1 * cnt,
        beast::Journal{beast::Journal::getNullSink()});

    return tesSUCCESS;
}

std::optional<STObject>
findToken(
    ReadView const& view,
    AccountID const& owner,
    uint256 const& nftokenID)
{
    std::shared_ptr<SLE const> page = locatePage(view, owner, nftokenID);

    // If the page couldn't be found, the given NFT isn't owned by this account
    if (!page)
        return std::nullopt;

    // We found a candidate page, but the given NFT may not be in it.
    for (auto const& t : page->getFieldArray(sfNFTokens))
    {
        if (t[sfNFTokenID] == nftokenID)
            return t;
    }

    return std::nullopt;
}

std::optional<TokenAndPage>
findTokenAndPage(
    ApplyView& view,
    AccountID const& owner,
    uint256 const& nftokenID)
{
    std::shared_ptr<SLE> page = locatePage(view, owner, nftokenID);

    // If the page couldn't be found, the given NFT isn't owned by this account
    if (!page)
        return std::nullopt;

    // We found a candidate page, but the given NFT may not be in it.
    for (auto const& t : page->getFieldArray(sfNFTokens))
    {
        if (t[sfNFTokenID] == nftokenID)
            // This std::optional constructor is explicit, so it is spelled out.
            return std::optional<TokenAndPage>(
                std::in_place, t, std::move(page));
    }
    return std::nullopt;
}

std::size_t
removeTokenOffersWithLimit(
    ApplyView& view,
    Keylet const& directory,
    std::size_t maxDeletableOffers)
{
    if (maxDeletableOffers == 0)
        return 0;

    std::optional<std::uint64_t> pageIndex{0};
    std::size_t deletedOffersCount = 0;

    do
    {
        auto const page = view.peek(keylet::page(directory, *pageIndex));
        if (!page)
            break;

        // We get the index of the next page in case the current
        // page is deleted after all of its entries have been removed
        pageIndex = (*page)[~sfIndexNext];

        auto offerIndexes = page->getFieldV256(sfIndexes);

        // We reverse-iterate the offer directory page to delete all entries.
        // Deleting an entry in a NFTokenOffer directory page won't cause
        // entries from other pages to move to the current, so, it is safe to
        // delete entries one by one in the page. It is required to iterate
        // backwards to handle iterator invalidation for vector, as we are
        // deleting during iteration.
        for (int i = offerIndexes.size() - 1; i >= 0; --i)
        {
            if (auto const offer = view.peek(keylet::nftoffer(offerIndexes[i])))
            {
                if (deleteTokenOffer(view, offer))
                    ++deletedOffersCount;
                else
                    Throw<std::runtime_error>(
                        "Offer " + to_string(offerIndexes[i]) +
                        " cannot be deleted!");
            }

            if (maxDeletableOffers == deletedOffersCount)
                break;
        }
    } while (pageIndex.value_or(0) && maxDeletableOffers != deletedOffersCount);

    return deletedOffersCount;
}

TER
notTooManyOffers(ReadView const& view, uint256 const& nftokenID)
{
    std::size_t totalOffers = 0;

    {
        Dir buys(view, keylet::nft_buys(nftokenID));
        for (auto iter = buys.begin(); iter != buys.end(); iter.next_page())
        {
            totalOffers += iter.page_size();
            if (totalOffers > maxDeletableTokenOfferEntries)
                return tefTOO_BIG;
        }
    }

    {
        Dir sells(view, keylet::nft_sells(nftokenID));
        for (auto iter = sells.begin(); iter != sells.end(); iter.next_page())
        {
            totalOffers += iter.page_size();
            if (totalOffers > maxDeletableTokenOfferEntries)
                return tefTOO_BIG;
        }
    }
    return tesSUCCESS;
}

bool
deleteTokenOffer(ApplyView& view, std::shared_ptr<SLE> const& offer)
{
    if (offer->getType() != ltNFTOKEN_OFFER)
        return false;

    auto const owner = (*offer)[sfOwner];

    if (!view.dirRemove(
            keylet::ownerDir(owner),
            (*offer)[sfOwnerNode],
            offer->key(),
            false))
        return false;

    auto const nftokenID = (*offer)[sfNFTokenID];

    if (!view.dirRemove(
            ((*offer)[sfFlags] & tfSellNFToken) ? keylet::nft_sells(nftokenID)
                                                : keylet::nft_buys(nftokenID),
            (*offer)[sfNFTokenOfferNode],
            offer->key(),
            false))
        return false;

    adjustOwnerCount(
        view,
        view.peek(keylet::account(owner)),
        -1,
        beast::Journal{beast::Journal::getNullSink()});

    view.erase(offer);
    return true;
}

}  // namespace nft
}  // namespace ripple
