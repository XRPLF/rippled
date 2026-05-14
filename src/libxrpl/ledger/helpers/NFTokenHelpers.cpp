/**
 * @file NFTokenHelpers.cpp
 * @brief Implementation of NFT paged-directory and offer management helpers.
 *
 * All NFT lifecycle operations (mint, burn, transfer, offer create/cancel)
 * delegate to these helpers instead of manipulating ledger state directly.
 * The file owns three families of logic:
 *
 * 1. **Page management** (`locatePage`, `getPageForToken`, `mergePages`,
 *    `insertToken`, `removeToken`): maintain the doubly-linked
 *    `ltNFTOKEN_PAGE` chain and its sorted-token invariants.
 *
 * 2. **Offer management** (`deleteTokenOffer`, `removeTokenOffersWithLimit`,
 *    `tokenOfferCreatePreflight/Preclaim/Apply`): insert and remove offers
 *    from the per-token buy/sell directories and the owner directory.
 *
 * 3. **Repair** (`repairNFTokenDirectoryLinks`): defensive walk-and-fix of
 *    broken page links, introduced alongside `fixNFTokenPageLinks`.
 */
#include <xrpl/ledger/helpers/NFTokenHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/protocol/nftPageMask.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace xrpl::nft {

/** Find the read-only NFToken page that may contain `id` for `owner`.
 *
 *  Computes the theoretical lower-bound page key for the token, then calls
 *  `view.succ()` to find the first actual page key strictly greater than it
 *  within the owner's range.  This O(log N) B-tree lookup avoids walking
 *  the linked list.  Falls back to the max-page key if `succ()` yields
 *  nothing, so the read returns `nullptr` when no page at all exists.
 *
 *  @param view Read-only ledger view.
 *  @param owner Account whose NFToken pages are searched.
 *  @param id Full 256-bit NFToken ID.
 *  @return The candidate page SLE, or `nullptr` if no page can contain `id`.
 */
static std::shared_ptr<SLE const>
locatePage(ReadView const& view, AccountID const& owner, uint256 const& id)
{
    auto const first = keylet::nftpage(keylet::nftpageMin(owner), id);
    auto const last = keylet::nftpageMax(owner);

    return view.read(
        Keylet(ltNFTOKEN_PAGE, view.succ(first.key, last.key.next()).value_or(last.key)));
}

/** Find the mutable NFToken page that may contain `id` for `owner`.
 *
 *  Same lookup strategy as the `ReadView const&` overload but calls
 *  `view.peek()` so the returned SLE can be mutated and passed back to
 *  `view.update()` or `view.erase()` on the same view instance.
 *
 *  @param view Mutable ledger view.
 *  @param owner Account whose NFToken pages are searched.
 *  @param id Full 256-bit NFToken ID.
 *  @return The candidate page SLE, or `nullptr` if no page can contain `id`.
 */
static std::shared_ptr<SLE>
locatePage(ApplyView& view, AccountID const& owner, uint256 const& id)
{
    auto const first = keylet::nftpage(keylet::nftpageMin(owner), id);
    auto const last = keylet::nftpageMax(owner);

    return view.peek(
        Keylet(ltNFTOKEN_PAGE, view.succ(first.key, last.key.next()).value_or(last.key)));
}

