#include <xrpl/tx/invariants/NFTInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/nftPageMask.h>
#include <xrpl/tx/invariants/InvariantCheckPrivilege.h>

#include <cstddef>
#include <memory>
#include <optional>

namespace xrpl {

void
ValidNFTokenPage::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    static constexpr uint256 const& kPageBits = nft::kPageMask;
    static constexpr uint256 kAccountBits = ~kPageBits;

    if ((before && before->getType() != ltNFTOKEN_PAGE) ||
        (after && after->getType() != ltNFTOKEN_PAGE))
        return;

    auto check = [this, isDelete](std::shared_ptr<SLE const> const& sle) {
        uint256 const account = sle->key() & kAccountBits;
        uint256 const hiLimit = sle->key() & kPageBits;
        std::optional<uint256> const prev = (*sle)[~sfPreviousPageMin];

        // Make sure that any page links...
        //  1. Are properly associated with the owning account and
        //  2. The page is correctly ordered between links.
        if (prev)
        {
            if (account != (*prev & kAccountBits))
                badLink_ = true;

            if (hiLimit <= (*prev & kPageBits))
                badLink_ = true;
        }

        if (auto const next = (*sle)[~sfNextPageMin])
        {
            if (account != (*next & kAccountBits))
                badLink_ = true;

            if (hiLimit >= (*next & kPageBits))
                badLink_ = true;
        }

        {
            auto const& nftokens = sle->getFieldArray(sfNFTokens);

            // An NFTokenPage should never contain too many tokens or be empty.
            if (std::size_t const nftokenCount = nftokens.size();
                (!isDelete && nftokenCount == 0) || nftokenCount > kDirMaxTokensPerPage)
                invalidSize_ = true;

            // If prev is valid, use it to establish a lower bound for
            // page entries.  If prev is not valid the lower bound is zero.
            uint256 const loLimit = prev ? *prev & kPageBits : uint256(beast::kZero);

            // Also verify that all NFTokenIDs in the page are sorted.
            uint256 loCmp = loLimit;
            for (auto const& obj : nftokens)
            {
                uint256 const tokenID = obj[sfNFTokenID];
                if (!nft::compareTokens(loCmp, tokenID))
                    badSort_ = true;
                loCmp = tokenID;

                // None of the NFTs on this page should belong on lower or
                // higher pages.
                if (uint256 const tokenPageBits = tokenID & kPageBits;
                    tokenPageBits < loLimit || tokenPageBits >= hiLimit)
                    badEntry_ = true;

                if (auto uri = obj[~sfURI]; uri && uri->empty())
                    badURI_ = true;
            }
        }
    };

    if (before)
    {
        check(before);

        // While an account's NFToken directory contains any NFTokens, the last
        // NFTokenPage (with 96 bits of 1 in the low part of the index) should
        // never be deleted.
        if (isDelete && (before->key() & nft::kPageMask) == nft::kPageMask &&
            before->isFieldPresent(sfPreviousPageMin))
        {
            deletedFinalPage_ = true;
        }
    }

    if (after)
        check(after);

    if (!isDelete && before && after)
    {
        // If the NFTokenPage
        //  1. Has a NextMinPage field in before, but loses it in after, and
        //  2. This is not the last page in the directory
        // Then we have identified a corruption in the links between the
        // NFToken pages in the NFToken directory.
        if ((before->key() & nft::kPageMask) != nft::kPageMask &&
            before->isFieldPresent(sfNextPageMin) && !after->isFieldPresent(sfNextPageMin))
        {
            deletedLink_ = true;
        }
    }
}

bool
ValidNFTokenPage::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j) const
{
    if (badLink_)
    {
        JLOG(j.fatal()) << "Invariant failed: NFT page is improperly linked.";
        return false;
    }

    if (badEntry_)
    {
        JLOG(j.fatal()) << "Invariant failed: NFT found in incorrect page.";
        return false;
    }

    if (badSort_)
    {
        JLOG(j.fatal()) << "Invariant failed: NFTs on page are not sorted.";
        return false;
    }

    if (badURI_)
    {
        JLOG(j.fatal()) << "Invariant failed: NFT contains empty URI.";
        return false;
    }

    if (invalidSize_)
    {
        JLOG(j.fatal()) << "Invariant failed: NFT page has invalid size.";
        return false;
    }

    if (view.rules().enabled(fixNFTokenPageLinks))
    {
        if (deletedFinalPage_)
        {
            JLOG(j.fatal()) << "Invariant failed: Last NFT page deleted with "
                               "non-empty directory.";
            return false;
        }
        if (deletedLink_)
        {
            JLOG(j.fatal()) << "Invariant failed: Lost NextMinPage link.";
            return false;
        }
    }

    return true;
}

//------------------------------------------------------------------------------
void
NFTokenCountTracking::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (before && before->getType() == ltACCOUNT_ROOT)
    {
        beforeMintedTotal_ += (*before)[~sfMintedNFTokens].value_or(0);
        beforeBurnedTotal_ += (*before)[~sfBurnedNFTokens].value_or(0);
    }

    if (after && after->getType() == ltACCOUNT_ROOT)
    {
        afterMintedTotal_ += (*after)[~sfMintedNFTokens].value_or(0);
        afterBurnedTotal_ += (*after)[~sfBurnedNFTokens].value_or(0);
    }
}

bool
NFTokenCountTracking::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j) const
{
    if (!hasPrivilege(tx, ChangeNftCounts))
    {
        if (beforeMintedTotal_ != afterMintedTotal_)
        {
            JLOG(j.fatal()) << "Invariant failed: the number of minted tokens "
                               "changed without a mint transaction!";
            return false;
        }

        if (beforeBurnedTotal_ != afterBurnedTotal_)
        {
            JLOG(j.fatal()) << "Invariant failed: the number of burned tokens "
                               "changed without a burn transaction!";
            return false;
        }

        return true;
    }

    if (tx.getTxnType() == ttNFTOKEN_MINT)
    {
        if (isTesSuccess(result) && beforeMintedTotal_ >= afterMintedTotal_)
        {
            JLOG(j.fatal()) << "Invariant failed: successful minting didn't increase "
                               "the number of minted tokens.";
            return false;
        }

        if (!isTesSuccess(result) && beforeMintedTotal_ != afterMintedTotal_)
        {
            JLOG(j.fatal()) << "Invariant failed: failed minting changed the "
                               "number of minted tokens.";
            return false;
        }

        if (beforeBurnedTotal_ != afterBurnedTotal_)
        {
            JLOG(j.fatal()) << "Invariant failed: minting changed the number of "
                               "burned tokens.";
            return false;
        }
    }

    if (tx.getTxnType() == ttNFTOKEN_BURN)
    {
        if (isTesSuccess(result))
        {
            if (beforeBurnedTotal_ >= afterBurnedTotal_)
            {
                JLOG(j.fatal()) << "Invariant failed: successful burning didn't increase "
                                   "the number of burned tokens.";
                return false;
            }
        }

        if (!isTesSuccess(result) && beforeBurnedTotal_ != afterBurnedTotal_)
        {
            JLOG(j.fatal()) << "Invariant failed: failed burning changed the "
                               "number of burned tokens.";
            return false;
        }

        if (beforeMintedTotal_ != afterMintedTotal_)
        {
            JLOG(j.fatal()) << "Invariant failed: burning changed the number of "
                               "minted tokens.";
            return false;
        }
    }

    return true;
}

}  // namespace xrpl
