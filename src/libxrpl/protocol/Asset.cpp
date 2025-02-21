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

#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

AccountID const&
Asset::getIssuer() const
{
    return std::visit(
        [&](auto&& issue) -> AccountID const& { return issue.getIssuer(); },
        issue_);
}

std::string
Asset::getText() const
{
    return std::visit([&](auto&& issue) { return issue.getText(); }, issue_);
}

void
Asset::setJson(Json::Value& jv) const
{
    std::visit([&](auto&& issue) { issue.setJson(jv); }, issue_);
}

std::string
to_string(Asset const& asset)
{
    return std::visit(
        [&](auto const& issue) { return to_string(issue); }, asset.value());
}

bool
validJSONAsset(Json::Value const& jv)
{
    if (jv.isMember(jss::mpt_issuance_id))
        return !(jv.isMember(jss::currency) || jv.isMember(jss::issuer));
    return jv.isMember(jss::currency);
}

Asset
assetFromJson(Json::Value const& v)
{
    if (!v.isMember(jss::currency) && !v.isMember(jss::mpt_issuance_id))
        Throw<std::runtime_error>(
            "assetFromJson must contain currency or mpt_issuance_id");

    if (v.isMember(jss::currency))
        return issueFromJson(v);
    return mptIssueFromJson(v);
}

Json::Value
to_json(Asset const& asset)
{
    return std::visit(
        [&](auto const& issue) { return to_json(issue); }, asset.value());
}

}  // namespace ripple
