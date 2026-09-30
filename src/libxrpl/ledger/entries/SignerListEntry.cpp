#include <xrpl/ledger/entries/SignerListEntry.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstddef>
#include <cstdint>

namespace xrpl {

namespace {

// We're prepared for there to be multiple signer lists in the future,
// but we don't need them yet.  So for the time being we're manually
// setting the sfSignerListID to zero in all cases.
constexpr std::uint32_t kDefaultSignerListId = 0;

std::uint32_t
signerCountBasedOwnerCountDelta(std::size_t entryCount, Rules const& rules)
{
    // We always compute the full change in OwnerCount, taking into account:
    //  o The fact that we're adding/removing a SignerList and
    //  o Accounting for the number of entries in the list.
    // We can get away with that because lists are not adjusted incrementally;
    // we add or remove an entire list.
    //
    // The rule is:
    //  o Simply having a SignerList costs 2 OwnerCount units.
    //  o And each signer in the list costs 1 more OwnerCount unit.
    // So, at a minimum, adding a SignerList with 1 entry costs 3 OwnerCount
    // units.  A SignerList with 8 entries would cost 10 OwnerCount units.
    //
    // The static_cast should always be safe since entryCount should always
    // be in the range from 1 to 32, so the result is always positive.
    // We've got a lot of room to grow.
    XRPL_ASSERT(
        entryCount >= STTx::kMinMultiSigners,
        "xrpl::signerCountBasedOwnerCountDelta : minimum signers");
    XRPL_ASSERT(
        entryCount <= STTx::kMaxMultiSigners,
        "xrpl::signerCountBasedOwnerCountDelta : maximum signers");
    return 2 + static_cast<int>(entryCount);
}

}  // namespace

template <typename ViewT>
void
SignerListEntry<ViewT>::setSigners(
    AccountID const& owner,
    std::uint32_t quorum,
    STArray const& signerEntries,
    std::uint32_t flags)
    requires Base::kIsWritable
{
    auto& ledgerEntry = **this;

    // Assign the quorum, default SignerListID, and flags.
    if (this->applyView().rules().enabled(fixIncludeKeyletFields))
    {
        ledgerEntry.setAccountID(sfOwner, owner);
    }
    ledgerEntry.setFieldU32(sfSignerQuorum, quorum);
    ledgerEntry.setFieldU32(sfSignerListID, kDefaultSignerListId);
    if (flags != 0u)  // Only set flags if they are non-default (default is zero).
        ledgerEntry.setFieldU32(sfFlags, flags);

    // Assign the SignerEntries.
    ledgerEntry.setFieldArray(sfSignerEntries, signerEntries);
}

template <typename ViewT>
TER
SignerListEntry<ViewT>::removeFromLedger(AccountID const& owner)
    requires Base::kIsWritable
{
    // If the signer list doesn't exist we've already succeeded in deleting it.
    if (!this->exists())
        return tesSUCCESS;

    auto& view = this->applyView();
    auto const j = this->journal();

    // There are two different ways that the OwnerCount could be managed.
    // If the lsfOneOwnerCount bit is set then remove just one owner count.
    // Otherwise use the pre-MultiSignReserve amendment calculation.
    std::uint32_t removeFromOwnerCount = 1;
    if (!(**this).isFlag(lsfOneOwnerCount))
    {
        STArray const& actualList = (**this).getFieldArray(sfSignerEntries);
        removeFromOwnerCount = signerCountBasedOwnerCountDelta(actualList.size(), view.rules());
    }

    // Remove the node from the account directory.
    auto const hint = (**this)[sfOwnerNode];

    if (!view.dirRemove(keylet::ownerDir(owner), hint, this->key(), false))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete SignerList from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    decreaseOwnerCountForObject(
        view, view.peek(keylet::account(owner)), this->mutableRawSle(), removeFromOwnerCount, j);

    // Remove object from ledger
    this->erase();

    return tesSUCCESS;
}

template class SignerListEntry<ReadView>;
template class SignerListEntry<ApplyView>;

}  // namespace xrpl
