/** @file
 *  Implements the range-protocol adapters for `ReadView::SlesType` and
 *  `ReadView::TxsType`, and the `makeRulesGivenLedger` factory that
 *  bootstraps a `Rules` object from live ledger state.
 *
 *  The range adapters delegate entirely to virtual methods on the owning
 *  `ReadView`, keeping this file free of storage concerns. The rules factory
 *  uses `DigestAwareReadView::digest` so the resulting `Rules` object can
 *  detect amendment changes between ledger closes without re-parsing the SLE.
 */

#include <xrpl/ledger/ReadView.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/ledger/detail/ReadViewFwdRange.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>

#include <optional>
#include <unordered_set>

namespace xrpl {

ReadView::SlesType::SlesType(ReadView const& view) : ReadViewFwdRange(view)
{
}

auto
ReadView::SlesType::begin() const -> Iterator
{
    return Iterator(view_, view_->slesBegin());
}

auto
ReadView::SlesType::end() const -> Iterator
{
    return Iterator(view_, view_->slesEnd());
}

auto
ReadView::SlesType::upperBound(key_type const& key) const -> Iterator
{
    return Iterator(view_, view_->slesUpperBound(key));
}

ReadView::TxsType::TxsType(ReadView const& view) : ReadViewFwdRange(view)
{
}

/** Returns `true` when the transaction map contains no entries.
 *
 *  Implemented as `begin() == end()` rather than a virtual to avoid adding
 *  another virtual method to `ReadView`. The cost is two virtual calls instead
 *  of one; this is acceptable because `empty()` is rarely called on the hot path.
 */
bool
ReadView::TxsType::empty() const
{
    return begin() == end();
}

auto
ReadView::TxsType::begin() const -> Iterator
{
    return Iterator(view_, view_->txsBegin());
}

auto
ReadView::TxsType::end() const -> Iterator
{
    return Iterator(view_, view_->txsEnd());
}

Rules
makeRulesGivenLedger(DigestAwareReadView const& ledger, Rules const& current)
{
    return makeRulesGivenLedger(ledger, current.presets());
}

Rules
makeRulesGivenLedger(
    DigestAwareReadView const& ledger,
    std::unordered_set<uint256, beast::Uhash<>> const& presets)
{
    Keylet const k = keylet::amendments();
    // The digest is fetched before the SLE for two reasons:
    //   1. `digest()` reads directly from the SHAMap trie node without
    //      deserializing the leaf, making it cheaper than `read()`.
    //   2. The digest is threaded into the `Rules` constructor so that
    //      subsequent calls can compare digests to detect whether amendments
    //      have changed between ledger closes without re-parsing the SLE.
    std::optional const digest = ledger.digest(k.key);
    if (digest)
    {
        auto const sle = ledger.read(k);
        // The inner guard handles the genesis-ledger case: the amendments
        // object (`ltAMENDMENTS`) does not exist on the very first ledger,
        // so digest() can return a value while read() returns nullptr when
        // a concurrent apply creates the key but the SLE is not yet visible.
        // Falling through to Rules(presets) is the correct baseline.
        if (sle)
            return Rules(presets, digest, sle->getFieldV256(sfAmendments));
    }
    return Rules(presets);
}

}  // namespace xrpl
