/**
 * @file NFTInvariant.cpp
 * @brief Post-transaction invariant checkers for the NFT subsystem.
 *
 * Implements `ValidNFTokenPage` (structural integrity of `ltNFTOKEN_PAGE`
 * entries) and `NFTokenCountTracking` (global mint/burn tally consistency on
 * `ltACCOUNT_ROOT` entries). Both follow the two-phase invariant protocol:
 * `visitEntry` accumulates state across every ledger entry touched by the
 * transaction; `finalize` renders a pass/fail verdict once, after all entries
 * have been visited.
 *
 * These classes are driven exclusively by the invariant-check harness in
 * `ApplyContext`; they are not called directly by application code.
 */
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

/**
 * @brief Inspect one touched `ltNFTOKEN_PAGE` entry for structural integrity.
 *
 * Non-NFT-page SLEs are silently ignored. For each snapshot (`before` and
 * `after`) that is present, the inner `check` lambda verifies:
 *  - Link ownership: `sfPreviousPageMin` / `sfNextPageMin` high 160 bits
 *    match the current page's owning account; low 96 bits are strictly ordered.
 *  - Size: 1–`kDIR_MAX_TOKENS_PER_PAGE` (32) tokens, unless the page is being
 *    deleted (empty is then permitted).
 *  - Membership: each token's page-bits fall in `[loLimit, hiLimit)`.
 *  - Sort: tokens are strictly ascending by `nft::compareTokens`.
 *  - URI validity: an `sfURI` field, if present, must be non-empty.
 *
 * Two additional checks operate across the `before`→`after` transition rather
 * than on a single snapshot:
 *  - Deletion of the final page (all 96 page-bits set to 1) while
 *    `sfPreviousPageMin` is still present would orphan the rest of the
 *    directory, so `deletedFinalPage_` is set. Enforced only after
 *    `fixNFTokenPageLinks` activates.
 *  - Loss of `sfNextPageMin` on a non-final page between `before` and `after`
 *    indicates a broken forward chain, so `deletedLink_` is set. Also gated on
 *    `fixNFTokenPageLinks`.
 *
 * Failures are recorded in boolean flags; `finalize` reports them.
 *
 * @param isDelete  True if the SLE is being removed from the ledger.
 * @param before    Snapshot of the entry before the transaction, or nullptr if
 *     the entry is being created.
 * @param after     Snapshot of the entry after the transaction, or nullptr if
 *     the entry is being deleted.
 */
