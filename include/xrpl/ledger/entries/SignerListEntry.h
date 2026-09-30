#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class SignerListEntry : public SLEBase<ViewT, ltSIGNER_LIST>
{
public:
    using Base = SLEBase<ViewT, ltSIGNER_LIST>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit SignerListEntry(
        AccountID const& account,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::signerList(account), view, j)
    {
    }

    /**
     * Writes the quorum, signer entries, and flags into this SignerList SLE.
     *
     * The entry must already exist (call newSLE() first when creating a new
     * list). signerEntries is taken pre-built, rather than as the
     * tx-layer SignerEntries::SignerEntry vector it is usually sourced from,
     * because the ledger module cannot depend on the tx module (see
     * levelization in ordering.txt); the caller builds it with
     * SignerEntries.
     *
     * @param owner The account that owns this signer list. Written to
     *        sfOwner when the fixIncludeKeyletFields amendment is enabled.
     * @param quorum The signer quorum to write to sfSignerQuorum.
     * @param signerEntries The pre-built array of sfSignerEntry objects to
     *        write to sfSignerEntries.
     * @param flags The flags to write to sfFlags (0 means "no flags").
     */
    void
    setSigners(
        AccountID const& owner,
        std::uint32_t quorum,
        STArray const& signerEntries,
        std::uint32_t flags)
        requires Base::kIsWritable;

    /**
     * Removes the signer list from the owner's directory, decrements the
     * owner count, and erases the signer list from the ledger.
     *
     * A no-op that returns tesSUCCESS if the signer list does not exist
     * (deletion has already succeeded).
     *
     * The owner-count log message is written through this->journal(), so
     * callers that need it tagged under the "View" journal partition (as the
     * free function this replaced was) should construct the entry with that
     * journal, e.g. SignerListEntryW(owner, view, registry.getJournal("View")).
     *
     * @param owner The account that owns the signer list.
     * @return tesSUCCESS, or tefBAD_LEDGER if the owner directory entry
     *         cannot be unlinked.
     */
    [[nodiscard]] TER
    removeFromLedger(AccountID const& owner)
        requires Base::kIsWritable;
};

using SignerListEntryR = SignerListEntry<ReadView>;
using SignerListEntryW = SignerListEntry<ApplyView>;

}  // namespace xrpl
