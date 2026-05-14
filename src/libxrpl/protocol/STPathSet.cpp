/**
 * @file STPathSet.cpp
 * @brief Binary serialization, deserialization, hashing, cycle-detection, and
 *     JSON rendering for STPathSet, STPath, and STPathElement.
 *
 * Cross-currency payments carry candidate routes in the `Paths` field of a
 * Payment transaction as an `STPathSet`. Each `STPath` is an ordered sequence
 * of `STPathElement` hop descriptors that guide the payment engine through
 * trust-line rippling nodes and order-book offer nodes.
 */

#include <xrpl/protocol/STPathSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace xrpl {

/**
 * Compute a non-cryptographic hash over a path element's account, asset, and
 * issuer fields for fast equality short-circuiting.
 *
 * Uses the pattern `hash = hash * prime ^ byte` with distinct small primes
 * (257, 509, 911) per field to reduce collisions, then XORs the three
 * sub-hashes together. The result is stored eagerly in `hash_value_` at
 * construction time so that `operator==` can reject unequal elements without
 * a full field comparison.
 *
 * @note The hash is derived from the actual `PathAsset` variant contents
 *     (via `getPathAsset().visit()`), not from the `type_` bitmask, because
 *     `Pathfinder::addLink()` may set `TypeAccount` while the asset slot still
 *     holds a currency or MPT value.
 * @note This hash need not be cryptographically secure; speed dominates.
 * @param element The element to hash.
 * @return Non-cryptographic hash combining account, asset, and issuer bytes.
 */
std::size_t
STPathElement::getHash(STPathElement const& element)
{
    std::size_t hashAccount = 2654435761;
    std::size_t hashCurrency = 2654435761;
    std::size_t hashIssuer = 2654435761;

    for (auto const x : element.getAccountID())
        hashAccount += (hashAccount * 257) ^ x;

    // Dispatch on actual PathAsset contents rather than type_ because type_
    // may carry TypeAccount while the asset slot is still populated
    // (e.g. from Pathfinder::addLink()).
    element.getPathAsset().visit(
        [&](MPTID const& mpt) { hashCurrency += beast::Uhash<>{}(mpt); },
        [&](Currency const& currency) {
            for (auto const x : currency)
                hashCurrency += (hashCurrency * 509) ^ x;
        });

    for (auto const x : element.getIssuerID())
        hashIssuer += (hashIssuer * 911) ^ x;

    return (hashAccount ^ hashCurrency ^ hashIssuer);
}

/**
 * Deserialize an STPathSet from a binary stream.
 *
 * Parses the wire format produced by `add()`: a sequence of one-byte type
 * tags followed by the optional account (20 bytes), currency (20 bytes),
 * MPT ID (24 bytes), and/or issuer (20 bytes) payloads for each hop.
 * Paths are delimited by `TypeBoundary` (0xFF) bytes and the set is
 * terminated by a `TypeNone` (0x00) byte.
 *
 * @param sit   Binary cursor positioned at the first type byte of the
 *     path set; advanced past the terminating `TypeNone` on return.
 * @param name  SField that names this path set field in the enclosing object.
 * @throws std::runtime_error with message "empty path" if a `TypeBoundary`
 *     or `TypeNone` marker is encountered before any hop has been accumulated,
 *     indicating corrupt or malformed input.
 * @throws std::runtime_error with message "bad path element" if a type byte
 *     contains bits outside `TypeAll` (0x71), indicating an unknown element
 *     kind.
 * @note Setting both `TypeCurrency` and `TypeMpt` in a single hop's type byte
 *     is a protocol invariant violation; an assertion fires in debug builds.
 */
STPathSet::STPathSet(SerialIter& sit, SField const& name) : STBase(name)
{
    std::vector<STPathElement> path;
    for (;;)
    {
        int const iType = sit.get8();

        if (iType == STPathElement::TypeNone || iType == STPathElement::TypeBoundary)
        {
            if (path.empty())
            {
                JLOG(debugLog().error()) << "Empty path in pathset";
                Throw<std::runtime_error>("empty path");
            }

            pushBack(path);
            path.clear();

            if (iType == STPathElement::TypeNone)
                return;
        }
        else if ((iType & ~STPathElement::TypeAll) != 0)
        {
            JLOG(debugLog().error()) << "Bad path element " << iType << " in pathset";
            Throw<std::runtime_error>("bad path element");
        }
        else
        {
            auto const hasAccount = (iType & STPathElement::TypeAccount) != 0u;
            auto const hasCurrency = (iType & STPathElement::TypeCurrency) != 0u;
            auto const hasIssuer = (iType & STPathElement::TypeIssuer) != 0u;
            auto const hasMPT = (iType & STPathElement::TypeMpt) != 0u;

            AccountID account;
            PathAsset asset;
            AccountID issuer;

            if (hasAccount)
                account = sit.get160();

            XRPL_ASSERT(
                !(hasCurrency && hasMPT), "xrpl::STPathSet::STPathSet : not has Currency and MPT");
            if (hasCurrency)
                asset = static_cast<Currency>(sit.get160());

            if (hasMPT)
                asset = sit.get192();

            if (hasIssuer)
                issuer = sit.get160();

            path.emplace_back(account, asset, issuer, hasCurrency);
        }
    }
}

