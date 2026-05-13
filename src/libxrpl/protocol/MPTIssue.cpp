/** @file
 *  Implements MPTIssue: the identity type for Multi-Purpose Token issuances.
 *
 *  MPTIssue adapts a raw 192-bit MPTID (sequence ‖ AccountID) to provide the
 *  same interface as Issue, enabling participation in the Asset variant and
 *  static-polymorphism patterns throughout the ledger engine without rewriting
 *  amount-handling code.
 */

#include <xrpl/protocol/MPTIssue.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/json/json_errors.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>

namespace xrpl {

/** Construct from a pre-formed 192-bit issuance identifier.
 *
 *  @param issuanceID The packed MPTID (32-bit sequence ‖ 160-bit AccountID).
 */
MPTIssue::MPTIssue(MPTID const& issuanceID) : mptID_(issuanceID)
{
}

/** Construct from the raw components of an MPTID.
 *
 *  Delegates to `xrpl::makeMptID(sequence, account)` to assemble the packed
 *  MPTID, saving callers from invoking that helper explicitly.
 *
 *  @param sequence The issuer's account sequence number at the time of
 *      issuance.
 *  @param account  The AccountID of the issuer.
 */
MPTIssue::MPTIssue(std::uint32_t sequence, AccountID const& account)
    : MPTIssue(xrpl::makeMptID(sequence, account))
{
}

/** Extract the issuer's AccountID from the packed MPTID.
 *
 *  MPTID encodes sequence (4 bytes) followed immediately by AccountID (20
 *  bytes). This method recovers the AccountID by pointer-casting past the
 *  leading 4 bytes. A `static_assert` on the total size of MPTID guards
 *  against padding being silently introduced between the two fields; if the
 *  layout ever changes the build fails rather than reading the wrong bytes.
 *
 *  @return A reference to the AccountID embedded in the MPTID. The reference
 *      is valid for the lifetime of this MPTIssue object.
 *  @note The free function `getMPTIssuer()` in the header achieves the same
 *      extraction via `std::copy_n` + `std::bit_cast`, which is constexpr-
 *      eligible. This member uses `reinterpret_cast` for a direct reference
 *      return but is equally correct given the same `static_assert` guard.
 */
AccountID const&
MPTIssue::getIssuer() const
{
    static_assert(sizeof(MPTID) == (sizeof(std::uint32_t) + sizeof(AccountID)));
    AccountID const* account =
        reinterpret_cast<AccountID const*>(mptID_.data() + sizeof(std::uint32_t));

    return *account;
}

/** Return the hex string representation of the underlying MPTID.
 *
 *  @return Uppercase hex encoding of the 192-bit issuance identifier.
 */
std::string
MPTIssue::getText() const
{
    return to_string(mptID_);
}

/** Write the issuance identifier into a JSON object under the key
 *  `mpt_issuance_id`.
 *
 *  Serializes the MPTID as a hex string. Intended for merging into a larger
 *  JSON structure; use `toJson()` when a standalone object is needed.
 *
 *  @param jv The JSON object to write into; must be of object type.
 */
void
MPTIssue::setJson(json::Value& jv) const
{
    jv[jss::mpt_issuance_id] = to_string(mptID_);
}

/** Produce a JSON object representing this MPT issuance.
 *
 *  Constructs a fresh `Json::Value` object and populates it via
 *  `MPTIssue::setJson()`, yielding `{"mpt_issuance_id": "<hex>"}`.
 *
 *  @param mptIssue The issuance to serialize.
 *  @return A JSON object with a single `mpt_issuance_id` field.
 */
json::Value
toJson(MPTIssue const& mptIssue)
{
    json::Value jv;
    mptIssue.setJson(jv);
    return jv;
}

/** Return the hex string representation of an MPT issuance.
 *
 *  @param mptIssue The issuance to convert.
 *  @return Uppercase hex encoding of the underlying 192-bit MPTID.
 */
std::string
to_string(MPTIssue const& mptIssue)
{
    return to_string(mptIssue.getMptID());
}

/** Parse an MPT issuance from a JSON object.
 *
 *  Expects a JSON object with exactly a `mpt_issuance_id` string field
 *  containing a valid 192-bit hex-encoded MPTID. The presence of `currency`
 *  or `issuer` fields is explicitly rejected to guard against callers
 *  accidentally passing an IOU-style amount object.
 *
 *  Two exception types are used deliberately: `std::runtime_error` covers
 *  structural misuse (wrong JSON type, forbidden fields), while `json::Error`
 *  is reserved for field-level format failures (wrong value type, invalid
 *  hex). Callers that need to distinguish protocol misuse from parse errors
 *  can catch at the appropriate level.
 *
 *  @param v The JSON value to parse; must be an object.
 *  @return The parsed MPTIssue.
 *  @throws std::runtime_error if `v` is not a JSON object, or if `currency`
 *      or `issuer` fields are present.
 *  @throws json::Error if `mpt_issuance_id` is absent, not a string, or not
 *      a valid 192-bit hex value.
 */
MPTIssue
mptIssueFromJson(json::Value const& v)
{
    if (!v.isObject())
    {
        Throw<std::runtime_error>(
            "mptIssueFromJson can only be specified with an 'object' Json "
            "value");
    }

    if (v.isMember(jss::currency) || v.isMember(jss::issuer))
    {
        Throw<std::runtime_error>("mptIssueFromJson, MPTIssue should not have currency or issuer");
    }

    json::Value const& idStr = v[jss::mpt_issuance_id];

    if (!idStr.isString())
    {
        Throw<json::Error>("mptIssueFromJson MPTID must be a string Json value");
    }

    MPTID id;
    if (!id.parseHex(idStr.asString()))
    {
        Throw<json::Error>("mptIssueFromJson MPTID is invalid");
    }

    return MPTIssue{id};
}

/** Stream the hex representation of an MPT issuance.
 *
 *  @param os The output stream.
 *  @param x  The issuance to write.
 *  @return `os`, to allow chaining.
 */
std::ostream&
operator<<(std::ostream& os, MPTIssue const& x)
{
    os << to_string(x);
    return os;
}

}  // namespace xrpl
