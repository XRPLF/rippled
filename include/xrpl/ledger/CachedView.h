/** @file
 *  Transparent two-level caching layer over a `DigestAwareReadView`.
 *
 *  Declares `detail::CachedViewImpl` (non-template caching logic) and the
 *  public template `CachedView<Base>`, which adds `shared_ptr` ownership of
 *  the wrapped view.  The canonical instantiation `CachedLedger` (defined in
 *  `Ledger.h`) wraps the immutable closed ledger that serves as the base for
 *  transaction application.
 *
 *  @see CachedSLEs
 *  @see CachedLedger
 */

#pragma once

#include <xrpl/basics/hardened_hash.h>
#include <xrpl/ledger/CachedSLEs.h>
#include <xrpl/ledger/ReadView.h>

#include <mutex>
#include <type_traits>

namespace xrpl {

namespace detail {

/** Non-template base class that implements SLE caching over a `DigestAwareReadView`.
 *
 *  All caching logic is compiled once here, avoiding template-instantiation bloat
 *  in `CachedView<Base>`.  The class maintains two complementary caches:
 *
 *  - **`map_`** — a per-instance `unordered_map` from ledger key (`uint256`) to
 *    SLE digest.  Once a key has been resolved to its content hash, subsequent
 *    reads skip the SHAMap traversal.  Uses `HardenedHash<>` to resist
 *    hash-flood attacks from adversarially crafted ledger keys.
 *  - **`cache_`** — a reference to an externally owned, process-wide `CachedSLEs`
 *    (`TaggedCache<uint256, SLE const>`) keyed by digest.  Multiple views over
 *    different ledgers share this cache; if two ledgers carry an unchanged SLE,
 *    only one deserialized copy lives in memory.
 *
 *  `mutex_` guards `map_` only; it is deliberately *not* held across
 *  `base_.digest()` or `base_.read()` calls so that concurrent readers are not
 *  serialized through SHAMap traversal or deserialization.  Two threads may both
 *  call `base_.digest()` for the same key on a cold miss — this is safe because
 *  `base_` is an immutable ledger snapshot.
 *
 *  Copy and assignment are deleted; a cached view always represents a unique,
 *  coherent window onto a specific ledger snapshot.
 *
 *  @note All `ReadView` and `DigestAwareReadView` pass-through methods delegate
 *      directly to `base_`; only `exists()` and `read()` go through the cache.
 */
class CachedViewImpl : public DigestAwareReadView
{
private:
    DigestAwareReadView const& base_;
    CachedSLEs& cache_;
    std::mutex mutable mutex_;
    /** Per-instance map from ledger key to SLE digest.
     *
     *  Uses `HardenedHash<>` to prevent adversarial hash-bucket flooding from
     *  network-visible ledger keys (account IDs, object types).
     */
    std::unordered_map<key_type, uint256, HardenedHash<>> mutable map_;

public:
    CachedViewImpl() = delete;
    CachedViewImpl(CachedViewImpl const&) = delete;
    CachedViewImpl&
    operator=(CachedViewImpl const&) = delete;

    /** Construct over an existing `DigestAwareReadView` and a shared SLE cache.
     *
     *  @param base   The underlying immutable view to cache reads against.
     *      The caller is responsible for ensuring `base` outlives this object;
     *      `CachedView<Base>` satisfies this by holding the owning `shared_ptr`.
     *  @param cache  The process-wide SLE cache shared across all views.
     */
    CachedViewImpl(DigestAwareReadView const* base, CachedSLEs& cache) : base_(*base), cache_(cache)
    {
    }

    //
    // ReadView
    //

    /** Returns `true` if an SLE exists for the given keylet.
     *
     *  Delegates to `read(k) != nullptr`; benefits from caching on repeated
     *  calls for the same key.
     */
    bool
    exists(Keylet const& k) const override;

