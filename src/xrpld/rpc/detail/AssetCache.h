/** @file
 *  Per-ledger, thread-safe trust-line and MPT cache for the `Pathfinder`.
 *
 *  `AssetCache` amortises repeated SLE reads during a single pathfinding
 *  session by loading each account's asset data once from an immutable
 *  `ReadView` and reusing it across all traversal steps.
 */

#pragma once

#include <xrpld/rpc/detail/MPT.h>
#include <xrpld/rpc/detail/TrustLine.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/hardened_hash.h>
#include <xrpl/ledger/Ledger.h>

#include <cstddef>
#include <mutex>
#include <vector>

namespace xrpl {

/** Per-ledger trust-line and MPT cache for the pathfinding engine.
 *
 *  The cache is bound to a single immutable `ReadView` snapshot, so all
 *  queries are answered against one consistent ledger version. It is shared
 *  across pathfinder invocations within a single client request via
 *  `std::shared_ptr<AssetCache>`, then discarded when the request ends.
 *
 *  Both `getRippleLines()` and `getMPTs()` are thread-safe; each acquires
 *  `lock_` for its entire duration. Null `shared_ptr` sentinels are stored
 *  instead of empty vectors for accounts with no assets — over 90% of
 *  accounts are estimated to fall into this category, making the sentinel
 *  approach a meaningful memory saving at pathfinding scale.
 *
 *  @see Pathfinder
 *  @see PathRequest
 */
class AssetCache final : public CountedObject<AssetCache>
{
public:
    /** Construct the cache bound to a specific ledger snapshot.
     *
     *  Logs a debug message with the ledger sequence so that cache
     *  lifetimes can be correlated with path-search activity.
     *
     *  @param l The immutable ledger view all queries are answered against.
     *      Kept alive for the lifetime of the cache.
     *  @param j Journal used for diagnostic logging.
     */
    explicit AssetCache(std::shared_ptr<ReadView const> const& l, beast::Journal j);

    /** Destroy the cache, logging the final account and trust-line counts. */
    ~AssetCache();

    /** Return the immutable ledger view this cache is bound to. */
    [[nodiscard]] std::shared_ptr<ReadView const> const&
    getLedger() const
    {
        return ledger_;
    }

    /** Return the cached trust lines for an account, applying the
     *  direction-superset optimisation.
     *
     *  The outgoing set (all trust lines) is always a superset of the
     *  incoming set (only rippling-enabled lines). To avoid storing the
     *  same data twice the cache converges to at most one entry per account:
     *
     *  - If an **outgoing** set is requested and an **incoming** subset is
     *    already cached, the subset is erased and the full outgoing set is
     *    fetched and stored in its place.
     *  - If an **incoming** set is requested and an **outgoing** superset is
     *    already cached, the superset is returned directly. The pathfinder
     *    silently ignores any non-rippling lines it contains.
     *  - On the first request for either direction a fresh set is loaded
     *    from the ledger via `PathFindTrustLine::getItems()`.
     *
     *  @param accountID The account whose trust lines are requested.
     *  @param direction `Outgoing` to obtain all trust lines; `Incoming` to
     *      obtain only rippling-enabled lines (though the full superset may be
     *      returned if it was already cached for that account).
     *  @return A shared pointer to the trust-line vector, or `nullptr` if the
     *      account has no usable trust lines. The vector is never mutated after
     *      insertion so the pointer may safely be shared across threads.
     */
    std::shared_ptr<std::vector<PathFindTrustLine>>
    getRippleLines(AccountID const& accountID, LineDirection direction);

    /** Return the cached MPT holdings and issuances for an account,
     *  populating on first access.
     *
     *  On the first call for a given account the function walks the account's
     *  owner directory and collects `ltMPTOKEN_ISSUANCE` entries (tokens the
     *  account has issued) and `ltMPTOKEN` entries (tokens the account holds).
     *  Each `PathFindMPT` records whether the holder's balance is zero and
     *  whether the issuance has reached its maximum outstanding amount — flags
     *  the pathfinder uses to prune unusable MPT paths without additional
     *  ledger reads. If the issuance SLE for a held token cannot be found,
     *  `maxedOut` defaults conservatively to `true`.
     *
     *  @param account The account whose MPT holdings and issuances are
     *      requested.
     *  @return A `const` reference to the internal `shared_ptr` holding the
     *      MPT vector, or a reference to `nullptr` if the account has no MPTs.
     *      Callers must copy the `shared_ptr` if they need to retain it past
     *      the current call frame, as the reference is into the internal map.
     */
    std::shared_ptr<std::vector<PathFindMPT>> const&
    getMPTs(AccountID const& account);

private:
    std::mutex lock_;

    xrpl::HardenedHash<> hasher_;
    std::shared_ptr<ReadView const> ledger_;

    beast::Journal journal_;

    /** Composite map key encoding an account and its path direction.
     *
     *  The hash is computed once from the `AccountID` alone and shared
     *  between the outgoing and incoming keys for the same account, so
     *  the direction-superset check in `getRippleLines()` never needs to
     *  re-hash when looking up the opposite-direction entry.
     */
    struct AccountKey final : public CountedObject<AccountKey>
    {
        AccountID account;
        LineDirection direction;
        std::size_t hash_value;

        AccountKey(AccountID const& account, LineDirection direction, std::size_t hash)
            : account(account), direction(direction), hash_value(hash)
        {
        }

        AccountKey(AccountKey const& other) = default;

        AccountKey&
        operator=(AccountKey const& other) = default;

        bool
        operator==(AccountKey const& lhs) const
        {
            return hash_value == lhs.hash_value && account == lhs.account &&
                direction == lhs.direction;
        }

        [[nodiscard]] std::size_t
        getHash() const
        {
            return hash_value;
        }

        /** Passthrough hasher that returns the pre-computed `hash_value`,
         *  avoiding redundant hashing during map lookups.
         */
        struct Hash
        {
            Hash() = default;

            std::size_t
            operator()(AccountKey const& key) const noexcept
            {
                return key.getHash();
            }
        };
    };

    // Null shared_ptr sentinels are stored for accounts with no trust lines
    // (estimated >90% of accounts), avoiding vector allocations for the
    // common empty case while still allowing safe removal and sharing of
    // entries without invalidating outstanding shared_ptr copies.
    hash_map<AccountKey, std::shared_ptr<std::vector<PathFindTrustLine>>, AccountKey::Hash> lines_;

    /** Running total of trust-line objects across all cached accounts.
     *  Adjusted on every insert or erase so the destructor can log it
     *  without iterating the map.
     */
    std::size_t totalLineCount_ = 0;

    /** MPT holdings/issuances keyed by account; null pointer for empty accounts. */
    hash_map<AccountID, std::shared_ptr<std::vector<PathFindMPT>>> mpts_;
};

}  // namespace xrpl
