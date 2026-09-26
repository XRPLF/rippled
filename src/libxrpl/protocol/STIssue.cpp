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

#include <boost/endian/conversion.hpp>

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
    // Either the currency of an Issue or the issuer of an MPT; the
    // following 160 bits disambiguate.
    auto const currencyOrAccount = sit.get160();

    if (isXRP(Currency{currencyOrAccount}))
    {
        asset_ = xrpIssue();
        return;
    }
    
    // MPT is serialized as:
    // - 160 bits MPT issuer account
    // - 160 bits black hole account
    // - 32 bits sequence
    AccountID const account{sit.get160()};

    // MPT
    if (noAccount() == account)
    {
        // MPT: issuer (160), noAccount() (160), sequence (32). The sequence
        // is stored in little-endian and get32() reads big-endian, so 
        // reverse it before building the canonical ID.
        asset_ = MPTIssue{std::byteswap(sit.get32()), AccountID{currencyOrAccount}};
        return;
    }

    Issue const issue{Currency{currencyOrAccount}, account};
    
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
            s.addBitString(issue.getIssuer());
            s.addBitString(noAccount());
            std::uint32_t sequence = 0;
            memcpy(&sequence, issue.getMptID().data(), sizeof(sequence));
            // The MPTID bytes are canonical big-endian. Interpret those bytes
            // as the legacy LE-host value so add32() writes the preserved
            // STIssue wire bytes on every host endian.
            sequence = boost::endian::little_to_native(sequence);
            s.add32(sequence);
        });
}

bool
STIssue::isEquivalent(STBase const& t) const
{
    auto const* v = dynamic_cast<STIssue const*>(&t);
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
