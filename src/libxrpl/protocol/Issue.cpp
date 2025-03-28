//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_errors.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <ostream>
#include <stdexcept>
#include <string>

namespace ripple {

std::string
Issue::getText() const
{
    std::string ret;

    ret.reserve(64);
    ret = to_string(currency);

    if (!isXRP(currency))
    {
        ret += "/";

        if (isXRP(account))
            ret += "0";
        else if (account == noAccount())
            ret += "1";
        else
            ret += to_string(account);
    }

    return ret;
}

void
Issue::setJson(Json::Value& jv) const
{
    jv[jss::currency] = to_string(currency);
    if (!isXRP(currency))
        jv[jss::issuer] = toBase58(account);
}

bool
Issue::native() const
{
    return *this == xrpIssue();
}

bool
isConsistent(Issue const& ac)
{
    return isXRP(ac.currency) == isXRP(ac.account);
}

std::string
to_string(Issue const& ac)
{
    if (isXRP(ac.account))
        return to_string(ac.currency);

    return to_string(ac.account) + "/" + to_string(ac.currency);
}

Json::Value
to_json(Issue const& is)
{
    Json::Value jv;
    is.setJson(jv);
    return jv;
}

Issue
issueFromJson(Json::Value const& v)
{
    if (!v.isObject())
    {
        Throw<std::runtime_error>(
            "issueFromJson can only be specified with an 'object' Json value");
    }

    if (v.isMember(jss::mpt_issuance_id))
    {
        Throw<std::runtime_error>(
            "issueFromJson, Issue should not have mpt_issuance_id");
    }

    Json::Value const curStr = v[jss::currency];
    Json::Value const issStr = v[jss::issuer];

    if (!curStr.isString())
    {
        Throw<Json::error>(
            "issueFromJson currency must be a string Json value");
    }

    auto const currency = to_currency(curStr.asString());
    if (currency == badCurrency() || currency == noCurrency())
    {
        Throw<Json::error>("issueFromJson currency must be a valid currency");
    }

    if (isXRP(currency))
    {
        if (!issStr.isNull())
        {
            Throw<Json::error>("Issue, XRP should not have issuer");
        }
        return xrpIssue();
    }

    if (!issStr.isString())
    {
        Throw<Json::error>("issueFromJson issuer must be a string Json value");
    }
    auto const issuer = parseBase58<AccountID>(issStr.asString());

    if (!issuer)
    {
        Throw<Json::error>("issueFromJson issuer must be a valid account");
    }

    return Issue{currency, *issuer};
}

std::ostream&
operator<<(std::ostream& os, Issue const& x)
{
    os << to_string(x);
    return os;
}

}  // namespace ripple
