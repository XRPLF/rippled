#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl {

/*
    Validator key manifests
    -----------------------

    Suppose the secret keys installed on an XRPL validator are compromised. Not
    only do you have to generate and install new key pairs on each validator,
    EVERY xrpld needs to have its config updated with the new public keys, and
    is vulnerable to forged validation signatures until this is done.  The
    solution is a new layer of indirection: A master secret key under
    restrictive access control is used to sign a "manifest": essentially, a
    certificate including the master public key, an ephemeral public key for
    verifying validations (which will be signed by its secret counterpart), a
    sequence number, and a digital signature.

    The manifest has two serialized forms: one which includes the digital
    signature and one which doesn't.  There is an obvious causal dependency
    relationship between the (latter) form with no signature, the signature
    of that form, and the (former) form which includes that signature.  In
    other words, a message can't contain a signature of itself.  The code
    below stores a serialized manifest which includes the signature, and
    dynamically generates the signatureless form when it needs to verify
    the signature.

    An instance of ManifestCache stores, for each known validator, (a) its
    master public key, and (b) the most senior of all valid manifests it has
    seen for that validator, if any.  On startup, the [validator_token] config
    entry (which contains the manifest for this validator) is decoded and
    added to the manifest cache.  Other manifests are added as "gossip"
    received from xrpld peers, including ones for validators this node does not
    trust. Manifests for untrusted validators are capped (kMaxUntrustedCount)
    so peer gossip cannot grow the cache without bound; trusted validators are
    not capped. Entries are never evicted, so a stored revocation is permanent.

    When an ephemeral key is compromised, a new signing key pair is created,
    along with a new manifest vouching for it (with a higher sequence number),
    signed by the master key.  When an xrpld peer receives the new manifest,
    it verifies it with the master key and (assuming it's valid) discards the
    old ephemeral key and stores the new one.  If the master key itself gets
    compromised, a manifest with sequence number 0xFFFFFFFF will supersede a
    prior manifest and discard any existing ephemeral key without storing a
    new one.  These revocation manifests are loaded from the
    [validator_key_revocation] config entry as well as received as gossip from
    peers.  Since no further manifests for this master key will be accepted
    (since no higher sequence number is possible), and no signing key is on
    record, no validations will be accepted from the compromised validator.
*/

//------------------------------------------------------------------------------

struct Manifest
{
    /**
     * The manifest in serialized form.
     */
    std::string serialized;

    /**
     * The master key associated with this manifest.
     */
    PublicKey masterKey;

    /**
     * The ephemeral key associated with this manifest.
     */
    // A revoked manifest does not have a signingKey
    // This field is specified as "optional" in manifestFormat's
    // SOTemplate
    std::optional<PublicKey> signingKey;

    /**
     * The sequence number of this manifest.
     */
    std::uint32_t sequence = 0;

    /**
     * The domain, if one was specified in the manifest; empty otherwise.
     */
    std::string domain;

    Manifest() = delete;

    Manifest(
        std::string serialized,
        PublicKey const& masterKey,
        std::optional<PublicKey> const& signingKey,
        std::uint32_t seq,
        std::string domain)
        : serialized(std::move(serialized))
        , masterKey(masterKey)
        , signingKey(signingKey)
        , sequence(seq)
        , domain(std::move(domain))
    {
    }

    Manifest(Manifest const& other) = delete;
    Manifest&
    operator=(Manifest const& other) = delete;
    Manifest(Manifest&& other) = default;
    Manifest&
    operator=(Manifest&& other) = default;

    /**
     * Returns `true` if manifest signature is valid
     */
    [[nodiscard]] bool
    verify() const;

    /**
     * Returns hash of serialized manifest data
     */
    [[nodiscard]] UInt256
    hash() const;

    /**
     * Returns `true` if manifest revokes master key
     */
    // The maximum possible sequence number means that the master key has
    // been revoked
    static bool
    revoked(std::uint32_t sequence);

    /**
     * Returns `true` if manifest revokes master key
     */
    [[nodiscard]] bool
    revoked() const;

    /**
     * Returns manifest signature
     */
    [[nodiscard]] std::optional<Blob>
    getSignature() const;

    /**
     * Returns manifest master key signature
     */
    [[nodiscard]] Blob
    getMasterSignature() const;
};

/**
 * Format the specified manifest to a string for debugging purposes.
 */
std::string
to_string(Manifest const& m);

