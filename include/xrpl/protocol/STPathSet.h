/** @file
 *  Defines the three-class hierarchy for payment path representation in XRPL
 *  transactions.
 *
 *  `STPathElement` is a single hop; `STPath` is an ordered sequence of hops;
 *  `STPathSet` is the collection of alternate candidate paths carried in the
 *  `Paths` field of a `Payment` transaction on the wire. Together they encode
 *  how a cross-currency payment routes through the order book and the trust-line
 *  graph from source to destination.
 */

#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/PathAsset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstddef>
#include <optional>

namespace xrpl {

/** A single hop in a payment path.
 *
 *  A node is either an *account node* (rippling through trust lines) or an
 *  *offer node* (matching against a DEX order book). `isOffer()` returns `true`
 *  when `mAccountID` is the XRP "no account" sentinel; otherwise the element
 *  represents an account. The `Type` bitmask drives both on-wire encoding and
 *  runtime dispatch.
 *
 *  The asset field holds a `PathAsset` variant (`Currency` for legacy IOU hops,
 *  `MPTID` for MPT hops). `TypeCurrency` and `TypeMpt` are mutually exclusive.
 *
 *  A non-cryptographic hash of the account, asset, and issuer fields is
 *  pre-computed at construction (`hash_value_`). `operator==` short-circuits on
 *  this hash before performing field-by-field comparison, making duplicate
 *  detection in the pathfinder fast over the small vectors used in practice.
 *
 *  @see STPath, STPathSet
 */
class STPathElement final : public CountedObject<STPathElement>
{
    unsigned int type_;
    AccountID accountID_;
    PathAsset assetID_;
    AccountID issuerID_;

    bool is_offer_;
    std::size_t hash_value_;

public:
    /** Bitmask constants that govern on-wire encoding and runtime dispatch.
     *
     *  Each hop's type byte is the OR of the applicable constants. The
     *  deserializer rejects any byte with bits outside `TypeAll` as malformed.
     *  `TypeCurrency` and `TypeMpt` are mutually exclusive; `TypeAsset` is a
     *  convenience mask to test either without caring which.
     */
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
    enum Type {
        TypeNone = 0x00,      /**< Path terminator (0x00 byte ends the PathSet). */
        TypeAccount = 0x01,   /**< Account field is present; node ripples through trust lines. */
        TypeCurrency = 0x10,  /**< Legacy IOU Currency (160-bit) follows. Mutually exclusive with TypeMpt. */
        TypeIssuer = 0x20,    /**< Issuer AccountID (160-bit) follows. */
        TypeMpt = 0x40,       /**< MPT issuance ID (192-bit MPTID) follows. Mutually exclusive with TypeCurrency. */
        TypeBoundary = 0xFF,  /**< Separator between consecutive paths within the PathSet. */
        TypeAsset = TypeCurrency | TypeMpt,              /**< Either asset kind; tests presence without distinguishing IOU vs MPT. */
        TypeAll = TypeAccount | TypeCurrency | TypeIssuer | TypeMpt, /**< Union of all valid type bits; used to validate incoming bytes. */
    };

    /** Construct a `TypeNone` (path-terminator / empty) element.
     *
     *  The resulting element has `is_offer_ = true` and all fields zero.
     *  Used as a sentinel and by default-constructed STPath entries.
     */
    STPathElement();
    STPathElement(STPathElement const&) = default;
    STPathElement&
    operator=(STPathElement const&) = default;

    /** Construct an element from optional fields, setting type bits automatically.
     *
     *  The type bitmask is derived from which optionals are non-null:
     *  `TypeAccount` if `account` is set, `TypeCurrency`/`TypeMpt` from the
     *  `PathAsset` variant if `asset` is set, and `TypeIssuer` if `issuer` is
     *  set. Asserts (debug builds) that account and issuer are not `noAccount()`
     *  when provided.
     *
     *  @param account AccountID of the hop; absent means this is an offer node.
     *  @param asset   PathAsset (Currency or MPTID) for the hop; absent means
     *      no asset constraint.
     *  @param issuer  Issuer AccountID; absent means no issuer constraint.
     */
    STPathElement(
        std::optional<AccountID> const& account,
        std::optional<PathAsset> const& asset,
        std::optional<AccountID> const& issuer);

