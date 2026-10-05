#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STVector256.h>

#include <cstddef>
#include <optional>

namespace xrpl {

template <typename ViewT>
class LedgerHashesEntry : public SLEBase<ViewT, ltLEDGER_HASHES>
{
public:
    using Base = SLEBase<ViewT, ltLEDGER_HASHES>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit LedgerHashesEntry(
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::skip(), view, j)
    {
    }

    /**
     * Looks up a hash `diff` slots back from the most recent entry in the
     * sfHashes vector (diff == 0 is the most recent entry).
     *
     * Callers decide which skip list entry to read and how to translate a
     * target ledger sequence into `diff`; this only does the bounds check
     * and vector indexing shared by both the recent (stride 1) and distant
     * (stride 256) skip lists.
     *
     * @param diff how many slots back from the most recent hash to look up;
     * 0 is the most recent hash.
     * @return the hash at that slot, or std::nullopt if the entry does not
     * exist or `diff` is out of range.
     */
    [[nodiscard]] std::optional<uint256>
    hashAt(std::size_t diff) const
    {
        if (!this->exists())
            return std::nullopt;
        STVector256 const vec = (*this)->getFieldV256(sfHashes);
        if (vec.size() > diff)
            return vec[vec.size() - diff - 1];
        return std::nullopt;
    }
};

using LedgerHashesEntryR = LedgerHashesEntry<ReadView>;
using LedgerHashesEntryW = LedgerHashesEntry<ApplyView>;

}  // namespace xrpl
