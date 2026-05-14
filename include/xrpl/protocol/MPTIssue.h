#pragma once

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

/** Identifies a Multi-Purpose Token issuance, adapting `MPTID` to mirror
 *  the public interface of `Issue`.
 *
 *  `MPTIssue` wraps a single 192-bit `MPTID` (32-bit big-endian sequence
 *  concatenated with a 160-bit `AccountID`) and exposes the same accessors as
 *  `Issue` — `getIssuer()`, `getText()`, `setJson()`, `native()`, and
 *  `integral()` — allowing `Asset` and any template code constrained by
 *  `ValidIssueType` to treat MPTs and IOUs uniformly.
 *
 *  Key semantic differences from `Issue`:
 *  - `native()` always returns `false` (MPTs are never the native currency).
 *  - `integral()` always returns `true` (MPT amounts are 64-bit integers,
 *    unlike IOUs which use multi-precision rational arithmetic).
 *  - Equality and ordering compare the full 192-bit `MPTID`, with no
 *    special-case equivalence class for any sentinel value.
 *
 *  @see Issue, Asset, MPTID
 */
class MPTIssue
{
private:
    MPTID mptID_;

public:
    MPTIssue() = default;

    /** Constructs an MPTIssue from a pre-formed 192-bit issuance identifier.
     *
     *  @param issuanceID The packed MPTID (32-bit sequence ‖ 160-bit AccountID).
     */
    MPTIssue(MPTID const& issuanceID);

    /** Constructs an MPTIssue from the raw components of an MPTID.
     *
     *  Delegates to `xrpl::makeMptID(sequence, account)` to assemble the
     *  packed MPTID, saving callers from invoking that helper explicitly.
     *
     *  @param sequence The issuer's account sequence number at the time of
     *      issuance.
     *  @param account  The AccountID of the issuer.
     */
    MPTIssue(std::uint32_t sequence, AccountID const& account);

    /** Implicit conversion to the underlying `MPTID`.
     *
     *  Allows an `MPTIssue` to be passed wherever a raw `MPTID` is expected
     *  without an explicit cast.
     *
     *  @return A reference to the underlying `MPTID`, valid for the lifetime
     *      of this object.
     */
    operator MPTID const&() const
    {
        return mptID_;
    }

    /** Extracts the issuer's `AccountID` from the packed `MPTID`.
     *
     *  `MPTID` lays out 4 bytes of sequence followed immediately by 20 bytes
     *  of `AccountID`. This method returns a reference into that buffer by
     *  pointer-casting past the leading 4 bytes. A `static_assert` on the
     *  total size of `MPTID` guards against layout changes breaking this
     *  assumption at compile time.
     *
     *  @return A reference to the `AccountID` embedded in the `MPTID`.
     *      Valid for the lifetime of this `MPTIssue` object.
     *  @note The free function `getMPTIssuer()` achieves the same extraction
     *      via `std::bit_cast` and returns by value, avoiding any
     *      lifetime dependency on the source object.
     */
    [[nodiscard]] AccountID const&
    getIssuer() const;

    /** Returns the underlying `MPTID`.
     *
     *  @return A reference to the 192-bit issuance identifier, valid for the
     *      lifetime of this object.
     */
    [[nodiscard]] constexpr MPTID const&
    getMptID() const
    {
        return mptID_;
    }

    /** Returns the hex string representation of the underlying `MPTID`.
     *
     *  @return Uppercase hex encoding of the 192-bit issuance identifier.
     */
    [[nodiscard]] std::string
    getText() const;

    /** Writes the issuance identifier into a JSON object under the key
     *  `mpt_issuance_id`.
     *
     *  Serializes the `MPTID` as a hex string. Contrasted with
     *  `Issue::setJson()`, which writes separate `currency` and `issuer` keys.
     *
     *  @param jv Output JSON object to write into; existing keys are not
     *      cleared.
     */
    void
    setJson(json::Value& jv) const;

    friend constexpr bool
    operator==(MPTIssue const& lhs, MPTIssue const& rhs);

    friend constexpr std::weak_ordering
    operator<=>(MPTIssue const& lhs, MPTIssue const& rhs);

