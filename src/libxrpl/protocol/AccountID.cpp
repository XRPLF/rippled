/** @file
 *  Implements AccountID derivation, Base58Check encoding/decoding, sentinel
 *  constants, and the optional direct-mapped cache that amortises repeated
 *  Base58Check encoding in hot ledger-processing paths.
 */

#include <xrpl/protocol/AccountID.h>

#include <xrpl/basics/hardened_hash.h>
#include <xrpl/basics/spinlock.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/tokens.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace xrpl {

namespace detail {

/** Direct-mapped cache of Base58Check-encoded AccountID strings.
 *
 *  Converting an AccountID to a base58 string requires a SHA-256 checksum
 *  computation.  Because the same account IDs appear repeatedly during ledger
 *  processing, this cache amortises that cost.
 *
 *  The cache is a flat `std::vector<CachedAccountID>` with no heap allocation
 *  per slot — the encoded string is stored inline in a fixed 40-byte buffer.
 *  Slot selection uses a DoS-resistant `HardenedHash<>` (xxHash + random seed)
 *  so adversarial inputs cannot force systematic collisions.
 *
 *  Concurrency is handled by lock sharding: a single `atomic<uint64_t>`
 *  encodes 64 independent spinlocks via `PackedSpinlock`, one per
 *  `slot % 64`.  Up to 64 distinct slots may be written concurrently without
 *  blocking each other, while the entire lock state fits in one cache line.
 *
 *  @note This class lives in `namespace detail`; callers should use the
 *      `toBase58(AccountID const&)` free function and `initAccountIdCache()`.
 */
class AccountIdCache
{
private:
    /** One cache slot: the account whose encoding is stored, plus the
     *  inline 40-byte buffer that holds the NUL-terminated Base58Check string.
     *
     *  XRPL account addresses are at most 34 characters; 40 bytes gives a
     *  comfortable margin (asserted at ≤38 in `toBase58`).  All bytes are
     *  zero-initialised so that an empty slot is distinguishable from a
     *  legitimate entry for the all-zero account (`xrpAccount()`).
     */
    struct CachedAccountID
    {
        AccountID id;
        char encoding[40] = {0};
    };

    /** Flat slot array; size fixed at construction and never reallocated. */
    std::vector<CachedAccountID> cache_;

    /** DoS-resistant hash used to map an AccountID to a slot index.
     *
     *  Uses xxHash seeded with a random value at startup.  A predictable hash
     *  would be a denial-of-service vector: an attacker could craft account IDs
     *  that all collide, degrading the cache to O(n) effective slots.
     */
    HardenedHash<> hasher_;

    /** 64 spinlocks packed into a single 64-bit atomic word.
     *
     *  Each spinlock guards one lock shard (`slot % 64`).  Using
     *  `PackedSpinlock` keeps the entire lock state on a single cache line and
     *  avoids the overhead of 64 separate `std::mutex` objects.
     */
    std::atomic<std::uint64_t> locks_ = 0;

public:
    /** Construct the cache with exactly `count` slots.
     *
     *  `shrink_to_fit` is called after construction to release any excess
     *  memory that the vector implementation may have reserved.
     *
     *  @param count Number of cache slots to allocate.
     */
    AccountIdCache(std::size_t count) : cache_(count)
    {
        cache_.shrink_to_fit();
    }

    /** Return the Base58Check encoding of `id`, using the cache when possible.
     *
     *  Acquires the shard spinlock for the target slot before reading or
     *  writing.  The hit check `encoding[0] != 0 && id == id` guards against
     *  treating an uninitialised (all-zero) slot as a valid hit for
     *  `xrpAccount()`, whose ID is also all zeros.
     *
     *  On a cache miss the encoding is computed outside the lock to minimise
     *  contention, then written back under the lock.
     *
     *  @param id The account identifier to encode.
     *  @return The Base58Check-encoded account string.
     */
    std::string
    toBase58(AccountID const& id)
    {
        auto const index = hasher_(id) % cache_.size();

        PackedSpinlock sl(locks_, index % 64);

        {
            std::scoped_lock const lock(sl);

            // encoding[0] != 0 distinguishes a populated slot from an
            // uninitialised one whose id bytes happen to be all zero
            // (i.e. xrpAccount()), preventing a false cache hit.
            if (cache_[index].encoding[0] != 0 && cache_[index].id == id)
                return cache_[index].encoding;
        }

        auto ret = encodeBase58Token(TokenType::AccountID, id.data(), id.size());

        XRPL_ASSERT(ret.size() <= 38, "xrpl::detail::AccountIdCache : maximum result size");

        {
            std::scoped_lock const lock(sl);
            cache_[index].id = id;
            std::strcpy(cache_[index].encoding, ret.c_str());
        }

        return ret;
    }
};

}  // namespace detail

/** Module-level singleton for the optional AccountID→base58 cache.
 *
 *  Null until `initAccountIdCache()` is called with a non-zero count.
 *  The `toBase58()` free function falls through to the uncached path when
 *  this pointer is null, so the cache is entirely optional.
 */
static std::unique_ptr<detail::AccountIdCache> gAccountIdCache;

void
initAccountIdCache(std::size_t count)
{
    if (!gAccountIdCache && count != 0)
        gAccountIdCache = std::make_unique<detail::AccountIdCache>(count);
}

std::string
toBase58(AccountID const& v)
{
    if (gAccountIdCache)
        return gAccountIdCache->toBase58(v);

    return encodeBase58Token(TokenType::AccountID, v.data(), v.size());
}

template <>
std::optional<AccountID>
parseBase58(std::string const& s)
{
    auto const result = decodeBase58Token(s, TokenType::AccountID);
    if (result.size() != AccountID::kBYTES)
        return std::nullopt;
    return AccountID{result};
}

/** Derive a 160-bit AccountID from a public key using SHA-256 + RIPEMD-160.
 *
 *  Feeds the raw public-key bytes through `RipeshaHasher`, which internally
 *  runs SHA-256 followed by RIPEMD-160 — the same two-step derivation used
 *  by Bitcoin.  The design rationale (David Schwartz):
 *
 *  - A single SHA-256 hash is vulnerable to length-extension attacks; the
 *    double-hash eliminates that risk.
 *  - RIPEMD-160 is considered secure at 160 bits, whereas simply truncating
 *    SHA-256 or SHA-512 output is a less well-analysed approach.
 *  - XRPL adopted Bitcoin's scheme to avoid any claim of weaker security:
 *    "where there was no good reason to change something, it was not changed."
 *
 *  The `static_assert` below confirms that `AccountID::kBYTES` equals
 *  `sizeof(RipeshaHasher::result_type)`, making the cast safe by construction.
 *
 *  @param pk The public key whose address is to be derived.
 *  @return The 20-byte AccountID.
 */
AccountID
calcAccountID(PublicKey const& pk)
{
    static_assert(AccountID::kBYTES == sizeof(RipeshaHasher::result_type));

    RipeshaHasher rsh;
    rsh(pk.data(), pk.size());
    return AccountID{static_cast<RipeshaHasher::result_type>(rsh)};
}

AccountID const&
xrpAccount()
{
    static AccountID const kACCOUNT(beast::kZERO);
    return kACCOUNT;
}

AccountID const&
noAccount()
{
    static AccountID const kACCOUNT(1);
    return kACCOUNT;
}

bool
toIssuer(AccountID& issuer, std::string const& s)
{
    if (issuer.parseHex(s))
        return true;
    auto const account = parseBase58<AccountID>(s);
    if (!account)
        return false;
    issuer = *account;
    return true;
}

}  // namespace xrpl