/** Locate or create the NFToken page that should hold `id`, splitting a full
 *  page when necessary.
 *
 *  If no candidate page exists, a new empty page is created at
 *  `keylet::nftpage_max(owner)` and `createCallback` is invoked to
 *  increment the owner reserve count.
 *
 *  If the candidate page is full (`kDIR_MAX_TOKENS_PER_PAGE` tokens), it is
 *  split at the first equivalent-group boundary on or after the midpoint.
 *  Splitting strategy:
 *  - Start at the midpoint, advance past any run of equivalent tokens (same
 *    low-96-bit prefix).
 *  - If the entire back half is equivalent, search from the front instead.
 *  - If `splitIter == narr.end()` after both searches, the page is entirely
 *    one equivalence class and the new token cannot be placed — return
 *    `nullptr`.
 *  - If `splitIter == narr.begin()`, the page holds one class but the
 *    incoming token belongs to a *different* class:
 *      - Token sorts higher → leave all current tokens in `narr`; new token
 *        goes into empty `carr`.
 *      - Token sorts lower  → move all to `carr`; new token fills `narr`.
 *  After splitting, a new SLE is inserted for the lower half and doubly-linked
 *  pointers on up to three pages (new, existing, predecessor) are updated.
 *  `createCallback` fires once more to account for the extra page reserve.
 *  The new page key is set to `narr.back().next()` when `narr` is still full,
 *  or to `carr.front()` otherwise, preserving the page-key invariant.
 *
 *  @param view Mutable ledger view.
 *  @param owner Account that will own the new token.
 *  @param id Full 256-bit NFToken ID of the token about to be inserted.
 *  @param createCallback Invoked each time a new page SLE is created; must
 *      call `adjustOwnerCount` (or equivalent) to charge the reserve.
 *  @return The mutable SLE of the page that should receive the new token, or
 *      `nullptr` if the token's equivalence class has exhausted available
 *      page space.
 */
static std::shared_ptr<SLE>
getPageForToken(
    ApplyView& view,
    AccountID const& owner,
    uint256 const& id,
    std::function<void(ApplyView&, AccountID const&)> const& createCallback)
{
    auto const base = keylet::nftpageMin(owner);
    auto const first = keylet::nftpage(base, id);
    auto const last = keylet::nftpageMax(owner);

    auto cp =
        view.peek(Keylet(ltNFTOKEN_PAGE, view.succ(first.key, last.key.next()).value_or(last.key)));

    if (!cp)
    {
        STArray const arr;
        cp = std::make_shared<SLE>(last);
        cp->setFieldArray(sfNFTokens, arr);
        view.insert(cp);
        createCallback(view, owner);
        return cp;
    }

    STArray narr = cp->getFieldArray(sfNFTokens);

    if (narr.size() != kDIR_MAX_TOKENS_PER_PAGE)
        return cp;

    STArray carr;
    {
        // We prefer to keep equivalent NFTs on a page boundary.  That gives
        // any additional equivalent NFTs maximum room for expansion.
        // Round up the boundary until there's a non-equivalent entry.
        uint256 const cmp =
            narr[(kDIR_MAX_TOKENS_PER_PAGE / 2) - 1].getFieldH256(sfNFTokenID) & nft::kPAGE_MASK;

        // The calls to find_if_not() and (later) find_if() rely on narr
        // being kept in sorted order.
        auto splitIter = std::find_if_not(
            narr.begin() + (kDIR_MAX_TOKENS_PER_PAGE / 2), narr.end(), [&cmp](STObject const& obj) {
                return (obj.getFieldH256(sfNFTokenID) & nft::kPAGE_MASK) == cmp;
            });

        if (splitIter == narr.end())
        {
            splitIter = std::ranges::find_if(narr, [&cmp](STObject const& obj) {
                return (obj.getFieldH256(sfNFTokenID) & nft::kPAGE_MASK) == cmp;
            });
        }

        // There should be no circumstance when splitIter == end(), but if it
        // were to happen we should bail out because something is confused.
        if (splitIter == narr.end())
            return nullptr;

        if (splitIter == narr.begin())
        {
            auto const relation{(id & nft::kPAGE_MASK) <=> cmp};
            if (relation == 0)
                return nullptr;

            if (relation > 0)
                splitIter = narr.end();
        }

        STArray newCarr(std::make_move_iterator(splitIter), std::make_move_iterator(narr.end()));
        narr.erase(splitIter, narr.end());
        std::swap(carr, newCarr);
    }

    // Determine the ID for the page index.
    //
    // Note that we use uint256::next() because there's a subtlety in the way
    // NFT pages are structured.  The low 96-bits of NFT ID must be strictly
    // less than the low 96-bits of the enclosing page's index.  In order to
    // accommodate that requirement we use an index one higher than the
    // largest NFT in the page.
    uint256 const tokenIDForNewPage = narr.size() == kDIR_MAX_TOKENS_PER_PAGE
        ? narr[kDIR_MAX_TOKENS_PER_PAGE - 1].getFieldH256(sfNFTokenID).next()
        : carr[0].getFieldH256(sfNFTokenID);

    auto np = std::make_shared<SLE>(keylet::nftpage(base, tokenIDForNewPage));
    XRPL_ASSERT(np->key() > base.key, "xrpl::nft::getPageForToken : valid NFT page index");
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

    return (first.key < np->key()) ? np : cp;
}

