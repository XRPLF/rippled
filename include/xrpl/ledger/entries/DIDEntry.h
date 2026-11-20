#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

template <typename ViewT>
class DIDEntry : public SLEBase<ViewT, ltDID>
{
public:
    using Base = SLEBase<ViewT, ltDID>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit DIDEntry(
        AccountID const& account,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::did(account), view, j)
    {
    }

    /**
     * Inserts a new DID into the ledger, links it into the owner's directory
     * and increments the owner count.
     *
     * Call newSLE() and set the DID fields first.
     *
     * @param owner The account that owns the DID
     * @return tesSUCCESS, or tecINSUFFICIENT_RESERVE / tecDIR_FULL on failure
     */
    [[nodiscard]] TER
    addToLedger(AccountID const& owner)
        requires Base::kIsWritable;

    /**
     * Removes the DID from the owner's directory, decrements the owner count
     * and erases the DID from the ledger.
     *
     * @param owner The account that owns the DID
     * @return tesSUCCESS, or tefBAD_LEDGER / tecINTERNAL on failure
     */
    [[nodiscard]] TER
    removeFromLedger(AccountID const& owner)
        requires Base::kIsWritable;
};

using DIDEntryR = DIDEntry<ReadView>;
using DIDEntryW = DIDEntry<ApplyView>;

}  // namespace xrpl