    /** Construct an element from explicit non-optional fields.
     *
     *  `TypeAccount` is set when `account` is not the XRP sentinel; the asset
     *  type bit (`TypeCurrency` or `TypeMpt`) is set when the asset is not XRP
     *  (or unconditionally when `forceAsset` is `true`); `TypeIssuer` is set
     *  when `issuer` is not the XRP sentinel.
     *
     *  @param account    AccountID of the hop; XRP sentinel (`xrpAccount()`)
     *      means offer node.
     *  @param asset      PathAsset describing the hop's currency or MPT.
     *  @param issuer     Issuer AccountID.
     *  @param forceAsset When `true`, always set the asset type bit even if the
     *      asset is XRP. Used to preserve currency information in offer nodes
     *      whose asset happens to be XRP.
     */
    STPathElement(
        AccountID const& account,
        PathAsset const& asset,
        AccountID const& issuer,
        bool forceAsset = false);

    /** Construct an element from an explicit wire-format type byte and fields.
     *
     *  Used by the deserializer. The type byte is accepted verbatim and then
     *  sanitised: the actual `PathAsset` variant is inspected to clear the
     *  contradictory bit (`TypeMpt` when holding a `Currency`, or
     *  `TypeCurrency` when holding an `MPTID`), so a caller cannot pass a
     *  self-contradictory bitmask.
     *
     *  @param uType   Wire-format type bitmask from the stream.
     *  @param account AccountID of the hop.
     *  @param asset   PathAsset (Currency or MPTID) for the hop.
     *  @param issuer  Issuer AccountID.
     */
    STPathElement(
        unsigned int uType,
        AccountID const& account,
        PathAsset const& asset,
        AccountID const& issuer);

    /** Return the raw type bitmask for this element.
     *
     *  The result is the OR of the applicable `Type` constants and can be
     *  inspected with `isType()` or tested directly against the `Type` enum.
     */
    [[nodiscard]] auto
    getNodeType() const;

    /** Return `true` if this element represents a DEX offer node.
     *
     *  An element is an offer node when its account field is the XRP "no
     *  account" sentinel, meaning the hop matches against the order book
     *  rather than rippling through a trust line.
     */
    [[nodiscard]] bool
    isOffer() const;

    /** Return `true` if this element represents a trust-line account node.
     *
     *  Equivalent to `!isOffer()`.
     */
    [[nodiscard]] bool
    isAccount() const;

    /** Return `true` if the `TypeIssuer` bit is set. */
    [[nodiscard]] bool
    hasIssuer() const;

    /** Return `true` if the `TypeCurrency` bit is set (legacy IOU hop). */
    [[nodiscard]] bool
    hasCurrency() const;

    /** Return `true` if the `TypeMpt` bit is set (MPT hop). */
    [[nodiscard]] bool
    hasMPT() const;

    /** Return `true` if any asset type bit (`TypeCurrency` or `TypeMpt`) is set. */
    [[nodiscard]] bool
    hasAsset() const;

    /** Return `true` if this element is a path terminator (`TypeNone`). */
    [[nodiscard]] bool
    isNone() const;

    /** Return the account for this hop.
     *
     *  For account nodes this is the AccountID through which the payment
     *  ripples. For offer nodes the field holds the XRP "no account" sentinel
     *  and callers should use `isOffer()` to distinguish the two cases before
     *  interpreting this value.
     */
    [[nodiscard]] AccountID const&
    getAccountID() const;

    /** Return the `PathAsset` (Currency or MPTID) for this hop. */
    [[nodiscard]] PathAsset const&
    getPathAsset() const;

    /** Return the `Currency` for this hop.
     *
     *  @note Only valid when `hasCurrency()` is `true`; the underlying
     *      `PathAsset::get<Currency>()` throws on type mismatch in debug builds.
     */
    [[nodiscard]] Currency const&
    getCurrency() const;

    /** Return the `MPTID` for this hop.
     *
     *  @note Only valid when `hasMPT()` is `true`; the underlying
     *      `PathAsset::get<MPTID>()` throws on type mismatch in debug builds.
     */
    [[nodiscard]] MPTID const&
    getMPTID() const;

