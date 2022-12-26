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

#include <ripple/protocol/STIssue.h>

#include <ripple/basics/Log.h>
#include <ripple/basics/contract.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/protocol/jss.h>

#include <boost/algorithm/string.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/regex.hpp>

#include <iostream>
#include <iterator>
#include <memory>

namespace ripple {

STIssue::STIssue(SField const& name) : STBase{name}
{
}

STIssue::STIssue(SerialIter& sit, SField const& name) : STBase{name}
{
    issue_.currency = sit.get160();
    if (!isXRP(issue_.currency))
        issue_.account = sit.get160();
    else
        issue_.account = xrpAccount();

    if (isXRP(issue_.currency) != isXRP(issue_.account))
        Throw<std::runtime_error>(
            "invalid issue: currency and account native mismatch");
}

STIssue::STIssue(SField const& name, Issue const& issue)
    : STBase{name}, issue_{issue}
{
    if (isXRP(issue_.currency) != isXRP(issue_.account))
        Throw<std::runtime_error>(
            "invalid issue: currency and account native mismatch");
}

SerializedTypeID
STIssue::getSType() const
{
    return STI_ISSUE;
}

std::string
STIssue::getText() const
{
    std::string ret;

    ret.reserve(64);
    ret = to_string(issue_.currency);

    if (!isXRP(issue_.currency))
    {
        ret += "/";

        if (isXRP(issue_.account))
            ret += "0";
        else if (issue_.account == noAccount())
            ret += "1";
        else
            ret += to_string(issue_.account);
    }

    return ret;
}

Json::Value STIssue::getJson(JsonOptions) const
{
    return to_json(issue_);
}

void
STIssue::add(Serializer& s) const
{
    s.addBitString(issue_.currency);
    if (!isXRP(issue_.currency))
        s.addBitString(issue_.account);
}

bool
STIssue::isEquivalent(const STBase& t) const
{
    const STIssue* v = dynamic_cast<const STIssue*>(&t);
    return v && (*v == *this);
}

bool
STIssue::isDefault() const
{
    return issue_ == xrpIssue();
}

std::unique_ptr<STIssue>
STIssue::construct(SerialIter& sit, SField const& name)
{
    return std::make_unique<STIssue>(sit, name);
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
    return STIssue{name, issueFromJson(v)};
}

}  // namespace ripple