/**
 * Copy-construct this STPathSet into an external buffer via placement-new.
 *
 * Supports the `detail::STVar` small-object storage used by `STObject` to
 * keep serialized fields compact without per-field heap allocation.
 *
 * @param n   Size in bytes of the buffer at `buf`; must be at least
 *     `sizeof(STPathSet)`.
 * @param buf Aligned storage into which the copy is placement-constructed.
 * @return Pointer to the newly constructed object within `buf`.
 */
STBase*
STPathSet::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

/**
 * Move-construct this STPathSet into an external buffer via placement-new.
 *
 * Supports the `detail::STVar` small-object storage used by `STObject`.
 *
 * @param n   Size in bytes of the buffer at `buf`; must be at least
 *     `sizeof(STPathSet)`.
 * @param buf Aligned storage into which the object is placement-constructed.
 * @return Pointer to the newly constructed object within `buf`.
 */
STBase*
STPathSet::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

/**
 * Append `base` extended by `tail` to the set, unless an identical path is
 * already present.
 *
 * Used by the pathfinder to build candidate paths incrementally. The candidate
 * is formed by pushing `base` into `value_` and then appending `tail`. The
 * existing paths are then scanned in reverse order — newest-first — to detect
 * a duplicate; if one is found the candidate is popped and `false` is
 * returned. Reverse iteration is a micro-optimisation because duplicates are
 * most likely to be the most recently added paths.
 *
 * Deduplication at insertion time bounds the set size during pathfinding,
 * preventing the payment engine from simulating redundant paths.
 *
 * @param base  Prefix path to extend.
 * @param tail  Single hop to append to `base` before adding to this set.
 * @return `true` if the path was added; `false` if it was a duplicate and
 *     discarded.
 */
bool
STPathSet::assembleAdd(STPath const& base, STPathElement const& tail)
{
    value_.push_back(base);

    std::vector<STPath>::reverse_iterator it = value_.rbegin();

    STPath& newPath = *it;
    newPath.pushBack(tail);

    while (++it != value_.rend())
    {
        if (*it == newPath)
        {
            value_.pop_back();
            return false;
        }
    }
    return true;
}

/**
 * Return `true` if `t` is an STPathSet with identical path contents.
 *
 * @param t Object to compare; returns `false` immediately if it is not an
 *     STPathSet.
 */
bool
STPathSet::isEquivalent(STBase const& t) const
{
    STPathSet const* v = dynamic_cast<STPathSet const*>(&t);
    return (v != nullptr) && (value_ == v->value_);
}

/**
 * Return `true` when the path set contains no paths (default/empty state).
 *
 * Used by the serialization layer to elide the field from a transaction when
 * no paths have been supplied.
 */
bool
STPathSet::isDefault() const
{
    return value_.empty();
}

/**
 * Return `true` if the path already contains a hop with the given account,
 * asset, and issuer combination.
 *
 * Used by the pathfinder for cycle detection: before extending a path with a
 * new hop, calling `hasSeen()` prevents the payment engine from being handed
 * a route that would loop back through a node it has already visited.
 *
 * @param account AccountID of the hop to look for.
 * @param asset   PathAsset (Currency or MPTID) of the hop.
 * @param issuer  Issuer AccountID of the hop.
 * @return `true` if any element in the path matches all three fields exactly.
 */
bool
STPath::hasSeen(AccountID const& account, PathAsset const& asset, AccountID const& issuer) const
{
    for (auto& p : path_)
    {
        if (p.getAccountID() == account && p.getPathAsset() == asset && p.getIssuerID() == issuer)
            return true;
    }

    return false;
}