    /** Return the issuer AccountID for this hop. */
    [[nodiscard]] AccountID const&
    getIssuerID() const;

    /** Return `true` if any bit of `pe` is set in the element's type bitmask.
     *
     *  @param pe Type mask to test; typically a single `Type` constant or a
     *      bitwise OR of several.
     */
    [[nodiscard]] bool
    isType(Type const& pe) const;

    /** Return `true` if the two elements have identical account, asset, and issuer fields.
     *
     *  Short-circuits first on the `TypeAccount` bit and the pre-computed hash
     *  before performing full field comparison, making deduplication fast over
     *  the small path vectors used in practice.
     */
    bool
    operator==(STPathElement const& t) const;

    /** Return `true` if the two elements differ in any field. */
    bool
    operator!=(STPathElement const& t) const;

private:
    /** Compute the non-cryptographic hash stored in `hash_value_`.
     *
     *  Uses FNV-style multiply-XOR with distinct primes (257, 509, 911) for
     *  the account, asset, and issuer fields respectively, then XORs the three
     *  sub-hashes together. Reads the actual `PathAsset` variant via `visit()`
     *  rather than the type bitmask, because the bitmask may be partially set
     *  during pathfinder construction. Speed dominates; cryptographic strength
     *  is not required.
     *
     *  @param element The element to hash.
     *  @return Non-cryptographic hash combining account, asset, and issuer.
     */
    static std::size_t
    getHash(STPathElement const& element);
};

/** An ordered sequence of `STPathElement` hops describing one candidate payment path.
 *
 *  Wraps a `std::vector<STPathElement>` with a standard container interface.
 *  The XRPL protocol caps path length, so the underlying vector is short in
 *  practice (typically 2–6 elements); linear scans are therefore acceptable.
 *
 *  @see STPathElement, STPathSet
 */
class STPath final : public CountedObject<STPath>
{
    std::vector<STPathElement> path_;

public:
    STPath() = default;

    /** Construct a path from an existing vector of elements.
     *
     *  @param p Elements to populate the path; moved into internal storage.
     */
    STPath(std::vector<STPathElement> p);

    /** Return the number of hops in this path. */
    [[nodiscard]] std::vector<STPathElement>::size_type
    size() const;

    /** Return `true` if the path contains no hops. */
    [[nodiscard]] bool
    empty() const;

    /** Append a copy of `e` to the end of this path. */
    void
    pushBack(STPathElement const& e);

    /** Emplace a new element at the end of this path.
     *
     *  @tparam Args Argument types forwarded to `STPathElement`'s constructor.
     *  @param args  Arguments forwarded to the new element's constructor.
     */
    template <typename... Args>
    void
    emplaceBack(Args&&... args);

    /** Return `true` if any hop in this path matches the given (account, asset, issuer) triple.
     *
     *  Used by the pathfinder for cycle detection: before extending a path,
     *  `Pathfinder::addLink()` calls this to ensure the candidate hop has not
     *  already appeared earlier in the path. A linear scan is acceptable
     *  because XRPL path lengths are protocol-bounded.
     *
     *  @param account AccountID of the hop to search for.
     *  @param asset   PathAsset (Currency or MPTID) to match.
     *  @param issuer  Issuer AccountID to match.
     *  @return `true` if any existing element equals all three arguments.
     */
    [[nodiscard]] bool
    hasSeen(AccountID const& account, PathAsset const& asset, AccountID const& issuer) const;

    /** Serialize the path to a JSON array of hop objects.
     *
     *  Each hop object always includes a `type` field. Optional `account`,
     *  `currency`, `mpt_issuance_id`, and `issuer` keys are present only when
     *  the corresponding type bit is set.
     *
     *  @return JSON array where each element describes one hop.
     */
    [[nodiscard]] json::Value getJson(JsonOptions) const;

    /** Return an iterator to the first element. */
    [[nodiscard]] std::vector<STPathElement>::const_iterator
    begin() const;

    /** Return a past-the-end iterator. */
    [[nodiscard]] std::vector<STPathElement>::const_iterator
    end() const;

    /** Return `true` if both paths contain identical elements in identical order. */
    bool
    operator==(STPath const& t) const;

    /** Return a reference to the last element. */
    [[nodiscard]] std::vector<STPathElement>::const_reference
    back() const;

