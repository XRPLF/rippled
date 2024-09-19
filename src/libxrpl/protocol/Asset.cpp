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

Asset::Asset(Issue const& issue) : issue_(issue)
{
}

Asset::Asset(MPTIssue const& mpt) : issue_(mpt)
{
}

Asset::Asset(MPTID const& mpt) : issue_(MPTIssue{mpt})
{
}

Asset::operator Issue() const
{
    return get<Issue>();
}

Asset::operator MPTIssue() const
{
    return get<MPTIssue>();
}

AccountID const&
Asset::getIssuer() const
{
    if (holds<Issue>())
        return get<Issue>().getIssuer();
    return get<MPTIssue>().getIssuer();
}

std::string
Asset::getText() const
{
    if (holds<Issue>())
        return get<Issue>().getText();
    return to_string(get<MPTIssue>().getMptID());
}

void
Asset::setJson(Json::Value& jv) const
{
    if (holds<MPTIssue>())
        jv[jss::mpt_issuance_id] = to_string(get<MPTIssue>().getMptID());
    else
    {
        jv[jss::currency] = to_string(get<Issue>().currency);
        if (!isXRP(get<Issue>().currency))
            jv[jss::issuer] = toBase58(get<Issue>().account);
    }
}

std::string
to_string(Asset const& asset)
{
    if (asset.holds<Issue>())
        return to_string(asset.get<Issue>());
    return to_string(asset.get<MPTIssue>().getMptID());
}

bool
validJSONAsset(Json::Value const& jv)
{
    if (jv.isMember(jss::mpt_issuance_id))
        return !(jv.isMember(jss::currency) || jv.isMember(jss::issuer));
    return jv.isMember(jss::currency);
}

}  // namespace ripple