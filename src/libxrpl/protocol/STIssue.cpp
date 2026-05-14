#include <xrpl/protocol/STIssue.h>

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rules.h>
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

STIssue::STIssue(SField const& name) : STBase{name}
{
}

STIssue::STIssue(SerialIter& sit, SField const& name) : STBase{name}
{
    auto const currencyOrAccount = sit.get160();

    if (isXRP(Currency::fromRaw(currencyOrAccount)))
    {
        asset_ = xrpIssue();
        return;
    }

    // The next 160-bit field selects the format:
    //   noAccount  → MPT V1 (pre-fixCleanup3_2_0)
    //   xrpAccount → MPT V2 (fixCleanup3_2_0)
    //   else       → regular IOU; the field is the issuer account.
    //
    // Both MPT versions carry the 4-byte sequence next, but differ
    // in byte order:
    //
    //   V1 uses add32()/get32(), which swap host↔BE. The source
    //   bytes (first 4 of MPTID) are already canonical BE per
    //   makeMptID(), so on LE hosts the swap emits a byte-reversed
    //   sequence to the wire. Reads invert the same swap, so
    //   round-trips on a single arch are consistent.
    //
    //   V2 uses addRaw()/getRaw(): the canonical BE bytes from
    //   makeMptID() reach the wire untouched.
    AccountID const account = AccountID::fromRaw(sit.get160());
    if (account == noAccount() || account == xrpAccount())
    {
        MPTID mptID{};
        auto constexpr kSEQ_SIZE = sizeof(std::uint32_t);
        if (account == noAccount())
        {
            std::uint32_t sequence = sit.get32();
            memcpy(mptID.data(), &sequence, sizeof(sequence));
        }
        else
        {
            auto const rawBytes = sit.getRaw(kSEQ_SIZE);
            memcpy(mptID.data(), rawBytes.data(), rawBytes.size());
        }
        static_assert(MPTID::size() == kSEQ_SIZE + sizeof(currencyOrAccount));
        memcpy(mptID.data() + kSEQ_SIZE, currencyOrAccount.data(), sizeof(currencyOrAccount));
        MPTIssue const issue{mptID};
        asset_ = issue;
        return;
    }

    Issue issue;
    issue.currency = currencyOrAccount;
    issue.account = account;
    if (!isConsistent(issue))
        Throw<std::runtime_error>("invalid issue: currency and account native mismatch");
    asset_ = issue;
}

SerializedTypeID
STIssue::getSType() const
{
    return STI_ISSUE;
}

std::string
STIssue::getText() const
{
    return asset_.getText();
}

json::Value
STIssue::getJson(JsonOptions) const
{
    json::Value jv;
    asset_.setJson(jv);
    return jv;
}

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
            auto const fixSerializationEnabled = isFeatureEnabled(fixCleanup3_2_0, false);
            s.addBitString(issue.getIssuer());
            // The sentinel distinguishes V2 (xrpAccount) from V1 (noAccount)
            // during deserialization; see the constructor for the full format
            // description.
            s.addBitString(fixSerializationEnabled ? xrpAccount() : noAccount());
            if (fixSerializationEnabled)
            {
                // memcpy preserves the byte pattern exactly, so for V2, addRaw()
                // emits the same canonical bytes that were in the MPTID.
                s.addRaw(issue.getMptID().data(), sizeof(std::uint32_t));
            }
            else
            {
                // Copy the first 4 bytes of the MPTID (the canonical BE sequence)
                // into a uint32_t so we can pass either to add32() or addRaw().
                // For V1, add32() applies a native-to-BE swap on top of what is
                // already a BE-in-memory value, producing LE wire bytes on LE hosts.
                std::uint32_t sequence = 0;
                memcpy(&sequence, issue.getMptID().data(), sizeof(sequence));
                s.add32(sequence);
            }
        });
}

bool
STIssue::isEquivalent(STBase const& t) const
{
    STIssue const* v = dynamic_cast<STIssue const*>(&t);
    return (v != nullptr) && (*v == *this);
}

bool
STIssue::isDefault() const
{
    return asset_.visit(
        [](Issue const& issue) { return issue == xrpIssue(); },
        [](MPTIssue const&) { return false; });
}

STBase*
STIssue::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STIssue::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

STIssue
issueFromJson(SField const& name, json::Value const& v)
{
    return STIssue{name, assetFromJson(v)};
}

}  // namespace xrpl
