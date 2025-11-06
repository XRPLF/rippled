#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/json/json_errors.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace ripple {

MPTIssue::MPTIssue(MPTID const& issuanceID) : mptID_(issuanceID)
{
}

AccountID const&
MPTIssue::getIssuer() const
{
    // MPTID is concatenation of sequence + account
    static_assert(sizeof(MPTID) == (sizeof(std::uint32_t) + sizeof(AccountID)));
    // copy from id skipping the sequence
    AccountID const* account = reinterpret_cast<AccountID const*>(
        mptID_.data() + sizeof(std::uint32_t));

    return *account;
}

std::string
MPTIssue::getText() const
{
    return to_string(mptID_);
}

void
MPTIssue::setJson(Json::Value& jv) const
{
    jv[jss::mpt_issuance_id] = to_string(mptID_);
}

Json::Value
to_json(MPTIssue const& mptIssue)
{
    Json::Value jv;
    mptIssue.setJson(jv);
    return jv;
}

std::string
to_string(MPTIssue const& mptIssue)
{
    return to_string(mptIssue.getMptID());
}

MPTIssue
mptIssueFromJson(Json::Value const& v)
{
    if (!v.isObject())
    {
        Throw<std::runtime_error>(
            "mptIssueFromJson can only be specified with an 'object' Json "
            "value");
    }

    if (v.isMember(jss::currency) || v.isMember(jss::issuer))
    {
        Throw<std::runtime_error>(
            "mptIssueFromJson, MPTIssue should not have currency or issuer");
    }

    Json::Value const& idStr = v[jss::mpt_issuance_id];

    if (!idStr.isString())
    {
        Throw<Json::error>(
            "mptIssueFromJson MPTID must be a string Json value");
    }

    MPTID id;
    if (!id.parseHex(idStr.asString()))
    {
        Throw<Json::error>("mptIssueFromJson MPTID is invalid");
    }

    return MPTIssue{id};
}

}  // namespace ripple
