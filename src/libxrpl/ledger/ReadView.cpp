#include <xrpl/ledger/ReadView.h>

namespace ripple {

ReadView::sles_type::sles_type(ReadView const& view) : ReadViewFwdRange(view)
{
}

auto
ReadView::sles_type::begin() const -> iterator
{
    return iterator(view_, view_->slesBegin());
}

auto
ReadView::sles_type::end() const -> iterator
{
    return iterator(view_, view_->slesEnd());
}

auto
ReadView::sles_type::upper_bound(key_type const& key) const -> iterator
{
    return iterator(view_, view_->slesUpperBound(key));
}

ReadView::txs_type::txs_type(ReadView const& view) : ReadViewFwdRange(view)
{
}

bool
ReadView::txs_type::empty() const
{
    return begin() == end();
}

auto
ReadView::txs_type::begin() const -> iterator
{
    return iterator(view_, view_->txsBegin());
}

auto
ReadView::txs_type::end() const -> iterator
{
    return iterator(view_, view_->txsEnd());
}

Rules
makeRulesGivenLedger(DigestAwareReadView const& ledger, Rules const& current)
{
    return makeRulesGivenLedger(ledger, current.presets());
}

Rules
makeRulesGivenLedger(
    DigestAwareReadView const& ledger,
    std::unordered_set<uint256, beast::uhash<>> const& presets)
{
    Keylet const k = keylet::amendments();
    std::optional digest = ledger.digest(k.key);
    if (digest)
    {
        auto const sle = ledger.read(k);
        if (sle)
            return Rules(presets, digest, sle->getFieldV256(sfAmendments));
    }
    return Rules(presets);
}

}  // namespace ripple
