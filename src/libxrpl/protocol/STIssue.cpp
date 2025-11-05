#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace ripple {

STIssue::STIssue(SField const& name) : STBase{name}
{
}

STIssue::STIssue(SerialIter& sit, SField const& name) : STBase{name}
{
    auto const currencyOrAccount = sit.get160();

    if (isXRP(static_cast<Currency>(currencyOrAccount)))
    {
        asset_ = xrpIssue();
    }
    // Check if MPT
    else
    {
        // MPT is serialized as:
        // - 160 bits MPT issuer account
        // - 160 bits black hole account
        // - 32 bits sequence
        AccountID account = static_cast<AccountID>(sit.get160());
        // MPT
        if (noAccount() == account)
        {
            MPTID mptID;
            std::uint32_t sequence = sit.get32();
            static_assert(
                MPTID::size() == sizeof(sequence) + sizeof(currencyOrAccount));
            memcpy(mptID.data(), &sequence, sizeof(sequence));
            memcpy(
                mptID.data() + sizeof(sequence),
                currencyOrAccount.data(),
                sizeof(currencyOrAccount));
            MPTIssue issue{mptID};
            asset_ = issue;
        }
        else
        {
            Issue issue;
            issue.currency = currencyOrAccount;
            issue.account = account;
            if (!isConsistent(issue))
                Throw<std::runtime_error>(
                    "invalid issue: currency and account native mismatch");
            asset_ = issue;
        }
    }
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

Json::Value
STIssue::getJson(JsonOptions) const
{
    Json::Value jv;
    asset_.setJson(jv);
    return jv;
}

void
STIssue::add(Serializer& s) const
{
    if (holds<Issue>())
    {
        auto const& issue = asset_.get<Issue>();
        s.addBitString(issue.currency);
        if (!isXRP(issue.currency))
            s.addBitString(issue.account);
    }
    else
    {
        auto const& issue = asset_.get<MPTIssue>();
        s.addBitString(issue.getIssuer());
        s.addBitString(noAccount());
        std::uint32_t sequence;
        memcpy(&sequence, issue.getMptID().data(), sizeof(sequence));
        s.add32(sequence);
    }
}

bool
STIssue::isEquivalent(STBase const& t) const
{
    STIssue const* v = dynamic_cast<STIssue const*>(&t);
    return v && (*v == *this);
}

bool
STIssue::isDefault() const
{
    return holds<Issue>() && asset_.get<Issue>() == xrpIssue();
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
issueFromJson(SField const& name, Json::Value const& v)
{
    return STIssue{name, assetFromJson(v)};
}

}  // namespace ripple