    /** Return the SLE associated with the keylet, going through both cache levels.
     *
     *  The lookup sequence is:
     *  1. Check `map_` for a known digest (under `mutex_`).
     *  2. If absent, call `base_.digest(k.key)` outside the lock.
     *  3. Pass the digest to `cache_.fetch()`, which deserializes from `base_`
     *     only on a shared-cache miss.
     *  4. Populate `map_` on a cold miss (re-acquires `mutex_`).
     *  5. Validate the SLE type with `k.check(*sle)`.
     *
     *  Hit/miss statistics are tracked via `CountedObjects` counters
     *  `CachedView::hit`, `CachedView::hitExpired`, and `CachedView::miss`.
     *
     *  @return The matching `SLE const`, or `nullptr` if the key is absent or
     *      the stored type does not match the keylet's expected type.
     */
    std::shared_ptr<SLE const>
    read(Keylet const& k) const override;

    bool
    open() const override
    {
        return base_.open();
    }

    LedgerHeader const&
    header() const override
    {
        return base_.header();
    }

    Fees const&
    fees() const override
    {
        return base_.fees();
    }

    Rules const&
    rules() const override
    {
        return base_.rules();
    }

    std::optional<key_type>
    succ(key_type const& key, std::optional<key_type> const& last = std::nullopt) const override
    {
        return base_.succ(key, last);
    }

    std::unique_ptr<SlesType::iter_base>
    slesBegin() const override
    {
        return base_.slesBegin();
    }

    std::unique_ptr<SlesType::iter_base>
    slesEnd() const override
    {
        return base_.slesEnd();
    }

    std::unique_ptr<SlesType::iter_base>
    slesUpperBound(uint256 const& key) const override
    {
        return base_.slesUpperBound(key);
    }

    std::unique_ptr<TxsType::iter_base>
    txsBegin() const override
    {
        return base_.txsBegin();
    }

    std::unique_ptr<TxsType::iter_base>
    txsEnd() const override
    {
        return base_.txsEnd();
    }

    bool
    txExists(key_type const& key) const override
    {
        return base_.txExists(key);
    }

    tx_type
    txRead(key_type const& key) const override
    {
        return base_.txRead(key);
    }

    //
    // DigestAwareReadView
    //

    std::optional<digest_type>
    digest(key_type const& key) const override
    {
        return base_.digest(key);
    }
};

}  // namespace detail

/** Transparent caching layer over a `DigestAwareReadView`.
 *
 *  Wraps a `shared_ptr<Base const>` to ensure the underlying view remains alive
 *  for the lifetime of this object, then delegates all caching logic to
 *  `detail::CachedViewImpl`.  The `static_assert` enforces that `Base` satisfies
 *  the `DigestAwareReadView` contract required for two-level caching.
 *
 *  The production instantiation is `CachedLedger = CachedView<Ledger>`, used
 *  by `OpenLedger::create()` to wrap the closed ledger that forms the base for
 *  each round of transaction application.
 *
 *  Copy and assignment are deleted; each `CachedView` instance is the sole
 *  owner of its per-instance key→digest `map_`.
 *
 *  @tparam Base A type derived from `DigestAwareReadView`.
 *
 *  @see detail::CachedViewImpl
 *  @see CachedSLEs
 */
template <class Base>
class CachedView : public detail::CachedViewImpl
{
private:
    static_assert(std::is_base_of_v<DigestAwareReadView, Base>, "");

    std::shared_ptr<Base const> sp_;

public:
    using base_type = Base;

    CachedView() = delete;
    CachedView(CachedView const&) = delete;
    CachedView&
    operator=(CachedView const&) = delete;

    /** Construct a caching view over a shared immutable ledger snapshot.
     *
     *  @param base   Shared ownership of the underlying view; must not be null.
     *  @param cache  Process-wide SLE cache shared across all `CachedView`
     *      instances.  Must outlive this object.
     */
    CachedView(std::shared_ptr<Base const> const& base, CachedSLEs& cache)
        : CachedViewImpl(base.get(), cache), sp_(base)
    {
    }

    /** Return the underlying view, bypassing both cache levels.
     *
     *  @note This breaks encapsulation: callers interact with the
     *      `DigestAwareReadView` directly, skipping both the per-instance
     *      key→digest `map_` and the shared `CachedSLEs`.  Use only when the
     *      full `Base` type (e.g. `Ledger`) is needed and cannot be expressed
     *      through the `ReadView` interface alone.
     *
     *  @return A const shared pointer to the wrapped `Base` instance.
     */
    std::shared_ptr<Base const> const&
    base() const
    {
        return sp_;
    }
};

}  // namespace xrpl
