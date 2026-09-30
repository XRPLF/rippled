#include <xrpl/ledger/entries/AMMEntry.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <tuple>
#include <utility>

namespace xrpl {

template <typename ViewT>
std::expected<std::tuple<STAmount, STAmount, STAmount>, TER>
AMMEntry<ViewT>::holds(
    std::optional<Asset> const& optAsset1,
    std::optional<Asset> const& optAsset2,
    FreezeHandling freezeHandling,
    AuthHandling authHandling) const
{
    auto const j = this->journal();
    auto const assets = [&]() -> std::optional<std::pair<Asset, Asset>> {
        auto const asset1 = Base::operator*()[sfAsset];
        auto const asset2 = Base::operator*()[sfAsset2];
        if (optAsset1 && optAsset2)
        {
            if (invalidAMMAssetPair(
                    *optAsset1, *optAsset2, std::make_optional(std::make_pair(asset1, asset2))))
            {
                // This error can only be hit if the AMM is corrupted
                // LCOV_EXCL_START
                JLOG(j.debug()) << "ammHolds: Invalid optAsset1 or optAsset2 " << *optAsset1 << " "
                                << *optAsset2;
                return std::nullopt;
                // LCOV_EXCL_STOP
            }
            return std::make_optional(std::make_pair(*optAsset1, *optAsset2));
        }
        auto const singleAsset = [&asset1, &asset2, &j](
                                     Asset checkIssue,
                                     char const* label) -> std::optional<std::pair<Asset, Asset>> {
            if (checkIssue == asset1)
            {
                return std::make_optional(std::make_pair(asset1, asset2));
            }
            if (checkIssue == asset2)
            {
                return std::make_optional(std::make_pair(asset2, asset1));
            }
            // Unreachable unless AMM corrupted.
            // LCOV_EXCL_START
            JLOG(j.debug()) << "ammHolds: Invalid " << label << " " << checkIssue;
            return std::nullopt;
            // LCOV_EXCL_STOP
        };
        if (optAsset1)
        {
            return singleAsset(*optAsset1, "optAsset1");
        }
        if (optAsset2)
        {
            // Cannot have Amount2 without Amount.
            return singleAsset(*optAsset2, "optAsset2");  // LCOV_EXCL_LINE
        }
        return std::make_optional(std::make_pair(asset1, asset2));
    }();
    if (!assets)
        return std::unexpected(tecAMM_INVALID_TOKENS);
    auto const [amount1, amount2] = ammPoolHolds(
        this->readView(),
        Base::operator->()->getAccountID(sfAccount),
        assets->first,
        assets->second,
        freezeHandling,
        authHandling,
        j);
    return std::make_tuple(amount1, amount2, Base::operator*()[sfLPTokenBalance]);
}

template <typename ViewT>
STAmount
AMMEntry<ViewT>::lpHolds(AccountID const& lpAccount) const
{
    return ammLPHolds(
        this->readView(),
        Base::operator*()[sfAsset],
        Base::operator*()[sfAsset2],
        Base::operator*()[sfAccount],
        lpAccount,
        this->journal());
}

template <typename ViewT>
std::uint16_t
AMMEntry<ViewT>::tradingFee(AccountID const& account) const
{
    using namespace std::chrono;
    XRPL_ASSERT(
        Base::operator->()->isFieldPresent(sfAuctionSlot),
        "xrpl::AMMEntry::tradingFee : auction present");
    if (Base::operator->()->isFieldPresent(sfAuctionSlot))
    {
        auto const& auctionSlot =
            safeDowncast<STObject const&>(Base::operator->()->peekAtField(sfAuctionSlot));
        // Not expired
        if (auto const expiration = auctionSlot[~sfExpiration];
            duration_cast<seconds>(this->readView().header().parentCloseTime.time_since_epoch())
                .count() < expiration)
        {
            if (auctionSlot[~sfAccount] == account)
                return auctionSlot[sfDiscountedFee];
            if (auctionSlot.isFieldPresent(sfAuthAccounts))
            {
                for (auto const& acct : auctionSlot.getFieldArray(sfAuthAccounts))
                {
                    if (acct[~sfAccount] == account)
                        return auctionSlot[sfDiscountedFee];
                }
            }
        }
    }
    return Base::operator*()[sfTradingFee];
}

template class AMMEntry<ReadView>;
template class AMMEntry<ApplyView>;

}  // namespace xrpl