    /** Return a reference to the first element. */
    [[nodiscard]] std::vector<STPathElement>::const_reference
    front() const;

    /** Return a mutable reference to the element at index `i`. */
    STPathElement&
    operator[](int i);

    /** Return a const reference to the element at index `i`. */
    STPathElement const&
    operator[](int i) const;

    /** Reserve capacity for `s` elements, avoiding reallocations during path construction.
     *
     *  @param s Minimum capacity to reserve.
     */
    void
    reserve(size_t s);
};

//------------------------------------------------------------------------------

/** The serialized `Paths` field of a Payment transaction — a collection of alternate payment paths.
 *
 *  Inherits from `STBase` and participates in the ledger type system via
 *  `STI_PATHSET`. The binary wire format encodes each `STPath` as a sequence
 *  of type-tagged hop records; consecutive paths are delimited by
 *  `TypeBoundary` (0xFF) and the entire field is terminated by `TypeNone`
 *  (0x00). Deserialization throws `std::runtime_error` on malformed input
 *  (empty paths or unknown type bits).
 *
 *  The `isDefault()` override returns `true` when the set is empty, allowing
 *  the serialization layer to elide the field from transactions that have no
 *  explicit paths.
 *
 *  @see STPath, STPathElement
 */
class STPathSet final : public STBase, public CountedObject<STPathSet>
{
    std::vector<STPath> value_;

public:
    STPathSet() = default;

    /** Construct an empty STPathSet named by `n`. */
    STPathSet(SField const& n);

    /** Deserialize an STPathSet from a binary stream.
     *
     *  Reads the wire format produced by `add()`: hop records delimited by
     *  `TypeBoundary` (0xFF) and terminated by `TypeNone` (0x00).
     *
     *  @param sit  Binary cursor positioned at the first type byte; advanced
     *      past the terminating `TypeNone` on return.
     *  @param name SField that names this field in the enclosing object.
     *  @throws std::runtime_error "empty path" if a boundary or terminator is
     *      encountered before any hop is accumulated.
     *  @throws std::runtime_error "bad path element" if a type byte contains
     *      bits outside `TypeAll`.
     */
    STPathSet(SerialIter& sit, SField const& name);

    /** Serialize the path set to its canonical binary wire format.
     *
     *  Emits each hop as a type byte followed by its optional account (20B),
     *  MPTID (24B), currency (20B), and/or issuer (20B) payloads. Consecutive
     *  paths are separated by `TypeBoundary` (0xFF); the set ends with
     *  `TypeNone` (0x00).
     *
     *  @param s Serializer accumulator to which bytes are appended.
     */
    void
    add(Serializer& s) const override;

    /** Serialize the path set to a JSON array of path arrays.
     *
     *  @param options JSON rendering options forwarded to each path.
     *  @return Nested JSON array: `[[hop, ...], [hop, ...], ...]`.
     */
    [[nodiscard]] json::Value getJson(JsonOptions options) const override;

    /** Return `STI_PATHSET`, identifying this field to the serialization framework. */
    [[nodiscard]] SerializedTypeID
    getSType() const override;

    /** Append `base` extended by `tail` to the set, unless an identical path already exists.
     *
     *  Used by the pathfinder to build candidate paths incrementally. The
     *  candidate is pushed onto `value_` and then scanned against existing
     *  paths in reverse order (newest-first) to detect duplicates; if found,
     *  the candidate is popped and `false` is returned. Reverse iteration is a
     *  micro-optimisation because duplicates are most likely among recently
     *  added paths.
     *
     *  @param base Prefix path to extend.
     *  @param tail Single hop appended to `base` before comparison.
     *  @return `true` if the path was added; `false` if it was a duplicate.
     */
    bool
    assembleAdd(STPath const& base, STPathElement const& tail);

    /** Return `true` if `t` is an STPathSet with identical path contents.
     *
     *  @param t Object to compare; returns `false` immediately if not an STPathSet.
     */
    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    /** Return `true` when the set contains no paths.
     *
     *  The serialization layer uses this to elide the `Paths` field from
     *  transactions that require no explicit pathfinding routes.
     */
    [[nodiscard]] bool
    isDefault() const override;

