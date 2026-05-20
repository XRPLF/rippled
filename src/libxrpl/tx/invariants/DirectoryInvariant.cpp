#include <xrpl/tx/invariants/DirectoryInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <memory>

namespace xrpl {

namespace {

[[nodiscard]] bool
isRootBookDirectory(SLE const& dir)
{
    // Child page keys do not encode book quality.
    return dir.isFieldPresent(sfExchangeRate) || dir.isFieldPresent(sfTakerPaysCurrency) ||
        dir.isFieldPresent(sfTakerPaysIssuer) || dir.isFieldPresent(sfTakerPaysMPT) ||
        dir.isFieldPresent(sfTakerGetsCurrency) || dir.isFieldPresent(sfTakerGetsIssuer) ||
        dir.isFieldPresent(sfTakerGetsMPT) || dir.isFieldPresent(sfDomainID);
}

[[nodiscard]] bool
badExchangeRate(SLE const& dir)
{
    return isRootBookDirectory(dir) &&
        (!dir.isFieldPresent(sfExchangeRate) ||
         dir.getFieldU64(sfExchangeRate) != getQuality(dir.key()));
}

}  // namespace

void
ValidBookDirectory::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    // New root directories must have matching exchange-rate metadata. New
    // child directories must point to an existing root.

    // Only validate newly-created directories; LedgerStateFix handles legacy
    // bad exchange-rate metadata.
    if (badBookDirectory_ || before || !after || after->getType() != ltDIR_NODE)
        return;

    auto const rootIndex = after->getFieldH256(sfRootIndex);
    if (after->key() == rootIndex && !badBookDirectory_)
    {
        badBookDirectory_ = badBookDirectory_ || badExchangeRate(*after);
        return;
    }

    rootIndexes_.insert(rootIndex);
}

bool
ValidBookDirectory::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (!view.rules().enabled(fixCleanup3_2_0))
        return true;

    if (badBookDirectory_)
    {
        JLOG(j.fatal()) << "Invariant failed: book directory exchange rate "
                           "does not match directory quality";
        return false;
    }

    for (auto const& rootIndex : rootIndexes_)
    {
        auto const root = view.read(Keylet(ltDIR_NODE, rootIndex));
        if (!root)
        {
            JLOG(j.fatal()) << "Invariant failed: book directory root missing";
            return false;
        }
    }

    return true;
}

}  // namespace xrpl
