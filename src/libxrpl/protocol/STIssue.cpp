//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <xrpl/protocol/STIssue.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/jss.h>

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
            uint192 mptID;
            std::uint32_t sequence = sit.get32();
            memcpy(mptID.data(), &sequence, sizeof(sequence));
            memcpy(
                mptID.data() + sizeof(sequence),
                account.data(),
                sizeof(account));
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

Json::Value STIssue::getJson(JsonOptions) const
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
        s.addBitString(asset_.get<Issue>().currency);
        if (!isXRP(asset_.get<Issue>().currency))
            s.addBitString(asset_.get<Issue>().account);
    }
    else
    {
        s.addBitString(asset_.get<MPTIssue>().getIssuer());
        s.addBitString(noAccount());
        std::uint32_t sequence;
        memcpy(
            &sequence,
            asset_.get<MPTIssue>().getMptID().data(),
            sizeof(sequence));
        s.add32(sequence);
    }
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
    return STIssue{name, issueFromJson(v)};
}

}  // namespace ripple
