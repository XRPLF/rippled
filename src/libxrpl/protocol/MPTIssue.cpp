//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpl/json/json_errors.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/jss.h>

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

MPTID const&
MPTIssue::getMptID() const
{
    return mptID_;
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