void
ValidNFTokenPage::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    static constexpr uint256 const& kPAGE_BITS = nft::kPAGE_MASK;
    static constexpr uint256 const kACCOUNT_BITS = ~kPAGE_BITS;

    if ((before && before->getType() != ltNFTOKEN_PAGE) ||
        (after && after->getType() != ltNFTOKEN_PAGE))
        return;

    auto check = [this, isDelete](std::shared_ptr<SLE const> const& sle) {
        uint256 const account = sle->key() & kACCOUNT_BITS;
        uint256 const hiLimit = sle->key() & kPAGE_BITS;
        std::optional<uint256> const prev = (*sle)[~sfPreviousPageMin];

        if (prev)
        {
            if (account != (*prev & kACCOUNT_BITS))
                badLink_ = true;

            if (hiLimit <= (*prev & kPAGE_BITS))
                badLink_ = true;
        }

        if (auto const next = (*sle)[~sfNextPageMin])
        {
            if (account != (*next & kACCOUNT_BITS))
                badLink_ = true;

            if (hiLimit >= (*next & kPAGE_BITS))
                badLink_ = true;
        }

        {
            auto const& nftokens = sle->getFieldArray(sfNFTokens);

            if (std::size_t const nftokenCount = nftokens.size();
                (!isDelete && nftokenCount == 0) || nftokenCount > kDIR_MAX_TOKENS_PER_PAGE)
                invalidSize_ = true;

            // Lower bound for token IDs on this page. Derived from the
            // page-bits of sfPreviousPageMin; zero if no previous page exists.
            uint256 const loLimit = prev ? *prev & kPAGE_BITS : uint256(beast::kZERO);

            uint256 loCmp = loLimit;
            for (auto const& obj : nftokens)
            {
                uint256 const tokenID = obj[sfNFTokenID];
                if (!nft::compareTokens(loCmp, tokenID))
                    badSort_ = true;
                loCmp = tokenID;

                if (uint256 const tokenPageBits = tokenID & kPAGE_BITS;
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

        // Deleting the final page (all 96 page-bits == 1) while
        // sfPreviousPageMin is present would orphan the preceding pages.
        if (isDelete && (before->key() & nft::kPAGE_MASK) == nft::kPAGE_MASK &&
            before->isFieldPresent(sfPreviousPageMin))
        {
            deletedFinalPage_ = true;
        }
    }

    if (after)
        check(after);

    if (!isDelete && before && after)
    {
        // A non-final page that loses sfNextPageMin breaks the forward chain.
        if ((before->key() & nft::kPAGE_MASK) != nft::kPAGE_MASK &&
            before->isFieldPresent(sfNextPageMin) && !after->isFieldPresent(sfNextPageMin))
        {
            deletedLink_ = true;
        }
    }
}

/**
 * @brief Render a pass/fail verdict for `ValidNFTokenPage`.
 *
 * Reports the first accumulated failure flag and returns `false` (causing
 * `tecINVARIANT_FAILED`). The `deletedFinalPage_` and `deletedLink_` checks
 * are only enforced when the `fixNFTokenPageLinks` amendment is active,
 * allowing pre-amendment historical replay to proceed without triggering
 * these conditions.
 *
 * @param tx      The transaction being applied (unused here).
 * @param result  The transaction result code (unused here).
 * @param view    The post-transaction ledger view; used to test amendment
 *     activation for `fixNFTokenPageLinks`.
 * @param j       Journal for fatal-level diagnostics on failure.
 * @return True if all NFT page invariants pass; false otherwise.
 */
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

/**
 * @brief Accumulate `sfMintedNFTokens` / `sfBurnedNFTokens` totals.
 *
 * Sums the pre- and post-transaction values of both NFT count fields across
 * every `ltACCOUNT_ROOT` entry touched by the transaction. Non-account-root
 * SLEs are silently skipped. `value_or(0)` handles accounts that have never
 * minted or burned any NFTs (field absent).
 *
 * Tracking global totals across all touched roots, rather than per-account
 * deltas, is intentional: an NFT mint or burn legitimately affects exactly one
 * account's counters, so global sum equality is both necessary and sufficient.
 *
 * @param before  Pre-transaction snapshot, or nullptr for new entries.
 * @param after   Post-transaction snapshot, or nullptr for deleted entries.
 */
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

/**
 * @brief Validate NFT mint/burn count invariants for the completed transaction.
 *
 * Branches on whether the transaction holds the `ChangeNftCounts` privilege:
 *
 * **Without privilege**: neither `sfMintedNFTokens` nor `sfBurnedNFTokens`
 * may change — any modification indicates a bug in a non-NFT transaction.
 *
 * **With privilege** (only `ttNFTOKEN_MINT` and `ttNFTOKEN_BURN` carry this):
 *  - Successful `ttNFTOKEN_MINT`: minted total must strictly increase; burned
 *    total must be unchanged.
 *  - Failed `ttNFTOKEN_MINT`: both totals must be unchanged.
 *  - Successful `ttNFTOKEN_BURN`: burned total must strictly increase; minted
 *    total must be unchanged.
 *  - Failed `ttNFTOKEN_BURN`: both totals must be unchanged.
 *
 * Strict inequality (`>=` rather than `==`) on success is intentional: it
 * catches counter wrap-around and incorrect field rewrites, not just the
 * case where nothing changed at all.
 *
 * @param tx      The transaction; used for type and privilege checks.
 * @param result  The transaction result code; distinguishes success from
 *     failure for mint/burn.
 * @param j       Journal for fatal-level diagnostics on failure.
 * @return True if all count invariants pass; false otherwise.
 */
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