    // std::vector-like interface for iterating and indexing paths.

    /** Return a const reference to the path at index `n`. */
    std::vector<STPath>::const_reference
    operator[](std::vector<STPath>::size_type n) const;

    /** Return a mutable reference to the path at index `n`. */
    std::vector<STPath>::reference
    operator[](std::vector<STPath>::size_type n);

    /** Return an iterator to the first path. */
    [[nodiscard]] std::vector<STPath>::const_iterator
    begin() const;

    /** Return a past-the-end iterator. */
    [[nodiscard]] std::vector<STPath>::const_iterator
    end() const;

    /** Return the number of paths in the set. */
    [[nodiscard]] std::vector<STPath>::size_type
    size() const;

    /** Return `true` if the set contains no paths. */
    [[nodiscard]] bool
    empty() const;

    /** Append a copy of `e` to the set.
     *
     *  @note Does not deduplicate; use `assembleAdd()` when deduplication is needed.
     */
    void
    pushBack(STPath const& e);

    /** Emplace a new path at the end of the set.
     *
     *  @tparam Args Argument types forwarded to `STPath`'s constructor.
     *  @param args  Arguments forwarded to the new path's constructor.
     */
    template <typename... Args>
    void
    emplaceBack(Args&&... args);

private:
    /** Copy-construct this STPathSet into `buf` via placement-new.
     *
     *  Plugs STPathSet into the `detail::STVar` small-object storage scheme.
     *
     *  @param n   Byte size of `buf`; must be at least `sizeof(STPathSet)`.
     *  @param buf Aligned destination buffer.
     *  @return Pointer to the newly constructed object.
     */
    STBase*
    copy(std::size_t n, void* buf) const override;