/**
 * Serialize the path to a JSON array of hop objects.
 *
 * Each hop object always includes a numeric `type` field so consumers can
 * identify the hop kind without re-parsing optional keys. Additional fields
 * are present only when their corresponding type bit is set:
 * - `TypeAccount` → `"account"` (base58-encoded AccountID)
 * - `TypeCurrency` → `"currency"` (ISO currency string)
 * - `TypeMpt` → `"mpt_issuance_id"` (hex-encoded MPTID)
 * - `TypeIssuer` → `"issuer"` (base58-encoded AccountID)
 *
 * @note `TypeCurrency` and `TypeMpt` are mutually exclusive per protocol;
 *     an assertion fires in debug builds if both bits are set.
 * @return JSON array containing one object per hop.
 */
json::Value
STPath::getJson(JsonOptions) const
{
    json::Value ret(json::ValueType::Array);

    for (auto const& it : path_)
    {
        json::Value elem(json::ValueType::Object);
        auto const iType = it.getNodeType();

        elem[jss::type] = iType;

        if ((iType & STPathElement::TypeAccount) != 0u)
            elem[jss::account] = to_string(it.getAccountID());

        XRPL_ASSERT(
            ((iType & STPathElement::TypeCurrency) == 0u) ||
                ((iType & STPathElement::TypeMpt) == 0u),
            "xrpl::STPath::getJson : not type Currency and MPT");
        if ((iType & STPathElement::TypeCurrency) != 0u)
            elem[jss::currency] = to_string(it.getCurrency());

        if ((iType & STPathElement::TypeMpt) != 0u)
            elem[jss::mpt_issuance_id] = to_string(it.getMPTID());

        if ((iType & STPathElement::TypeIssuer) != 0u)
            elem[jss::issuer] = to_string(it.getIssuerID());

        ret.append(elem);
    }

    return ret;
}

/**
 * Serialize the path set to a JSON array of path arrays.
 *
 * Delegates to `STPath::getJson()` for each path, producing a nested
 * array structure: `[[hop, ...], [hop, ...], ...]`.
 *
 * @param options JSON rendering options forwarded to each path.
 * @return JSON array where each element is the JSON representation of one
 *     candidate payment path.
 */
json::Value
STPathSet::getJson(JsonOptions options) const
{
    json::Value ret(json::ValueType::Array);
    for (auto const& it : value_)
        ret.append(it.getJson(options));

    return ret;
}

/**
 * Return the serialized type identifier for this field (`STI_PATHSET`).
 *
 * Used by the generic serialization infrastructure to select the correct
 * codec when encoding or decoding an `STPathSet` field.
 */
SerializedTypeID
STPathSet::getSType() const
{
    return STI_PATHSET;
}

/**
 * Serialize the path set to its canonical binary wire format.
 *
 * Emits each path as a sequence of (type-byte, payload…) hop records.
 * Consecutive paths are separated by a `TypeBoundary` (0xFF) byte, and the
 * entire set is terminated by a `TypeNone` (0x00) byte. This is the inverse
 * of the deserialization constructor.
 *
 * Wire layout per hop:
 * - 1-byte type bitmask (`TypeAccount | TypeCurrency | TypeIssuer | TypeMpt`)
 * - 20-byte AccountID, if `TypeAccount` is set
 * - 24-byte MPTID, if `TypeMpt` is set
 * - 20-byte Currency, if `TypeCurrency` is set
 * - 20-byte issuer AccountID, if `TypeIssuer` is set
 *
 * @param s Serializer accumulator to which the encoded bytes are appended.
 */
void
STPathSet::add(Serializer& s) const
{
    XRPL_ASSERT(getFName().isBinary(), "xrpl::STPathSet::add : field is binary");
    XRPL_ASSERT(getFName().fieldType == STI_PATHSET, "xrpl::STPathSet::add : valid field type");
    bool first = true;

    for (auto const& spPath : value_)
    {
        if (!first)
            s.add8(STPathElement::TypeBoundary);

        for (auto const& speElement : spPath)
        {
            int const iType = speElement.getNodeType();

            s.add8(iType);

            if ((iType & STPathElement::TypeAccount) != 0u)
                s.addBitString(speElement.getAccountID());

            if ((iType & STPathElement::TypeMpt) != 0u)
                s.addBitString(speElement.getMPTID());

            if ((iType & STPathElement::TypeCurrency) != 0u)
                s.addBitString(speElement.getCurrency());

            if ((iType & STPathElement::TypeIssuer) != 0u)
                s.addBitString(speElement.getIssuerID());
        }

        first = false;
    }

    s.add8(STPathElement::TypeNone);
}

}  // namespace xrpl
