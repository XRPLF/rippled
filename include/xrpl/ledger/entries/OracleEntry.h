#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/ledger/helpers/OracleHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>  // IWYU pragma: keep
#include <xrpl/protocol/TER.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class OracleEntry : public SLEBase<ViewT, ltORACLE>
{
public:
    using Base = SLEBase<ViewT, ltORACLE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit OracleEntry(
        AccountID const& account,
        std::uint32_t documentID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::oracle(account, documentID), view, j)
    {
    }

    /**
     * Returns the number of owner reserves this Oracle holds, based on the
     * size of its PriceDataSeries.
     *
     * @throws std::logic_error if exists() is false.
     */
    [[nodiscard]] std::uint32_t
    reserveCount() const
    {
        return calculateOracleReserve((*this)->getFieldArray(sfPriceDataSeries));
    }

    /**
     * Removes this Oracle from its owner's directory, releases its owner
     * reserves, and erases it from the view.
     *
     * @param owner The Oracle's owner.
     * @return tesSUCCESS, or tecINTERNAL / tefBAD_LEDGER on ledger corruption.
     */
    [[nodiscard]] TER
    removeFromLedger(AccountID const& owner)
        requires Base::kIsWritable;
};

using OracleEntryR = OracleEntry<ReadView>;
using OracleEntryW = OracleEntry<ApplyView>;

}  // namespace xrpl
