#pragma once

#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/STBase.h>

namespace xrpl {

/** Mixin base that lets a serializable field receive an `Asset` at runtime.
 *
 *  Derived classes inherit from `STTakesAsset` _instead of_ `STBase` when
 *  they store a numeric quantity whose precision depends on the enclosing
 *  ledger entry's asset type (XRP, IOU, or MPT). The asset identity is
 *  already present in the containing ledger object and must not be duplicated
 *  in each field; `STTakesAsset` carries it at runtime without serializing it.
 *
 *  The only current concrete user is `STNumber`, which overrides
 *  `associateAsset()` to round its stored `Number` to the asset's canonical
 *  precision immediately upon association and again during serialization.
 *
 *  @note `asset_` is intentionally `std::optional`: during deserialization from
 *      disk no transactor context is available, so no asset can be supplied.
 *      The value still round-trips correctly because it was already rounded when
 *      originally written.
 *  @see STNumber, associateAsset(STLedgerEntry&, Asset const&)
 */
class STTakesAsset : public STBase
{
protected:
    /** Runtime asset identity used for precision rounding.
     *
     *  Absent (`std::nullopt`) on the deserialization path; set by a call to
     *  `associateAsset()` inside `doApply()` before the SLE is serialized.
     */
    std::optional<Asset> asset_;

public:
    using STBase::STBase;
    using STBase::operator=;

    /** Record @p a as the asset governing this field's precision.
     *
     *  The base implementation stores @p a in `asset_` via `emplace` and
     *  returns. Derived classes override this method to act on the asset
     *  immediately — for example, `STNumber` also rounds its stored value to
     *  the asset's canonical precision.
     *
     *  @param a The asset to associate. Must be the same asset used by all
     *      other `sMD_NeedsAsset`-flagged fields in the enclosing SLE.
     */
    virtual void
    associateAsset(Asset const& a);
};

inline void
STTakesAsset::associateAsset(Asset const& a)
{
    asset_.emplace(a);
}

class STLedgerEntry;

/** Associate an asset with every `sMD_NeedsAsset`-flagged field in @p sle.
 *
 *  Iterates over all fields in @p sle by offset (the only path that yields
 *  mutable `STBase&` references). For each field that is present and carries
 *  `SField::kSMD_NEEDS_ASSET`, calls `associateAsset(asset)` on it, triggering
 *  derived-class rounding logic (e.g., `STNumber` rounds to the asset's
 *  canonical precision). After rounding, any `soeDEFAULT`-style field whose
 *  value has become the default (e.g., rounded down to zero) is removed from
 *  the SLE via `makeFieldAbsent` so that zero defaults are not persisted in the
 *  ledger.
 *
 *  Call this near the end of `doApply()` in any transactor that creates or
 *  modifies an SLE containing `STNumber` fields, after all other mutations to
 *  the SLE are complete. Rounding before computations finish may distort
 *  intermediate values.
 *
 *  @param sle   The ledger entry whose `sMD_NeedsAsset` fields will be updated.
 *  @param asset The asset that governs precision for all such fields in @p sle.
 *  @throws std::bad_cast if any field carrying `kSMD_NEEDS_ASSET` is not
 *      derived from `STTakesAsset` — this indicates a field schema error.
 */
void
associateAsset(STLedgerEntry& sle, Asset const& asset);

}  // namespace xrpl
