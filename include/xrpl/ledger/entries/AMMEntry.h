#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

#include <expected>
#include <optional>
#include <tuple>

namespace xrpl {

template <typename ViewT>
class AMMEntry : public SLEBase<ViewT, ltAMM>
{
public:
    using Base = SLEBase<ViewT, ltAMM>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit AMMEntry(
        Asset const& issue1,
        Asset const& issue2,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::amm(issue1, issue2), view, j)
    {
    }

    explicit AMMEntry(
        UInt256 const& ammID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::amm(ammID), view, j)
    {
    }

    // Get the AMM pool and LP token balances. If both optAsset1 and optAsset2
    // are set, they are used as the AMM token pair assets. Otherwise the
    // missing assets are read from this AMM.
    [[nodiscard]] std::expected<std::tuple<STAmount, STAmount, STAmount>, TER>
    holds(
        std::optional<Asset> const& optAsset1,
        std::optional<Asset> const& optAsset2,
        FreezeHandling freezeHandling,
        AuthHandling authHandling) const;

    // Get the LP token balance that lpAccount holds in this AMM.
    [[nodiscard]] STAmount
    lpHolds(AccountID const& lpAccount) const;
};

using AMMEntryR = AMMEntry<ReadView>;
using AMMEntryW = AMMEntry<ApplyView>;

}  // namespace xrpl
