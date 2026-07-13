#pragma once

#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>  // IWYU pragma: keep
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstddef>
#include <cstdint>

namespace xrpl {

constexpr std::uint32_t kMinOracleReserveCount = 1;
constexpr std::uint32_t kMaxOracleReserveCount = 2;
constexpr std::size_t kOracleReserveCountThreshold = 5;

template <typename T>
    requires requires(T const& t) { t.size(); }
inline std::uint32_t
calculateOracleReserve(T const& priceDataSeries)
{
    return priceDataSeries.size() > kOracleReserveCountThreshold ? kMaxOracleReserveCount
                                                                 : kMinOracleReserveCount;
}

inline std::uint32_t
calculateOracleReserve(SLE::const_ref oracleSle)
{
    return calculateOracleReserve(oracleSle->getFieldArray(sfPriceDataSeries));
}

template <typename ViewT>
class OracleEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit OracleEntry(
        AccountID const& account,
        std::uint32_t documentID,
        typename SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::oracle(account, documentID), view, j)
    {
    }

    [[nodiscard]] SField const&
    ownerField() const override
    {
        return sfOwner;
    }

    // An Oracle with more than five price-data pairs occupies two reserve slots.
    [[nodiscard]] std::uint32_t
    reserveCount() const override
    {
        return calculateOracleReserve(this->sle()->getFieldArray(sfPriceDataSeries));
    }
};

}  // namespace xrpl