bool
compareTokens(uint256 const& a, uint256 const& b)
{
    if (auto const lowBitsCmp{(a & nft::kPAGE_MASK) <=> (b & nft::kPAGE_MASK)}; lowBitsCmp != 0)
        return lowBitsCmp < 0;

    return a < b;
}

TER
changeTokenURI(
    ApplyView& view,
    AccountID const& owner,
    uint256 const& nftokenID,
    std::optional<xrpl::Slice> const& uri)
{
    std::shared_ptr<SLE> const page = locatePage(view, owner, nftokenID);

    if (!page)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    STArray& arr = page->peekFieldArray(sfNFTokens);

    auto const nftIter = std::ranges::find_if(
        arr, [&nftokenID](STObject const& obj) { return (obj[sfNFTokenID] == nftokenID); });

    if (nftIter == arr.end())
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (uri)
    {
        nftIter->setFieldVL(sfURI, *uri);
    }
    else if (nftIter->isFieldPresent(sfURI))
    {
        nftIter->makeFieldAbsent(sfURI);
    }

    view.update(page);
    return tesSUCCESS;
}

TER
insertToken(ApplyView& view, AccountID owner, STObject&& nft)
{
    XRPL_ASSERT(nft.isFieldPresent(sfNFTokenID), "xrpl::nft::insertToken : has NFT token");

    std::shared_ptr<SLE> const page =
        getPageForToken(view, owner, nft[sfNFTokenID], [](ApplyView& view, AccountID const& owner) {
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
        arr.pushBack(std::move(nft));

        arr.sort([](STObject const& o1, STObject const& o2) {
            return compareTokens(o1.getFieldH256(sfNFTokenID), o2.getFieldH256(sfNFTokenID));
        });

        page->setFieldArray(sfNFTokens, arr);
    }

    view.update(page);

    return tesSUCCESS;
}

/** Merge two adjacent NFToken pages into the higher-keyed page if they fit.
 *
 *  Validates that `p1` and `p2` are genuinely adjacent (correct key order
 *  and matching forward/backward link fields), then merges only when the
 *  combined token count does not exceed `kDIR_MAX_TOKENS_PER_PAGE`.  On
 *  success the merged tokens are written into `p2`, `p1` is erased, and
 *  the predecessor of `p1` (if any) is relinked to `p2`.
 *
 *  @param view Mutable ledger view.
 *  @param p1 The lower-keyed page (will be erased on a successful merge).
 *  @param p2 The higher-keyed page (will receive the merged tokens).
 *  @return `true` if the pages were merged; `false` if the combined token
 *      count exceeds the page capacity.
 *  @throws std::runtime_error if `p1`/`p2` are out of order or their link
 *      fields are inconsistent, indicating corrupted ledger state.
 */
static bool
mergePages(ApplyView& view, std::shared_ptr<SLE> const& p1, std::shared_ptr<SLE> const& p2)
{
    if (p1->key() >= p2->key())
        Throw<std::runtime_error>("mergePages: pages passed in out of order!");

    if ((*p1)[~sfNextPageMin] != p2->key())
        Throw<std::runtime_error>("mergePages: next link broken!");

    if ((*p2)[~sfPreviousPageMin] != p1->key())
        Throw<std::runtime_error>("mergePages: previous link broken!");

    auto const p1arr = p1->getFieldArray(sfNFTokens);
    auto const p2arr = p2->getFieldArray(sfNFTokens);

    if (p1arr.size() + p2arr.size() > kDIR_MAX_TOKENS_PER_PAGE)
        return false;

    STArray x(p1arr.size() + p2arr.size());

    std::ranges::merge(
        p1arr, p2arr, std::back_inserter(x), [](STObject const& a, STObject const& b) {
            return compareTokens(a.getFieldH256(sfNFTokenID), b.getFieldH256(sfNFTokenID));
        });

    p2->setFieldArray(sfNFTokens, x);

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

TER
removeToken(ApplyView& view, AccountID const& owner, uint256 const& nftokenID)
{
    std::shared_ptr<SLE> const page = locatePage(view, owner, nftokenID);

    if (!page)
        return tecNO_ENTRY;

    return removeToken(view, owner, nftokenID, page);
}

TER
removeToken(
    ApplyView& view,
    AccountID const& owner,
    uint256 const& nftokenID,
    std::shared_ptr<SLE> const& curr)
{
    auto arr = curr->getFieldArray(sfNFTokens);

    {
        auto x = std::ranges::find_if(
            arr, [&nftokenID](STObject const& obj) { return (obj[sfNFTokenID] == nftokenID); });

        if (x == arr.end())
            return tecNO_ENTRY;

        arr.erase(x);
    }

    auto const loadPage = [&view](std::shared_ptr<SLE> const& page1, SF_UINT256 const& field) {
        std::shared_ptr<SLE> page2;

        if (auto const id = (*page1)[~field])
        {
            page2 = view.peek(Keylet(ltNFTOKEN_PAGE, *id));

            if (!page2)
            {
                Throw<std::runtime_error>(
                    "page " + to_string(page1->key()) + " has a broken " + field.getName() +
                    " field pointing to " + to_string(*id));
            }
        }

        return page2;
    };

    auto const prev = loadPage(curr, sfPreviousPageMin);
    auto const next = loadPage(curr, sfNextPageMin);

    if (!arr.empty())
    {
        curr->setFieldArray(sfNFTokens, arr);
        view.update(curr);

        int cnt = 0;

        if (prev && mergePages(view, prev, curr))
            cnt--;

        if (next && mergePages(view, curr, next))
            cnt--;

        if (cnt != 0)
        {
            adjustOwnerCount(
                view,
                view.peek(keylet::account(owner)),
                cnt,
                beast::Journal{beast::Journal::getNullSink()});
        }

        return tesSUCCESS;
    }

    if (prev)
    {
        // Under fixNFTokenPageLinks: when the emptied page is the chain's
        // tail (key ends with pageMask = nftpage_max sentinel bits), move
        // the predecessor's tokens into it and erase the predecessor.  This
        // preserves the invariant that the final page always sits at the
        // stable keylet::nftpage_max address.
        if (view.rules().enabled(fixNFTokenPageLinks) &&
            ((curr->key() & nft::kPAGE_MASK) == kPAGE_MASK))
        {
            curr->peekFieldArray(sfNFTokens) = prev->peekFieldArray(sfNFTokens);

            if (auto const prevLink = prev->at(~sfPreviousPageMin))
            {
                curr->at(sfPreviousPageMin) = *prevLink;

                // Also fix up the NextPageMin link in the new Previous.
                auto const newPrev = loadPage(curr, sfPreviousPageMin);
                newPrev->at(sfNextPageMin) = curr->key();
                view.update(newPrev);
            }
            else
            {
                curr->makeFieldAbsent(sfPreviousPageMin);
            }

            adjustOwnerCount(
                view,
                view.peek(keylet::account(owner)),
                -1,
                beast::Journal{beast::Journal::getNullSink()});

            view.update(curr);
            view.erase(prev);
            return tesSUCCESS;
        }

        if (next)
        {
            prev->setFieldH256(sfNextPageMin, next->key());
        }
        else
        {
            prev->makeFieldAbsent(sfNextPageMin);
        }

        view.update(prev);
    }

    if (next)
    {
        if (prev)
        {
            next->setFieldH256(sfPreviousPageMin, prev->key());
        }
        else
        {
            next->makeFieldAbsent(sfPreviousPageMin);
        }

        view.update(next);
    }

    view.erase(curr);

    int cnt = 1;

    // Defensively attempt to merge prev and next now that curr is gone.
    // In practice mergePages() should always return false here — tokens
    // are burned one at a time, so a single-token page between two nearly-
    // full pages is implausible — but the merge is cheap and safe.
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
findToken(ReadView const& view, AccountID const& owner, uint256 const& nftokenID)
{
    std::shared_ptr<SLE const> const page = locatePage(view, owner, nftokenID);

    if (!page)
        return std::nullopt;

    for (auto const& t : page->getFieldArray(sfNFTokens))
    {
        if (t[sfNFTokenID] == nftokenID)
            return t;
    }

    return std::nullopt;
}

std::optional<TokenAndPage>
findTokenAndPage(ApplyView& view, AccountID const& owner, uint256 const& nftokenID)
{
    std::shared_ptr<SLE> page = locatePage(view, owner, nftokenID);

    if (!page)
        return std::nullopt;

    for (auto const& t : page->getFieldArray(sfNFTokens))
    {
        if (t[sfNFTokenID] == nftokenID)
        {
            // std::optional<TokenAndPage> constructor is explicit — must spell it out.
            return std::optional<TokenAndPage>(std::in_place, t, std::move(page));
        }
    }
    return std::nullopt;
}

std::size_t
removeTokenOffersWithLimit(ApplyView& view, Keylet const& directory, std::size_t maxDeletableOffers)
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

        // Read next-page index before mutating: a fully-drained page is erased
        // by deleteTokenOffer, which would invalidate a post-deletion read.
        pageIndex = (*page)[~sfIndexNext];

        auto offerIndexes = page->getFieldV256(sfIndexes);

        // Reverse-iterate: NFTokenOffer directory pages are vector-backed and
        // deleting an entry shifts subsequent indices, so backward traversal
        // avoids index corruption without requiring a copy.
        for (int i = offerIndexes.size() - 1; i >= 0; --i)
        {
            if (auto const offer = view.peek(keylet::nftoffer(offerIndexes[i])))
            {
                if (deleteTokenOffer(view, offer))
                {
                    ++deletedOffersCount;
                }
                else
                {
                    Throw<std::runtime_error>(
                        "Offer " + to_string(offerIndexes[i]) + " cannot be deleted!");
                }
            }

            if (maxDeletableOffers == deletedOffersCount)
                break;
        }
    } while ((pageIndex.value_or(0) != 0u) && maxDeletableOffers != deletedOffersCount);

    return deletedOffersCount;
}

bool
deleteTokenOffer(ApplyView& view, std::shared_ptr<SLE> const& offer)
{
    if (offer->getType() != ltNFTOKEN_OFFER)
        return false;

    auto const owner = (*offer)[sfOwner];

    if (!view.dirRemove(keylet::ownerDir(owner), (*offer)[sfOwnerNode], offer->key(), false))
        return false;

    auto const nftokenID = (*offer)[sfNFTokenID];

    if (!view.dirRemove(
            (((*offer)[sfFlags] & lsfSellNFToken) != 0u) ? keylet::nftSells(nftokenID)
                                                         : keylet::nftBuys(nftokenID),
            (*offer)[sfNFTokenOfferNode],
            offer->key(),
            false))
        return false;

    adjustOwnerCount(
        view, view.peek(keylet::account(owner)), -1, beast::Journal{beast::Journal::getNullSink()});

    view.erase(offer);
    return true;
}

bool
repairNFTokenDirectoryLinks(ApplyView& view, AccountID const& owner)
{
    bool didRepair = false;

    auto const last = keylet::nftpageMax(owner);

    std::shared_ptr<SLE> page = view.peek(Keylet(
        ltNFTOKEN_PAGE,
        view.succ(keylet::nftpageMin(owner).key, last.key.next()).value_or(last.key)));

    if (!page)
        return didRepair;

    if (page->key() == last.key)
    {
        // There's only one page in this entire directory.  There should be
        // no links on that page.
        bool const nextPresent = page->isFieldPresent(sfNextPageMin);
        bool const prevPresent = page->isFieldPresent(sfPreviousPageMin);
        if (nextPresent || prevPresent)
        {
            didRepair = true;
            if (prevPresent)
                page->makeFieldAbsent(sfPreviousPageMin);
            if (nextPresent)
                page->makeFieldAbsent(sfNextPageMin);
            view.update(page);
        }
        return didRepair;
    }

    // First page is not the same as last page.  The first page should not
    // contain a previous link.
    if (page->isFieldPresent(sfPreviousPageMin))
    {
        didRepair = true;
        page->makeFieldAbsent(sfPreviousPageMin);
        view.update(page);
    }

    std::shared_ptr<SLE> nextPage;
    while (
        (nextPage = view.peek(Keylet(
             ltNFTOKEN_PAGE, view.succ(page->key().next(), last.key.next()).value_or(last.key)))))
    {
        if (!page->isFieldPresent(sfNextPageMin) ||
            page->getFieldH256(sfNextPageMin) != nextPage->key())
        {
            didRepair = true;
            page->setFieldH256(sfNextPageMin, nextPage->key());
            view.update(page);
        }

        if (!nextPage->isFieldPresent(sfPreviousPageMin) ||
            nextPage->getFieldH256(sfPreviousPageMin) != page->key())
        {
            didRepair = true;
            nextPage->setFieldH256(sfPreviousPageMin, page->key());
            view.update(nextPage);
        }

        if (nextPage->key() == last.key)
            break;

        page = nextPage;
    }

    if (!nextPage)
    {
        // `page` is the last page but does not carry the expected
        // nftpage_max key — migrate its tokens to a new SLE with the
        // correct key.  Owner count is unchanged because the old page is
        // removed at the same time.
        didRepair = true;
        nextPage = std::make_shared<SLE>(last);

        nextPage->peekFieldArray(sfNFTokens) = page->peekFieldArray(sfNFTokens);

        if (auto const prevLink = page->at(~sfPreviousPageMin))
        {
            nextPage->at(sfPreviousPageMin) = *prevLink;

            // Also fix up the NextPageMin link in the new Previous.
            auto const newPrev = view.peek(Keylet(ltNFTOKEN_PAGE, *prevLink));
            if (!newPrev)
            {
                Throw<std::runtime_error>(
                    "NFTokenPage directory for " + to_string(owner) +
                    " cannot be repaired.  Unexpected link problem.");
            }
            newPrev->at(sfNextPageMin) = nextPage->key();
            view.update(newPrev);
        }
        view.erase(page);
        view.insert(nextPage);
        return didRepair;
    }

    XRPL_ASSERT(nextPage, "xrpl::nft::repairNFTokenDirectoryLinks : next page is available");
    if (nextPage->isFieldPresent(sfNextPageMin))
    {
        didRepair = true;
        nextPage->makeFieldAbsent(sfNextPageMin);
        view.update(nextPage);
    }
    return didRepair;
}

NotTEC
tokenOfferCreatePreflight(
    AccountID const& acctID,
    STAmount const& amount,
    std::optional<AccountID> const& dest,
    std::optional<std::uint32_t> const& expiration,
    std::uint16_t nftFlags,
    Rules const& rules,
    std::optional<AccountID> const& owner,
    std::uint32_t txFlags)
{
    if (amount.negative())
        return temBAD_AMOUNT;

    if (!isXRP(amount))
    {
        if ((nftFlags & nft::kFLAG_ONLY_XRP) != 0)
            return temBAD_AMOUNT;

        if (!amount)
            return temBAD_AMOUNT;
    }

    // Buy offers must carry a non-zero amount; sell offers may ask for nothing.
    bool const isSellOffer = (txFlags & tfSellNFToken) != 0u;
    if (!isSellOffer && !amount)
        return temBAD_AMOUNT;

    if (expiration.has_value() && expiration.value() == 0)
        return temBAD_EXPIRATION;

    // sfOwner must be present for buy offers (identifies token holder) and
    // absent for sell offers (seller is implicit).
    if (owner.has_value() == isSellOffer)
        return temMALFORMED;

    if (owner && owner == acctID)
        return temMALFORMED;

    if (dest && dest == acctID)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
tokenOfferCreatePreclaim(
    ReadView const& view,
    AccountID const& acctID,
    AccountID const& nftIssuer,
    STAmount const& amount,
    std::optional<AccountID> const& dest,
    std::uint16_t nftFlags,
    std::uint16_t xferFee,
    beast::Journal j,
    std::optional<AccountID> const& owner,
    std::uint32_t txFlags)
{
    if (((nftFlags & nft::kFLAG_CREATE_TRUST_LINES) == 0) && !amount.native() && (xferFee != 0u))
    {
        if (!view.exists(keylet::account(nftIssuer)))
            return tecNO_ISSUER;

        // Under featureNFTokenMintOffer, an IOU issuer paying royalties in
        // their own currency needs no trust line to themselves.
        if (view.rules().enabled(featureNFTokenMintOffer))
        {
            if (nftIssuer != amount.getIssuer() &&
                !view.read(keylet::line(nftIssuer, amount.get<Issue>())))
                return tecNO_LINE;
        }
        else if (!view.exists(keylet::line(nftIssuer, amount.get<Issue>())))
        {
            return tecNO_LINE;
        }

        if (isFrozen(view, nftIssuer, amount.get<Issue>().currency, amount.getIssuer()))
            return tecFROZEN;
    }

    if (nftIssuer != acctID && ((nftFlags & nft::kFLAG_TRANSFERABLE) == 0))
    {
        auto const root = view.read(keylet::account(nftIssuer));
        XRPL_ASSERT(root, "xrpl::nft::tokenOfferCreatePreclaim : non-null account");

        if (auto minter = (*root)[~sfNFTokenMinter]; minter != acctID)
            return tefNFTOKEN_IS_NOT_TRANSFERABLE;
    }

    if (isFrozen(view, acctID, amount.get<Issue>().currency, amount.getIssuer()))
        return tecFROZEN;

    // Buy offers must be funded at submission time; funds are not reserved
    // and the offer may later become unfunded.  IOU issuers may use their
    // own currency, so accountFunds with ZeroIfFrozen is the correct check.
    if ((txFlags & tfSellNFToken) == 0)
    {
        if (accountFunds(view, acctID, amount, FreezeHandling::ZeroIfFrozen, j).signum() <= 0)
            return tecUNFUNDED_OFFER;
    }

    if (dest)
    {
        auto const sleDst = view.read(keylet::account(*dest));

        if (!sleDst)
            return tecNO_DST;

        if ((sleDst->getFlags() & lsfDisallowIncomingNFTokenOffer) != 0u)
            return tecNO_PERMISSION;
    }

    if (owner)
    {
        auto const sleOwner = view.read(keylet::account(*owner));

        if (!sleOwner)
            return tecNO_TARGET;

        if ((sleOwner->getFlags() & lsfDisallowIncomingNFTokenOffer) != 0u)
            return tecNO_PERMISSION;
    }

    if (view.rules().enabled(fixEnforceNFTokenTrustlineV2) && !amount.native())
    {
        // Even for buy offers where we already checked balance via
        // accountFunds, trust-line authorization must be verified separately:
        // unauthorized trust lines can carry a non-zero balance.
        auto const res =
            nft::checkTrustlineAuthorized(view, acctID, j, amount.asset().get<Issue>());
        if (!isTesSuccess(res))
            return res;
    }
    return tesSUCCESS;
}

TER
tokenOfferCreateApply(
    ApplyView& view,
    AccountID const& acctID,
    STAmount const& amount,
    std::optional<AccountID> const& dest,
    std::optional<std::uint32_t> const& expiration,
    SeqProxy seqProxy,
    uint256 const& nftokenID,
    XRPAmount const& priorBalance,
    beast::Journal j,
    std::uint32_t txFlags)
{
    Keylet const acctKeylet = keylet::account(acctID);
    if (auto const acct = view.read(acctKeylet);
        priorBalance < view.fees().accountReserve((*acct)[sfOwnerCount] + 1))
        return tecINSUFFICIENT_RESERVE;

    auto const offerID = keylet::nftoffer(acctID, seqProxy.value());

    {
        auto const ownerNode =
            view.dirInsert(keylet::ownerDir(acctID), offerID, describeOwnerDir(acctID));

        if (!ownerNode)
            return tecDIR_FULL;  // LCOV_EXCL_LINE

        bool const isSellOffer = (txFlags & tfSellNFToken) != 0u;

        auto const offerNode = view.dirInsert(
            isSellOffer ? keylet::nftSells(nftokenID) : keylet::nftBuys(nftokenID),
            offerID,
            [&nftokenID, isSellOffer](std::shared_ptr<SLE> const& sle) {
                (*sle)[sfFlags] = isSellOffer ? lsfNFTokenSellOffers : lsfNFTokenBuyOffers;
                (*sle)[sfNFTokenID] = nftokenID;
            });

        if (!offerNode)
            return tecDIR_FULL;  // LCOV_EXCL_LINE

        std::uint32_t sleFlags = 0;

        if (isSellOffer)
            sleFlags |= lsfSellNFToken;

        auto offer = std::make_shared<SLE>(offerID);
        (*offer)[sfOwner] = acctID;
        (*offer)[sfNFTokenID] = nftokenID;
        (*offer)[sfAmount] = amount;
        (*offer)[sfFlags] = sleFlags;
        (*offer)[sfOwnerNode] = *ownerNode;
        (*offer)[sfNFTokenOfferNode] = *offerNode;

        if (expiration)
            (*offer)[sfExpiration] = *expiration;

        if (dest)
            (*offer)[sfDestination] = *dest;

        view.insert(offer);
    }

    adjustOwnerCount(view, view.peek(acctKeylet), 1, j);

    return tesSUCCESS;
}

TER
checkTrustlineAuthorized(
    ReadView const& view,
    AccountID const id,
    beast::Journal const j,
    Issue const& issue)
{
    XRPL_ASSERT(!isXRP(issue.currency), "xrpl::nft::checkTrustlineAuthorized : valid to check.");

    if (view.rules().enabled(fixEnforceNFTokenTrustlineV2))
    {
        auto const issuerAccount = view.read(keylet::account(issue.account));
        if (!issuerAccount)
        {
            JLOG(j.debug()) << "xrpl::nft::checkTrustlineAuthorized: can't "
                               "receive IOUs from non-existent issuer: "
                            << to_string(issue.account);

            return tecNO_ISSUER;
        }

        // An issuer cannot hold a trust line to itself, so no authorization
        // check is possible or needed.
        if (issue.account == id)
            return tesSUCCESS;

        if (issuerAccount->isFlag(lsfRequireAuth))
        {
            auto const trustLine = view.read(keylet::line(id, issue.account, issue.currency));

            if (!trustLine)
                return tecNO_LINE;

            // Trust-line endpoints are stored in canonical order determined
            // by AccountID comparison (strict weak ordering).  The flag slot
            // (lsfLowAuth vs lsfHighAuth) therefore depends on which side
            // `id` occupies.
            if (!trustLine->isFlag(id > issue.account ? lsfLowAuth : lsfHighAuth))
                return tecNO_AUTH;
        }
    }

    return tesSUCCESS;
}

TER
checkTrustlineDeepFrozen(
    ReadView const& view,
    AccountID const id,
    beast::Journal const j,
    Issue const& issue)
{
    XRPL_ASSERT(!isXRP(issue.currency), "xrpl::nft::checkTrustlineDeepFrozen : valid to check.");

    if (view.rules().enabled(featureDeepFreeze))
    {
        auto const issuerAccount = view.read(keylet::account(issue.account));
        if (!issuerAccount)
        {
            JLOG(j.debug()) << "xrpl::nft::checkTrustlineDeepFrozen: can't "
                               "receive IOUs from non-existent issuer: "
                            << to_string(issue.account);

            return tecNO_ISSUER;
        }

        // An issuer cannot hold a trust line to itself, so no freeze check
        // is possible or needed.
        if (issue.account == id)
            return tesSUCCESS;

        auto const trustLine = view.read(keylet::line(id, issue.account, issue.currency));

        if (!trustLine)
            return tesSUCCESS;

        // Either side may enact deep freeze; check both flag slots.
        bool const deepFrozen =
            ((*trustLine)[sfFlags] & (lsfLowDeepFreeze | lsfHighDeepFreeze)) != 0u;

        if (deepFrozen)
            return tecFROZEN;
    }

    return tesSUCCESS;
}

}  // namespace xrpl::nft