    /** Returns `false`; MPTs are never the native asset (XRP).
     *
     *  Mirrors `Issue::native()` so that generic code can query XRP-ness
     *  without a type dispatch. `Asset::getAmountType()` relies on this flag
     *  to select `XRPAmount` vs `IOUAmount` vs `MPTAmount` at compile time.
     *
     *  @return Always `false`.
     */
    static bool
    native()
    {
        return false;
    }

    /** Returns `true`; MPT amounts are stored as 64-bit integers.
     *
     *  Mirrors the naming of `Issue::integral()` so that generic code can
     *  distinguish integer (drop/MPT) amounts from multi-precision IOU
     *  amounts without a type dispatch. Unlike `Issue::integral()`, this is
     *  unconditionally `true` — all MPTs use integer arithmetic regardless of
     *  the token configuration.
     *
     *  @return Always `true`.
     */
    static bool
    integral()
    {
        return true;
    }
};

/** Returns `true` if two `MPTIssue` instances represent the same issuance.
 *
 *  Delegates to the full 192-bit comparison of the underlying `MPTID`s.
 *  Both the sequence number and the issuer `AccountID` must match; there is
 *  no partial-equality exemption as there is in `Issue::operator==` for XRP.
 *
 *  @param lhs Left-hand issuance.
 *  @param rhs Right-hand issuance.
 *  @return `true` iff both `MPTID`s are bitwise equal.
 */
constexpr bool
operator==(MPTIssue const& lhs, MPTIssue const& rhs)
{
    return lhs.mptID_ == rhs.mptID_;
}

/** Provides a strict weak ordering over `MPTIssue` values.
 *
 *  Delegates to the 192-bit lexicographic comparison of the underlying
 *  `MPTID`s. The ordering is consistent with `operator==`: two issuances are
 *  equivalent iff their full `MPTID`s are identical.
 *
 *  @param lhs Left-hand issuance.
 *  @param rhs Right-hand issuance.
 *  @return A `std::weak_ordering` value consistent with `operator==`.
 */
constexpr std::weak_ordering
operator<=>(MPTIssue const& lhs, MPTIssue const& rhs)
{
    return lhs.mptID_ <=> rhs.mptID_;
}

/** Returns `false`; an `MPTID` never identifies the native XRP asset.
 *
 *  Provides the same naming convention as `isXRP(Issue)`, allowing call
 *  sites to test XRP-ness uniformly across both issue types.
 *
 *  @return Always `false`.
 */
inline bool
isXRP(MPTID const&)
{
    return false;
}

/** Extracts the issuer `AccountID` from a `MPTID` by value.
 *
 *  Copies the 20 bytes that follow the leading 4-byte sequence field into a
 *  temporary array, then uses `std::bit_cast` to reinterpret them as an
 *  `AccountID`. The `static_assert` on the total size of `MPTID` ensures the
 *  layout assumption holds; if the type ever gains padding the build fails.
 *  `std::bit_cast` is typically optimized to nothing in the final assembly.
 *
 *  @param mptid The 192-bit issuance identifier to extract from.
 *  @return The `AccountID` embedded in bytes 4–23 of `mptid`.
 *  @note Use `MPTIssue::getIssuer()` when a zero-copy reference into an
 *      existing `MPTIssue` object is sufficient. The rvalue overloads of
 *      this function are deleted to prevent dangling references from
 *      temporaries.
 */
inline AccountID
getMPTIssuer(MPTID const& mptid)
{
    static_assert(sizeof(MPTID) == (sizeof(std::uint32_t) + sizeof(AccountID)));
    // Extract the 20 bytes for the AccountID
    std::array<std::uint8_t, sizeof(AccountID)> bytes{};
    std::copy_n(mptid.data() + sizeof(std::uint32_t), sizeof(AccountID), bytes.begin());

    // bit_cast is a "magic" compiler intrinsic that is
    // usually optimized away to nothing in the final assembly.
    return std::bit_cast<AccountID>(bytes);
}