/**
 * Largest a valid manifest can be, in decoded bytes.
 *
 *   A manifest has a fixed set of fields. Each is serialized as a field header
 *   (1-2 bytes), an optional length prefix (1 byte for these sizes), and the
 *   field body. Taking every field at its largest gives the maximum below, so
 *   anything larger cannot be a valid manifest.
 *
 *       Field                 header + length + body = bytes
 *       sfVersion   (U16)        2          0     2     4
 *       sfSequence  (U32)        1          0     4     5
 *       sfPublicKey (33)         1          1    33    35
 *       sfSigningPubKey (33)     1          1    33    35
 *       sfSignature (72)         1          1    72    74
 *       sfMasterSignature (72)   2          1    72    75
 *       sfDomain    (128)        1          1   128   130
 *                                                    -----
 *                                                      358
 */
constexpr std::size_t kMaxManifestBytes = 358;

/**
 * Largest a valid manifest can be, in base64 characters.
 *
 *   base64 encodes 3 bytes as 4 characters, so this is the encoded form of
 *   @ref kMaxManifestBytes. Callers that receive a base64 manifest should
 *   reject anything longer than this before decoding, to avoid allocating
 *   memory for an oversized input.
 */
constexpr std::size_t kMaxManifestBase64 = base64::encodedSize(kMaxManifestBytes);

/**
 * Default number of untrusted manifests to store in cache and allowed
 * in one Manifest message.
 *
 * Bounds unlisted validators two ways. In the cache, a manifest for a
 * brand-new unlisted key is rejected once this many are held, so peer gossip
 * cannot grow the cache without end. In a TMManifests message, this many are
 * sent and processed, so a peer sending its whole cache cannot force unbounded
 * work.
 *
 * Operators can override this with `[overlay] max_untrusted_count`. Both users
 * read the configured value and fall back to this default.
 */
constexpr std::size_t kMaxUntrustedCount = 300;

/**
 * Default number of trusted manifests allowed in a Manifest message.
 * Not used atm while creating the message, but used to calculate the higher limit on
 * received message size. Introduced to maintain consistency. Future implementation
 * will use this limit.
 *
 * Trusted manifests are never dropped: every one this node holds is sent, and
 * every one received is processed, since dropping one would delay a validator
 * key rotation. This count only sizes the largest message accepted, so it must
 * stay above any realistic validator list. Cap can be increased in the config
 * file if messages get rejected with actual trusted manifest count crossing
 * configured(or else default) value.
 * Operators can override this with `[overlay] max_trusted_count`.
 */
constexpr std::size_t kMaxTrustedCount = 300;

/**
 * Number of untrusted manifests to store in cache and allowed
 * in one Manifest message..
 *
 * Returns the operator's override when one is configured, otherwise
 * @ref kMaxUntrustedCount. Config stores an override rather than the default
 * itself because the core module cannot depend on this module.
 *
 * @param configured The value from `[overlay] max_untrusted_count`, or
 *     `std::nullopt` when the operator did not set it.
 */
constexpr std::size_t
untrustedManifestCount(std::optional<std::size_t> const& configured)
{
    return configured.value_or(kMaxUntrustedCount);
}

/**
 * Number of trusted manifests allowed in a Manifest message.
 *
 * Not a cap on how many are sent or processed; see @ref kMaxTrustedCount.
 * but used to calculate the higher limit on received message size.
 *
 * @param configured The value from `[overlay] max_trusted_count`, or
 *     `std::nullopt` when the operator did not set it.
 */
constexpr std::size_t
trustedManifestCount(std::optional<std::size_t> const& configured)
{
    return configured.value_or(kMaxTrustedCount);
}

/**
 * Constructs Manifest from serialized string
 *
 * @param s Serialized manifest string
 *
 * @return `std::nullopt` if string is invalid
 *
 * @note This does not verify manifest signatures.
 * `Manifest::verify` should be called after constructing manifest.
 */
/** @{ */
std::optional<Manifest>
deserializeManifest(Slice s, beast::Journal journal);

inline std::optional<Manifest>
deserializeManifest(
    std::string const& s,
    beast::Journal journal = beast::Journal(beast::Journal::getNullSink()))
{
    return deserializeManifest(makeSlice(s), journal);
}

template <class T>
std::optional<Manifest>
deserializeManifest(
    std::vector<T> const& v,
    beast::Journal journal = beast::Journal(beast::Journal::getNullSink()))
    requires(std::is_same_v<T, char> || std::is_same_v<T, unsigned char>)
{
    return deserializeManifest(makeSlice(v), journal);
}
/** @} */

inline bool
operator==(Manifest const& lhs, Manifest const& rhs)
{
    // In theory, comparing the two serialized strings should be
    // sufficient.
    return lhs.sequence == rhs.sequence && lhs.masterKey == rhs.masterKey &&
        lhs.signingKey == rhs.signingKey && lhs.domain == rhs.domain &&
        lhs.serialized == rhs.serialized;
}

struct ValidatorToken
{
    std::string manifest;
    SecretKey validationSecret;
};

std::optional<ValidatorToken>
loadValidatorToken(
    std::vector<std::string> const& blob,
    beast::Journal journal = beast::Journal(beast::Journal::getNullSink()));

