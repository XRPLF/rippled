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

#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STMPTAmount.h>
#include <xrpl/protocol/jss.h>

#include <boost/regex.hpp>

namespace ripple {

STMPTAmount::STMPTAmount(SerialIter& sit)
{
    auto const mask = sit.get8();
    assert((mask & cMPToken));
    if (((mask & cMPToken) == 0))
        Throw<std::logic_error>("Not MPT Amount.");

    value_ = sit.get64();
    if ((mask & cPositive) == 0)
        value_ = -value_;
    issue_ = sit.get192();
}

STMPTAmount::STMPTAmount(MPTIssue const& issue, value_type value)
    : MPTAmount(value), issue_(issue)
{
}

STMPTAmount::STMPTAmount(
    MPTIssue const& issue,
    std::uint64_t value,
    bool negative)
    : issue_(issue)
{
    if (value > maxMPTokenAmount)
        Throw<std::logic_error>("MPTAmount is out of range");
    value_ = static_cast<std::int64_t>(value);
    if (negative)
        value_ = -value_;
}

STMPTAmount::STMPTAmount(value_type value) : MPTAmount(value)
{
}

SerializedTypeID
STMPTAmount::getSType() const
{
    return STI_AMOUNT;
}

std::string
STMPTAmount::getFullText() const
{
    std::string ret;

    ret.reserve(64);
    ret = getText() + "/" + to_string(issue_.getMptID());
    return ret;
}

std::string
STMPTAmount::getText() const
{
    return std::to_string(value_);
}

Json::Value STMPTAmount::getJson(JsonOptions) const
{
    Json::Value elem;
    setJson(elem);
    return elem;
}

void
STMPTAmount::setJson(Json::Value& elem) const
{
    elem[jss::mpt_issuance_id] = to_string(issue_.getMptID());
    elem[jss::value] = getText();
}

void
STMPTAmount::add(Serializer& s) const
{
    auto u8 = cMPToken;
    if (value_ >= 0)
        u8 |= cPositive;
    s.add8(u8);
    s.add64(value_ >= 0 ? value_ : -value_);
    s.addBitString(issue_.getMptID());
}

bool
STMPTAmount::isDefault() const
{
    return value_ == 0 && issue_ == noMPT();
}

AccountID
STMPTAmount::getIssuer() const
{
    return issue_.getIssuer();
}

MPTID const&
STMPTAmount::getCurrency() const
{
    return issue_.getMptID();
}

MPTIssue const&
STMPTAmount::issue() const
{
    return issue_;
}

void
STMPTAmount::clear()
{
    value_ = 0;
}

void
STMPTAmount::clear(MPTIssue const& issue)
{
    issue_ = issue;
    value_ = 0;
}

STMPTAmount
STMPTAmount::zeroed() const
{
    return STMPTAmount{issue_};
}

int
STMPTAmount::signum() const noexcept
{
    return MPTAmount::signum();
}

STMPTAmount
amountFromString(MPTIssue const& issue, std::string const& amount)
{
    static boost::regex const reNumber(
        "^"                       // the beginning of the string
        "([+-]?)"                 // (optional) + character
        "(0|[1-9][0-9]*)"         // a number (no leading zeroes, unless 0)
        "(\\.([0-9]+))?"          // (optional) period followed by any number
        "([eE]([+-]?)([0-9]+))?"  // (optional) E, optional + or -, any number
        "$",
        boost::regex_constants::optimize);

    boost::smatch match;

    if (!boost::regex_match(amount, match, reNumber))
        Throw<std::runtime_error>("MPT '" + amount + "' is not valid");

    // Match fields:
    //   0 = whole input
    //   1 = sign
    //   2 = integer portion
    //   3 = whole fraction (with '.')
    //   4 = fraction (without '.')
    //   5 = whole exponent (with 'e')
    //   6 = exponent sign
    //   7 = exponent number

    // CHECKME: Why 32? Shouldn't this be 16?
    if ((match[2].length() + match[4].length()) > 32)
        Throw<std::runtime_error>("Number '" + amount + "' is overlong");

    // Can't specify MPT using fractional representation
    if (match[3].matched)
        Throw<std::runtime_error>("MPT must be specified as integral.");

    std::uint64_t mantissa;
    int exponent;

    bool negative = (match[1].matched && (match[1] == "-"));

    if (!match[4].matched)  // integer only
    {
        mantissa =
            beast::lexicalCastThrow<std::uint64_t>(std::string(match[2]));
        exponent = 0;
    }
    else
    {
        // integer and fraction
        mantissa = beast::lexicalCastThrow<std::uint64_t>(match[2] + match[4]);
        exponent = -(match[4].length());
    }

    if (match[5].matched)
    {
        // we have an exponent
        if (match[6].matched && (match[6] == "-"))
            exponent -= beast::lexicalCastThrow<int>(std::string(match[7]));
        else
            exponent += beast::lexicalCastThrow<int>(std::string(match[7]));
    }

    while (exponent-- > 0)
        mantissa *= 10;

    return {issue, mantissa, negative};
}

}  // namespace ripple