// Deleted to prevent a dangling-reference bug: if a temporary MPTID were
// accepted, the returned AccountID const& would immediately dangle.
AccountID const&
getMPTIssuer(MPTID const&&) = delete;
AccountID const&
getMPTIssuer(MPTID&&) = delete;

/** Returns the `MPTID` sentinel representing "no MPT".
 *
 *  Encodes `{ sequence=0, account=noAccount() }` — all-zero bits.
 *  Mirrors `noIssue()` in `Issue.h` for use in contexts where a
 *  missing or invalid MPT must be represented without `std::optional`.
 *
 *  @return The all-zero 192-bit sentinel `MPTID`.
 */
inline MPTID
noMPT()
{
    static MPTIssue const kMPT{0, noAccount()};
    return kMPT.getMptID();
}

/** Returns the `MPTID` sentinel representing a structurally invalid MPT.
 *
 *  Encodes `{ sequence=0, account=xrpAccount() }` — sequence zero with the
 *  XRP account address as issuer, which is a conventionally invalid issuer
 *  for MPTs. `Asset`'s `BadAsset` comparison detects this sentinel by
 *  checking `getIssuer() == xrpAccount()`.
 *
 *  @return The sentinel `MPTID` whose issuer is `xrpAccount()`.
 */
inline MPTID
badMPT()
{
    static MPTIssue const kMPT{0, xrpAccount()};
    return kMPT.getMptID();
}

/** Appends the underlying `MPTID` to a hasher.
 *
 *  Plugs `MPTIssue` into the Beast hashing framework, enabling use in
 *  Beast-aware hash maps and sets.
 *
 *  @tparam Hasher A type satisfying the `beast::hash_append` concept.
 *  @param h The hasher to append to.
 *  @param r The issuance whose `MPTID` is appended.
 */
template <class Hasher>
void
hash_append(Hasher& h, MPTIssue const& r)
{
    using beast::hash_append;
    hash_append(h, r.getMptID());
}

/** Returns the canonical wire-format JSON representation of an MPT issuance.
 *
 *  Convenience wrapper around `MPTIssue::setJson()`. The returned object
 *  contains a single `mpt_issuance_id` field with the hex-encoded `MPTID`.
 *
 *  @param mptIssue The issuance to serialize.
 *  @return A JSON object of the form `{"mpt_issuance_id": "<hex>"}`.
 */
json::Value
toJson(MPTIssue const& mptIssue);

/** Returns the hex string representation of an MPT issuance.
 *
 *  @param mptIssue The issuance to convert.
 *  @return Uppercase hex encoding of the underlying 192-bit `MPTID`.
 */
std::string
to_string(MPTIssue const& mptIssue);

/** Parses an MPT issuance from a JSON object.
 *
 *  Validates in strict order: `v` must be a JSON object; `currency` and
 *  `issuer` keys must be absent (their presence indicates IOU data routed
 *  to the wrong parser); `mpt_issuance_id` must be a string containing a
 *  valid 48-character hex-encoded `MPTID`.
 *
 *  @param jv The JSON value to parse; must be an object.
 *  @return The parsed `MPTIssue`.
 *  @throws std::runtime_error if `jv` is not a JSON object, or if `currency`
 *      or `issuer` keys are present.
 *  @throws json::Error if `mpt_issuance_id` is absent, not a string, or not
 *      a valid 192-bit hex value.
 *  @see toJson for the inverse operation.
 */
MPTIssue
mptIssueFromJson(json::Value const& jv);

/** Writes the hex representation of an MPT issuance to a stream.
 *
 *  @param os The output stream.
 *  @param x  The issuance to write.
 *  @return `os`, to allow chaining.
 */
std::ostream&
operator<<(std::ostream& os, MPTIssue const& x);

}  // namespace xrpl

namespace std {

/** Specializes `std::hash` for `xrpl::MPTID`, delegating to the type's own
 *  hasher.
 *
 *  Enables `MPTID` to be used directly as a key in `std::unordered_map`,
 *  `std::unordered_set`, and similar standard containers without wrapping
 *  in `MPTIssue`.
 */
template <>
struct hash<xrpl::MPTID> : xrpl::MPTID::hasher
{
    explicit hash() = default;
};

}  // namespace std
