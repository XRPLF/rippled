#include <xrpl/tx/invariants/PerpInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace xrpl {

namespace {

/**
 * The balance of asset an account holds, read from its AccountRoot, trust line or MPToken.
 */
[[nodiscard]] Number
heldBalance(ReadView const& view, AccountID const& holder, Asset const& asset)
{
    return asset.visit(
        [&](Issue const& issue) -> Number {
            if (isXRP(issue))
            {
                auto const root = view.read(keylet::account(holder));
                return root ? Number{root->getFieldAmount(sfBalance).xrp().drops()} : Number{};
            }
            auto const line = view.read(keylet::trustLine(holder, issue));
            if (!line)
                return Number{};
            // The trust line balance is the low account's holding.
            STAmount balance = line->getFieldAmount(sfBalance);
            if (holder > issue.account)
                balance.negate();
            return balance;
        },
        [&](MPTIssue const& issue) -> Number {
            auto const token = view.read(keylet::mptoken(issue.getMptID(), holder));
            return token ? Number{static_cast<std::int64_t>((*token)[sfMPTAmount])} : Number{};
        });
}

/**
 * A position's size before the transaction and after it, zero for a created or deleted position.
 */
struct SizeChange
{
    Number before;
    Number after;
};

}  // namespace

void
ValidPerp::visitEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after)
{
    XRPL_ASSERT(after, "xrpl::ValidPerp::visitEntry : after is set");
    if (!after)
        return;  // LCOV_EXCL_LINE

    Change const change{.before = before, .after = after, .deleted = isDelete};
    switch (after->getType())
    {
        case ltPERP_EXCHANGE:
            exchanges_.push_back(change);
            break;
        case ltPERP_MARKET:
            markets_.push_back(change);
            break;
        case ltMARGIN_ACCOUNT:
            accounts_.push_back(change);
            break;
        case ltMARGIN_POSITION:
            positions_.push_back(change);
            break;
        case ltPERP_ORDER:
            orders_.push_back(change);
            break;
        case ltACCOUNT_ROOT:
            if (!isDelete && after->isFieldPresent(sfPerpExchangeID))
                pseudoExchanges_.insert((*after)[sfPerpExchangeID]);
            break;
        case ltRIPPLE_STATE:
        case ltMPTOKEN:
            holdings_.push_back(after);
            break;
        default:
            break;
    }
}