enum class ManifestDisposition {
    Accepted = 0,  ///< Manifest is valid

    Stale,  ///< Sequence is too old

    BadMasterKey,  ///< The master key is not acceptable to us

    BadEphemeralKey,  ///< The ephemeral key is not acceptable to us

    Invalid,  ///< Timely, but invalid signature

    UntrustedCapacity  ///< Unlisted and limit reached
};

inline std::string
to_string(ManifestDisposition m)
{
    switch (m)
    {
        case ManifestDisposition::Accepted:
            return "accepted";
        case ManifestDisposition::Stale:
            return "stale";
        case ManifestDisposition::BadMasterKey:
            return "badMasterKey";
        case ManifestDisposition::BadEphemeralKey:
            return "badEphemeralKey";
        case ManifestDisposition::Invalid:
            return "invalid";
        case ManifestDisposition::UntrustedCapacity:
            return "untrustedCapacity";
        default:
            return "unknown";
    }
}

/**
 * Whether a manifest counts against the 'untrusted' cache cap.
 *
 * Passed to `ManifestCache::applyManifest` with no default, so every caller
 * must choose. `Capped` is the safe, flood-resistant value; only listed or
 * configured keys should use `Uncapped`.
 */
enum class ManifestRateLimitCapPolicy : std::uint8_t {
    Capped,   ///< Subject to the untrusted cap (unlisted peer gossip)
    Uncapped  ///< Bypasses the cap (listed/trusted or config manifests)
};

class DatabaseCon;

/**
 * Remembers manifests with the highest sequence number.
 */
class ManifestCache
{
private:
    beast::Journal j_;
    std::shared_mutex mutable mutex_;

    /**
     * Active manifests stored by master public key.
     */
    HashMap<PublicKey, Manifest> map_;

    /**
     * Master public keys stored by current ephemeral public key.
     */
    HashMap<PublicKey, PublicKey> signingToMasterKeys_;

    std::atomic<std::uint32_t> seq_{0};

    /**
     * Master keys of cached manifests for validators this node does not list.
     *
     * One entry per capped key in `map_`; its size enforces the cap below.
     * A key is added when first cached under `Capped` and removed when it
     * becomes listed (see `promoteToTrusted`) or an `Uncapped` update arrives,
     * never re-added on de-listing. Uncapped keys are not tracked here.
     */
    HashSet<PublicKey> untrustedKeys_;

    /**
     * Maximum number of untrusted master keys kept in the cache.
     *
     * Once reached, a manifest for a brand-new unlisted key is rejected. Set
     * from the config, defaulting to @ref kMaxUntrustedCount.
     */
    std::size_t const maxUntrustedCount_;

    /**
     * Running count of manifests rejected because the untrusted cap was full.
     *
     * Drives throttled logging (see `kUntrustedRejectCount`). Atomic because
     * `applyManifest` may run concurrently.
     */
    std::atomic<std::uint64_t> untrustedRejectCount_{0};

    /**
     * Number of cap rejections between summary warnings.
     *
     * @see untrustedRejectCount_
     */
    static constexpr std::uint64_t kUntrustedRejectCount = 10000;

public:
    /**
     * @param j Journal for logging.
     *
     * @param maxUntrustedCount Untrusted master keys to keep. Pass the
     *     configured value; defaults to @ref kMaxUntrustedCount. Taken as a
     *     parameter because this module cannot depend on the config.
     */
    explicit ManifestCache(
        beast::Journal j = beast::Journal(beast::Journal::getNullSink()),
        std::size_t maxUntrustedCount = kMaxUntrustedCount)
        : j_(j), maxUntrustedCount_(maxUntrustedCount)
    {
    }

    /**
     * A monotonically increasing number used to detect new manifests.
     */
    std::uint32_t
    sequence() const
    {
        return seq_.load();
    }

    /**
     * Returns master key's current signing key.
     *
     * @param pk Master public key
     *
     * @return pk if no known signing key from a manifest
     *
     * @par Thread Safety
     *
     * May be called concurrently
     */
    std::optional<PublicKey>
    getSigningKey(PublicKey const& pk) const;

    /**
     * Returns ephemeral signing key's master public key.
     *
     * @param pk Ephemeral signing public key
     *
     * @return pk if signing key is not in a valid manifest
     *
     * @par Thread Safety
     *
     * May be called concurrently
     */
    PublicKey
    getMasterKey(PublicKey const& pk) const;

    /**
     * Returns master key's current manifest sequence.
     *
     * @return sequence corresponding to Master public key
     *   if configured or std::nullopt otherwise
     */
    std::optional<std::uint32_t>
    getSequence(PublicKey const& pk) const;

