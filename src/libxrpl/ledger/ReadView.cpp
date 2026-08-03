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
    std::optional const digest = ledger.digest(k.key);
    if (digest)
    {
        auto const sle = ledger.read(k);
        if (sle)
            return Rules(presets, digest, sle->getFieldV256(sfAmendments));
    }
    return Rules(presets);
}

}  // namespace xrpl
