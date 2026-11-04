#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <set>
#include <vector>

namespace xrpl {

/**
 * @brief Invariants: perpetual futures exchanges, markets, margin accounts, positions and orders
 * are internally consistent (XLS perpetual futures, section 17).
 *
 * 1. For each exchange whose entry or pseudo-account holding was modified, the pseudo-account's
 *    balance of `sfAsset` equals `sfAssetsTotal`.
 * 2. `sfAssetsTotal = sfCashTotal + sfInsuranceFund + sfCoverAvailable + sfOwnerFees +
 *    sfSettlementBalance - sfBadDebt`, and the change in `sfCashTotal` equals the sum of the
 *    changes in `sfCash` over the exchange's modified margin accounts.
 * 3. In a Live market, the sizes of the modified positions sum to an unchanged total, and
 *    `sfOpenInterestLong == sfOpenInterestShort`.
 * 4. In a Settled market, a position's size changes only to zero, and a market already Settled
 *    keeps its frozen `sfIndexPrice`.
 * 5. The change in `sfOpenInterestLong` equals the summed change in max(size, 0) over the
 *    market's modified positions, and the change in `sfOpenInterestShort` that in max(-size, 0).
 * 6. `sfInsuranceFund`, `sfCoverAvailable`, `sfOwnerFees`, `sfBadDebt` and `sfWaterfallCredit`
 *    are not negative, and a margin account with no `sfMarkets` has `sfCash >= 0`.
 * 7. `sfLossIndexLong` and `sfLossIndexShort` never fall.
 * 8. Every position `sfSize`, `sfOpenBuy` and `sfOpenSell` and every order `sfSize` is a
 *    multiple of its market's `sfLotSize`.
 * 9. A deleted position had zero size and zero `sfOrderCount`; a deleted margin account had zero
 *    cash, zero waterfall credit and no markets.
 * 10. A margin account lists at most kMaxMarginPositions markets, and a position in an
 *     isolated-only market is its account's only position.
 *
 * Each check runs whatever the transaction result.
 */
class ValidPerp
{
    /**
     * One visited entry: before is null when created, and after is the state at deletion when
     * deleted.
     */
    struct Change
    {
        SLE::const_pointer before;
        SLE::const_pointer after;
        bool deleted{};
    };

    std::vector<Change> exchanges_;
    std::vector<Change> markets_;
    std::vector<Change> accounts_;
    std::vector<Change> positions_;
    std::vector<Change> orders_;
    // Exchanges whose pseudo-account AccountRoot was modified.
    std::set<uint256> pseudoExchanges_;
    // Modified trust lines and MPTokens, searched for exchange pseudo-accounts.
    std::vector<SLE::const_pointer> holdings_;

public:
    void
    visitEntry(bool, SLE::ConstRef, SLE::ConstRef);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