    /**
     * Returns domain claimed by a given public key
     *
     * @return domain corresponding to Master public key
     *   if present, otherwise std::nullopt
     */
    std::optional<std::string>
    getDomain(PublicKey const& pk) const;

    /**
     * Returns manifest corresponding to a given public key
     *
     * @return manifest corresponding to Master public key
     *   if present, otherwise std::nullopt
     */
    std::optional<std::string>
    getManifest(PublicKey const& pk) const;

    /**
     * Returns `true` if master key has been revoked in a manifest.
     *
     * @param pk Master public key
     *
     * @par Thread Safety
     *
     * May be called concurrently
     */
    bool
    revoked(PublicKey const& pk) const;

    /**
     * Add manifest to cache.
     *
     * A brand-new unlisted key is rejected once the untrusted cap is full;
     * updates to a cached key and `Uncapped` manifests bypass the cap. The
     * caller decides `cap` before calling so the cache lock is not held while
     * consulting the validator list, which would risk a lock-ordering deadlock.
     *
     * @param m Manifest to add
     *
     * @param cap `Uncapped` skips the untrusted cap; use it for keys that are
     *     listed, configured, or loaded from the DB. Note `Uncapped` does not
     *     assert the key is currently trusted (a DB entry may predate a
     *     de-listing). Callers must state this explicitly so a manifest is
     *     never left uncapped by omission.
     *
     * @return `Accepted` if stored, `Stale` if superseded, `Invalid`/
     *         `BadEphemeralKey` if malformed, or `UntrustedCapacity` if the
     *         untrusted cap is full.
     *
     * @par Thread Safety
     *
     * May be called concurrently
     */
    ManifestDisposition
    applyManifest(Manifest m, ManifestRateLimitCapPolicy cap);

    /**
     * Stop counting a master key against the untrusted cap.
     *
     * Called when a cached untrusted key becomes listed, freeing its slot.
     * Idempotent and a no-op for keys that were never counted.
     *
     * @param pk Master public key that is now listed/trusted
     *
     * @par Thread Safety
     *
     * May be called concurrently
     */
    void
    promoteToTrusted(PublicKey const& pk);

    /**
     * Populate manifest cache with manifests in database and config.
     *
     * @param dbCon Database connection with dbTable
     *
     * @param dbTable Database table
     *
     * @param configManifest Base64 encoded manifest for local node's
     *     validator keys
     *
     * @param configRevocation Base64 encoded validator key revocation
     *     from the config
     *
     * @par Thread Safety
     *
     * May be called concurrently
     */
    bool
    load(
        DatabaseCon& dbCon,
        std::string const& dbTable,
        std::string const& configManifest,
        std::vector<std::string> const& configRevocation);

    /**
     * Populate manifest cache with manifests in database.
     *
     * @param dbCon Database connection with dbTable
     *
     * @param dbTable Database table
     *
     * @par Thread Safety
     *
     * May be called concurrently
     */
    void
    load(DatabaseCon& dbCon, std::string const& dbTable);

    /**
     * Save cached manifests to database.
     *
     * @param dbCon Database connection with `ValidatorManifests` table
     *
     * @param isTrusted Function that returns true if manifest is trusted
     *
     * @par Thread Safety
     *
     * May be called concurrently
     */
    void
    save(
        DatabaseCon& dbCon,
        std::string const& dbTable,
        std::function<bool(PublicKey const&)> const& isTrusted);

    /**
     * Invokes the callback once for every populated manifest.
     *
     * @note Do not call ManifestCache member functions from within the
     * callback. This can re-lock the mutex from the same thread, which is UB.
     * @note Do not write ManifestCache member variables from within the
     * callback. This can lead to data races.
     *
     * @param f Function called for each manifest
     *
     * @par Thread Safety
     *
     * May be called concurrently
     */
    template <class Function>
    void
    forEachManifest(Function&& f) const
    {
        std::shared_lock const lock{mutex_};
        for (auto const& [_, manifest] : map_)
        {
            (void)_;
            f(manifest);
        }
    }

    /**
     * Invokes the callback once for every populated manifest.
     *
     * @note Do not call ManifestCache member functions from within the
     * callback. This can re-lock the mutex from the same thread, which is UB.
     * @note Do not write ManifestCache member variables from
     * within the callback. This can lead to data races.
     *
     * @param pf Pre-function called with the maximum number of times f will be
     *     called (useful for memory allocations)
     *
     * @param f Function called for each manifest
     *
     * @par Thread Safety
     *
     * May be called concurrently
     */
    template <class PreFun, class EachFun>
    void
    forEachManifest(PreFun&& pf, EachFun&& f) const
    {
        std::shared_lock const lock{mutex_};
        pf(map_.size());
        for (auto const& [_, manifest] : map_)
        {
            (void)_;
            f(manifest);
        }
    }
};

}  // namespace xrpl
