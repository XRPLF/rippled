#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>

namespace xrpl {

/**
 * @brief Invariant: Validates several invariants for NFToken pages.
 *
 * The following checks are made:
 *  - The page is correctly associated with the owner.
 *  - The page is correctly ordered between the next and previous links.
 *  - The page contains at least one and no more than 32 NFTokens.
 *  - The NFTokens on this page do not belong on a lower or higher page.
 *  - The NFTokens are correctly sorted on the page.
 *  - Each URI, if present, is not empty.
 */
class ValidNFTokenPage
{
    bool badEntry_ = false;
    bool badLink_ = false;
    bool badSort_ = false;
    bool badURI_ = false;
    bool invalidSize_ = false;
    bool deletedFinalPage_ = false;
    bool deletedLink_ = false;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

/**
 * @brief Invariant: Validates counts of NFTokens after all transaction types.
 *
 * The following checks are made:
 *  - The number of minted or burned NFTokens can only be changed by
 *    NFTokenMint or NFTokenBurn transactions.
 *  - A successful NFTokenMint must increase the number of NFTokens.
 *  - A failed NFTokenMint must not change the number of minted NFTokens.
 *  - An NFTokenMint transaction cannot change the number of burned NFTokens.
 *  - A successful NFTokenBurn must increase the number of burned NFTokens.
 *  - A failed NFTokenBurn must not change the number of burned NFTokens.
 *  - An NFTokenBurn transaction cannot change the number of minted NFTokens.
 */
class NFTokenCountTracking
{
    std::uint32_t beforeMintedTotal = 0;
    std::uint32_t beforeBurnedTotal = 0;
    std::uint32_t afterMintedTotal = 0;
    std::uint32_t afterBurnedTotal = 0;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