bool
ValidPerp::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    bool ok = true;
    auto const fail = [&](std::string const& message) {
        JLOG(j.fatal()) << "Invariant failed: " << message;
        ok = false;
    };

    // The lot size of a market, or nullopt when the market is missing.
    auto const lotSize = [&](uint256 const& marketID) -> std::optional<Number> {
        auto const market = view.read(keylet::perpMarket(marketID));
        if (!market)
            return std::nullopt;
        return Number{(*market)[sfLotSize]};
    };
    auto const checkLots = [&](STLedgerEntry const& entry,
                               std::vector<SF_NUMBER const*> const& fields) {
        auto const lot = lotSize(entry[sfMarketID]);
        if (!lot)
        {
            fail("perp entry's market is missing");
            return;
        }
        for (auto const* field : fields)
        {
            if (!perp::isMultiple(entry[*field], *lot))
                fail("perp size is not a multiple of the lot size");
        }
    };

    // 6. Exchange balances are not negative.
    for (auto const& change : exchanges_)
    {
        if (change.deleted)
            continue;
        for (auto const* field : {&sfInsuranceFund, &sfCoverAvailable, &sfOwnerFees, &sfBadDebt})
        {
            if ((*change.after)[*field] < Number{})
                fail("perp exchange " + field->getName() + " is negative");
        }
    }

    // 7. Loss indexes never fall.
    for (auto const& change : markets_)
    {
        if (change.deleted || !change.before)
            continue;
        if ((*change.after)[sfLossIndexLong] < (*change.before)[sfLossIndexLong] ||
            (*change.after)[sfLossIndexShort] < (*change.before)[sfLossIndexShort])
            fail("perp market loss index fell");
    }

    // 6, 9 and 10 on margin accounts.
    for (auto const& change : accounts_)
    {
        auto const& account = *change.after;
        auto const markets = account.getFieldV256(sfMarkets);
        if (change.deleted)
        {
            if (account[sfCash] != Number{} || account[sfWaterfallCredit] != Number{} ||
                !markets.empty())
                fail("perp margin account deleted with cash, credit or markets");
            continue;
        }
        if (account[sfWaterfallCredit] < Number{})
            fail("perp margin account waterfall credit is negative");
        if (markets.empty() && account[sfCash] < Number{})
            fail("perp margin account without markets has negative cash");
        if (markets.size() > perp::kMaxMarginPositions)
            fail("perp margin account lists too many markets");
        if (markets.size() > 1 && std::ranges::any_of(markets, [&](uint256 const& marketID) {
                auto const market = view.read(keylet::perpMarket(marketID));
                return market && market->isFlag(lsfPerpIsolatedOnly);
            }))
            fail("perp isolated-only position is not its account's only position");
    }

    // 8 and 9 on positions and orders.
    for (auto const& change : positions_)
    {
        auto const& position = *change.after;
        if (change.deleted)
        {
            if (position[sfSize] != Number{} || position[sfOrderCount] != 0)
                fail("perp position deleted with size or orders");
            continue;
        }
        checkLots(position, {&sfSize, &sfOpenBuy, &sfOpenSell});
    }
    for (auto const& change : orders_)
    {
        if (!change.deleted)
            checkLots(*change.after, {&sfSize});
    }

    // 1 and 2 for every exchange whose entry, margin accounts or pseudo-account holding changed.
    struct ExchangeSum
    {
        Change const* change = nullptr;
        Number cash;
    };
    std::map<uint256, ExchangeSum> exchanges;
    for (auto const& change : exchanges_)
        exchanges[change.after->key()].change = &change;
    for (auto const& change : accounts_)
    {
        Number const before = change.before ? Number{(*change.before)[sfCash]} : Number{};
        Number const after = change.deleted ? Number{} : Number{(*change.after)[sfCash]};
        auto& sum = exchanges[(*change.after)[sfExchangeID]];
        sum.cash = sum.cash + (after - before);
    }
    for (auto const& exchangeID : pseudoExchanges_)
        exchanges.try_emplace(exchangeID);
    if (view.rules().enabled(featurePerpetualFutures))
    {
        auto const addPseudo = [&](AccountID const& id) {
            auto const root = view.read(keylet::account(id));
            if (root && root->isFieldPresent(sfPerpExchangeID))
                exchanges.try_emplace((*root)[sfPerpExchangeID]);
        };
        for (auto const& holding : holdings_)
        {
            if (holding->getType() == ltMPTOKEN)
            {
                addPseudo((*holding)[sfAccount]);
                continue;
            }
            addPseudo(holding->getFieldAmount(sfLowLimit).getIssuer());
            addPseudo(holding->getFieldAmount(sfHighLimit).getIssuer());
        }
    }

    for (auto const& [exchangeID, sum] : exchanges)
    {
        if ((sum.change != nullptr) && sum.change->deleted)
            continue;
        auto const after = (sum.change != nullptr) ? sum.change->after
                                                   : view.read(keylet::perpExchange(exchangeID));
        if (!after)
        {
            fail("perp exchange is missing");
            continue;
        }
        auto const& exchange = *after;
        Number const assets = exchange[sfAssetsTotal];

        if (assets !=
            exchange[sfCashTotal] + exchange[sfInsuranceFund] + exchange[sfCoverAvailable] +
                exchange[sfOwnerFees] + exchange[sfSettlementBalance] - exchange[sfBadDebt])
            fail("perp exchange accounting identity does not hold");

        Number cashBefore = exchange[sfCashTotal];
        if (sum.change != nullptr)
        {
            cashBefore = sum.change->before ? Number{(*sum.change->before)[sfCashTotal]} : Number{};
        }
        if (exchange[sfCashTotal] - cashBefore != sum.cash)
            fail("perp exchange cash total does not match its margin accounts' cash");

        if (heldBalance(view, exchange[sfAccount], exchange[sfAsset]) != assets)
            fail("perp exchange assets total does not match its pseudo-account balance");
    }

    // 3, 4 and 5 for every market whose entry or positions changed.
    struct MarketSum
    {
        Change const* change = nullptr;
        Number longs;
        Number shorts;
        Number size;
        std::vector<SizeChange> sizes;
    };
    std::map<uint256, MarketSum> markets;
    for (auto const& change : markets_)
        markets[change.after->key()].change = &change;
    for (auto const& change : positions_)
    {
        SizeChange const size{
            .before = change.before ? Number{(*change.before)[sfSize]} : Number{},
            .after = change.deleted ? Number{} : Number{(*change.after)[sfSize]}};
        auto& sum = markets[(*change.after)[sfMarketID]];
        sum.size = sum.size + (size.after - size.before);
        sum.longs = sum.longs + (std::max(size.after, Number{}) - std::max(size.before, Number{}));
        sum.shorts =
            sum.shorts + (std::max(-size.after, Number{}) - std::max(-size.before, Number{}));
        sum.sizes.push_back(size);
    }

    for (auto const& [marketID, sum] : markets)
    {
        if ((sum.change != nullptr) && sum.change->deleted)
            continue;
        auto const after =
            (sum.change != nullptr) ? sum.change->after : view.read(keylet::perpMarket(marketID));
        if (!after)
        {
            fail("perp market is missing");
            continue;
        }
        auto const& market = *after;
        auto const before = (sum.change != nullptr) ? sum.change->before : after;
        Number const longBefore = before ? Number{(*before)[sfOpenInterestLong]} : Number{};
        Number const shortBefore = before ? Number{(*before)[sfOpenInterestShort]} : Number{};

        if (market[sfOpenInterestLong] - longBefore != sum.longs ||
            market[sfOpenInterestShort] - shortBefore != sum.shorts)
            fail("perp market open interest does not match its positions");

        auto const status = market[sfMarketStatus];
        if (status == perp::kMarketLive)
        {
            if (sum.size != Number{})
                fail("perp fills in a live market are not zero-sum");
            if (market[sfOpenInterestLong] != market[sfOpenInterestShort])
                fail("perp live market open interest is unbalanced");
        }
        else if (status == perp::kMarketSettled)
        {
            if (std::ranges::any_of(sum.sizes, [](SizeChange const& size) {
                    return size.after != size.before && size.after != Number{};
                }))
                fail("perp position in a settled market changed other than to zero");

            // The index a market settled at is its settlement price and stays frozen.
            if (before && (*before)[sfMarketStatus] == perp::kMarketSettled &&
                Number{(*before)[sfIndexPrice]} != Number{market[sfIndexPrice]})
                fail("perp settled market index changed");
        }
    }

    return ok;
}

}  // namespace xrpl