    /** Move-construct this STPathSet into `buf` via placement-new.
     *
     *  Plugs STPathSet into the `detail::STVar` small-object storage scheme.
     *
     *  @param n   Byte size of `buf`; must be at least `sizeof(STPathSet)`.
     *  @param buf Aligned destination buffer.
     *  @return Pointer to the newly constructed object.
     */
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

// ------------ STPathElement ------------

inline STPathElement::STPathElement() : type_(TypeNone), is_offer_(true)
{
    hash_value_ = getHash(*this);
}

inline STPathElement::STPathElement(
    std::optional<AccountID> const& account,
    std::optional<PathAsset> const& asset,
    std::optional<AccountID> const& issuer)
    : type_(TypeNone)
{
    if (!account)
    {
        is_offer_ = true;
    }
    else
    {
        is_offer_ = false;
        accountID_ = *account;
        type_ |= TypeAccount;
        XRPL_ASSERT(
            accountID_ != noAccount(), "xrpl::STPathElement::STPathElement : account is set");
    }

    if (asset)
    {
        assetID_ = *asset;
        type_ |= assetID_.holds<Currency>() ? TypeCurrency : TypeMpt;
    }

    if (issuer)
    {
        issuerID_ = *issuer;
        type_ |= TypeIssuer;
        XRPL_ASSERT(issuerID_ != noAccount(), "xrpl::STPathElement::STPathElement : issuer is set");
    }

    hash_value_ = getHash(*this);
}

inline STPathElement::STPathElement(
    AccountID const& account,
    PathAsset const& asset,
    AccountID const& issuer,
    bool forceAsset)
    : type_(TypeNone)
    , accountID_(account)
    , assetID_(asset)
    , issuerID_(issuer)
    , is_offer_(isXRP(accountID_))
{
    if (!is_offer_)
        type_ |= TypeAccount;

    if (forceAsset || !isXRP(assetID_))
        type_ |= asset.holds<Currency>() ? TypeCurrency : TypeMpt;

    if (!isXRP(issuer))
        type_ |= TypeIssuer;

    hash_value_ = getHash(*this);
}

inline STPathElement::STPathElement(
    unsigned int uType,
    AccountID const& account,
    PathAsset const& asset,
    AccountID const& issuer)
    : type_(uType)
    , accountID_(account)
    , assetID_(asset)
    , issuerID_(issuer)
    , is_offer_(isXRP(accountID_))
{
    assetID_.visit(
        [&](Currency const&) { type_ = type_ & (~Type::TypeMpt); },
        [&](MPTID const&) { type_ = type_ & (~Type::TypeCurrency); });
    hash_value_ = getHash(*this);
}

inline auto
STPathElement::getNodeType() const
{
    return type_;
}

inline bool
STPathElement::isOffer() const
{
    return is_offer_;
}

inline bool
STPathElement::isAccount() const
{
    return !isOffer();
}

inline bool
STPathElement::isType(Type const& pe) const
{
    return (type_ & pe) != 0u;
}

inline bool
STPathElement::hasIssuer() const
{
    return isType(STPathElement::TypeIssuer);
}

inline bool
STPathElement::hasCurrency() const
{
    return isType(STPathElement::TypeCurrency);
}

inline bool
STPathElement::hasMPT() const
{
    return isType(STPathElement::TypeMpt);
}

inline bool
STPathElement::hasAsset() const
{
    return isType(STPathElement::TypeAsset);
}

inline bool
STPathElement::isNone() const
{
    return getNodeType() == STPathElement::TypeNone;
}

inline AccountID const&
STPathElement::getAccountID() const
{
    return accountID_;
}

inline PathAsset const&
STPathElement::getPathAsset() const
{
    return assetID_;
}

inline Currency const&
STPathElement::getCurrency() const
{
    return assetID_.get<Currency>();
}

inline MPTID const&
STPathElement::getMPTID() const
{
    return assetID_.get<MPTID>();
}

inline AccountID const&
STPathElement::getIssuerID() const
{
    return issuerID_;
}

inline bool
STPathElement::operator==(STPathElement const& t) const
{
    return (type_ & TypeAccount) == (t.type_ & TypeAccount) && hash_value_ == t.hash_value_ &&
        accountID_ == t.accountID_ && assetID_ == t.assetID_ && issuerID_ == t.issuerID_;
}

inline bool
STPathElement::operator!=(STPathElement const& t) const
{
    return !operator==(t);
}

// ------------ STPath ------------

inline STPath::STPath(std::vector<STPathElement> p) : path_(std::move(p))
{
}

inline std::vector<STPathElement>::size_type
STPath::size() const
{
    return path_.size();
}

inline bool
STPath::empty() const
{
    return path_.empty();
}

inline void
STPath::pushBack(STPathElement const& e)
{
    path_.push_back(e);
}

template <typename... Args>
inline void
STPath::emplaceBack(Args&&... args)
{
    path_.emplace_back(std::forward<Args>(args)...);
}

inline std::vector<STPathElement>::const_iterator
STPath::begin() const
{
    return path_.begin();
}

inline std::vector<STPathElement>::const_iterator
STPath::end() const
{
    return path_.end();
}

inline bool
STPath::operator==(STPath const& t) const
{
    return path_ == t.path_;
}

inline std::vector<STPathElement>::const_reference
STPath::back() const
{
    return path_.back();
}

inline std::vector<STPathElement>::const_reference
STPath::front() const
{
    return path_.front();
}

inline STPathElement&
STPath::operator[](int i)
{
    return path_[i];
}

inline STPathElement const&
STPath::operator[](int i) const
{
    return path_[i];
}

inline void
STPath::reserve(size_t s)
{
    path_.reserve(s);
}

// ------------ STPathSet ------------

inline STPathSet::STPathSet(SField const& n) : STBase(n)
{
}

inline std::vector<STPath>::const_reference
STPathSet::operator[](std::vector<STPath>::size_type n) const
{
    return value_[n];
}

inline std::vector<STPath>::reference
STPathSet::operator[](std::vector<STPath>::size_type n)
{
    return value_[n];
}

inline std::vector<STPath>::const_iterator
STPathSet::begin() const
{
    return value_.begin();
}

inline std::vector<STPath>::const_iterator
STPathSet::end() const
{
    return value_.end();
}

inline std::vector<STPath>::size_type
STPathSet::size() const
{
    return value_.size();
}

inline bool
STPathSet::empty() const
{
    return value_.empty();
}

inline void
STPathSet::pushBack(STPath const& e)
{
    value_.push_back(e);
}

template <typename... Args>
inline void
STPathSet::emplaceBack(Args&&... args)
{
    value_.emplace_back(std::forward<Args>(args)...);
}

}  // namespace xrpl
