#include <xrpl/ledger/helpers/SignerListHelpers.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>

#include <cstddef>
#include <cstdint>

namespace xrpl {

std::int32_t
signerListOwnerCount(SLE const& sle)
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
    // be in the range from 1 to 32.
    // We've got a lot of room to grow.

    XRPL_ASSERT(sle.getType() == ltSIGNER_LIST, "xrpl::signerListOwnerCount : ltSIGNER_LIST type");

    std::int32_t ownerCount = 1;

    if (!sle.isFlag(lsfOneOwnerCount))
    {
        STArray const& actualList = sle.getFieldArray(sfSignerEntries);
        std::size_t const entryCount = actualList.size();
        XRPL_ASSERT(
            entryCount >= STTx::kMinMultiSigners,
            "xrpl::signerCountBasedOwnerCountDelta : minimum signers");
        XRPL_ASSERT(
            entryCount <= STTx::kMaxMultiSigners,
            "xrpl::signerCountBasedOwnerCountDelta : maximum signers");

        ownerCount = 2 + static_cast<std::int32_t>(entryCount);
    }

    return ownerCount;
}

}  // namespace xrpl
