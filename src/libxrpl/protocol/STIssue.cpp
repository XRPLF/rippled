/** @file
 *  Implements STIssue — the serialized-type wrapper that allows an Asset
 *  (XRP, IOU, or MPT issuance) to be stored as a named field in an STObject.
 *
 *  The wire format multiplexes all three asset flavors through a fixed-width
 *  layout: a 160-bit currency/issuer slot, an optional 160-bit account slot,
 *  and an optional 32-bit sequence for MPT. The `noAccount()` sentinel in the
 *  second slot discriminates IOU from MPT without a separate type prefix byte.
 */

#include <xrpl/protocol/STIssue.h>

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

/** Construct an STIssue with the given field name and the default asset (XRP). */
STIssue::STIssue(SField const& name) : STBase{name}
{
}

/** Deserialize an STIssue from a byte stream, detecting XRP, IOU, or MPT by
 *  inspecting the wire layout.
 *
 *  The encoding is a compact, type-multiplexed fixed-width layout:
 *  - **XRP**: a single 160-bit all-zeros currency sentinel; no further bytes.
 *  - **IOU**: 160-bit currency, then 160-bit issuer AccountID.
 *  - **MPT**: 160-bit issuer AccountID (in the currency slot), then the 160-bit
 *      `noAccount()` sentinel to signal MPT, then a 32-bit sequence number.
 *
 *  For MPT, the MPTID buffer (192 bits) is assembled with the sequence at
 *  offset 0 and the raw issuer bytes at offset 4, reversing the on-wire order.
 *
 *  @param sit  Forward cursor over the serialized byte buffer; advanced in place.
 *  @param name The SField that identifies this field within its parent STObject.
 *  @throws std::runtime_error if an IOU carries a native currency paired with a
 *      non-null issuer, or a non-native currency paired with the null issuer.
 *
 *  @note `isConsistent()` is checked only for the IOU branch; MPT is inherently
 *      consistent by construction. The static_assert on `MPTID::size()` guards
 *      the memcpy layout assumption at compile time.
 */
STIssue::STIssue(SerialIter& sit, SField const& name) : STBase{name}
{
    auto const currencyOrAccount = sit.get160();

    if (isXRP(Currency::fromRaw(currencyOrAccount)))
    {
        asset_ = xrpIssue();
    }
    else
    {
        // MPT wire layout: 160-bit issuer | 160-bit noAccount() sentinel | 32-bit sequence
        AccountID const account = AccountID::fromRaw(sit.get160());
        if (noAccount() == account)
        {
            MPTID mptID;
            std::uint32_t sequence = sit.get32();
            static_assert(MPTID::size() == sizeof(sequence) + sizeof(currencyOrAccount));
            // MPTID layout: sequence (4 bytes) || issuer (20 bytes); wire order is reversed.
            memcpy(mptID.data(), &sequence, sizeof(sequence));
            memcpy(
                mptID.data() + sizeof(sequence),
                currencyOrAccount.data(),
                sizeof(currencyOrAccount));
            MPTIssue const issue{mptID};
            asset_ = issue;
        }
        else
        {
            Issue issue;
            issue.currency = currencyOrAccount;
            issue.account = account;
            if (!isConsistent(issue))
                Throw<std::runtime_error>("invalid issue: currency and account native mismatch");
            asset_ = issue;
        }
    }
}

/** @return The serialized type identifier `STI_ISSUE` for generic field dispatch. */
SerializedTypeID
STIssue::getSType() const
{
    return STI_ISSUE;
}

/** @return A human-readable representation of the asset (e.g., "XRP",
 *      "USD/r...", or the raw hex of an MPTID), forwarded from `Asset::getText()`.
 */
std::string
STIssue::getText() const
{
    return asset_.getText();
}

/** Serialize the asset to a JSON value suitable for RPC responses.
 *
 *  Delegates to `Asset::setJson()`, which formats the asset according to its
 *  active variant (XRP, IOU, or MPT).
 *
 *  @return A `json::Value` representing the asset.
 */
json::Value
STIssue::getJson(JsonOptions) const
{
    json::Value jv;
    asset_.setJson(jv);
    return jv;
}

/** Serialize the asset into a Serializer, inverting the deserializing constructor.
 *
 *  - **XRP**: writes only the 160-bit zero currency sentinel (no account).
 *  - **IOU**: writes the 160-bit currency followed by the 160-bit issuer AccountID.
 *  - **MPT**: writes the 160-bit issuer AccountID (in the currency slot), then the
 *      160-bit `noAccount()` sentinel, then the 32-bit sequence extracted from the
 *      front of the MPTID blob — reversing the MPTID memory layout back to wire order.
 *
 *  @param s The Serializer to append bytes to.
 */
void
STIssue::add(Serializer& s) const
{
    asset_.visit(
        [&](Issue const& issue) {
            s.addBitString(issue.currency);
            if (!isXRP(issue.currency))
                s.addBitString(issue.account);
        },
        [&](MPTIssue const& issue) {
            s.addBitString(issue.getIssuer());
            s.addBitString(noAccount());
            std::uint32_t sequence = 0;
            memcpy(&sequence, issue.getMptID().data(), sizeof(sequence));
            s.add32(sequence);
        });
}

/** Return true if `t` is an STIssue holding the same asset as this object.
 *
 *  @param t The STBase to compare against; must be safely downcasted to STIssue.
 *  @return `true` if `t` is an STIssue and its asset compares equal to this one.
 */
bool
STIssue::isEquivalent(STBase const& t) const
{
    STIssue const* v = dynamic_cast<STIssue const*>(&t);
    return (v != nullptr) && (*v == *this);
}

/** Return true if this field holds the default asset (XRP).
 *
 *  Matches the member initializer `asset_{xrpIssue()}`. An STIssue field
 *  absent from a ledger object is implicitly XRP, consistent with the
 *  ledger's treatment of the native currency as the base case. MPT issuances
 *  are never considered default.
 *
 *  @return `true` iff the held asset is `xrpIssue()`.
 */
bool
STIssue::isDefault() const
{
    return asset_.visit(
        [](Issue const& issue) { return issue == xrpIssue(); },
        [](MPTIssue const&) { return false; });
}

/** Placement-copy this STIssue into a caller-supplied buffer for STVar storage.
 *
 *  Used by `detail::STVar` to store heterogeneous STBase subclasses inside
 *  STObject without a heap allocation per field (small-object optimization).
 *
 *  @param n   Size of the destination buffer in bytes.
 *  @param buf Pointer to the destination buffer.
 *  @return Pointer to the newly constructed object within `buf`.
 */
STBase*
STIssue::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

/** Placement-move this STIssue into a caller-supplied buffer for STVar storage.
 *
 *  @param n   Size of the destination buffer in bytes.
 *  @param buf Pointer to the destination buffer.
 *  @return Pointer to the newly constructed object within `buf`.
 */
STBase*
STIssue::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

/** Construct an STIssue from a JSON value representing an asset.
 *
 *  Delegates parsing to `assetFromJson()`, which resolves the JSON
 *  representation to the appropriate `Asset` variant. Consistency validation
 *  (native currency vs. null issuer) is assumed to have been enforced upstream
 *  by the JSON parsing layer rather than re-checked here.
 *
 *  @param name The SField to attach to the resulting STIssue.
 *  @param v    A JSON value encoding an asset (XRP object, IOU object, or MPT object).
 *  @return An STIssue bound to `name` holding the parsed asset.
 */
STIssue
issueFromJson(SField const& name, json::Value const& v)
{
    return STIssue{name, assetFromJson(v)};
}

}  // namespace xrpl
