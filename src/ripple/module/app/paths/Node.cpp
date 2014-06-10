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

namespace ripple {
namespace path {

// Compare the non-calculated fields.
bool Node::operator== (const Node& other) const
{
    return other.uFlags == uFlags
       && other.account_ == account_
       && other.currency_ == currency_
       && other.issuer_ == issuer_;
}

// This is for debugging not end users. Output names can be changed without
// warning.
Json::Value Node::getJson () const
{
    Json::Value jvNode (Json::objectValue);
    Json::Value jvFlags (Json::arrayValue);

    jvNode["type"]  = uFlags;

    bool const hasCurrency = (currency_ != zero);
    bool const hasAccount = (account_ != zero);
    bool const hasIssuer = (issuer_ != zero);

    if (isAccount() || hasAccount)
    {
        jvFlags.append (!isAccount() == hasAccount ? "account" : "-account");
    }

    if (uFlags & STPathElement::typeCurrency || hasCurrency)
    {
        jvFlags.append ((uFlags & STPathElement::typeCurrency) && hasCurrency
            ? "currency"
            : "-currency");
    }

    if (uFlags & STPathElement::typeIssuer || hasIssuer)
    {
        jvFlags.append ((uFlags & STPathElement::typeIssuer) && hasIssuer
            ? "issuer"
            : "-issuer");
    }

    jvNode["flags"] = jvFlags;

    if (!!account_)
        jvNode["account"] = RippleAddress::createHumanAccountID (account_);

    if (!!currency_)
        jvNode["currency"] = STAmount::createHumanCurrency (currency_);

    if (!!issuer_)
        jvNode["issuer"] = RippleAddress::createHumanAccountID (issuer_);

    if (saRevRedeem)
        jvNode["rev_redeem"] = saRevRedeem.getFullText ();

    if (saRevIssue)
        jvNode["rev_issue"] = saRevIssue.getFullText ();

    if (saRevDeliver)
        jvNode["rev_deliver"] = saRevDeliver.getFullText ();

    if (saFwdRedeem)
        jvNode["fwd_redeem"] = saFwdRedeem.getFullText ();

    if (saFwdIssue)
        jvNode["fwd_issue"] = saFwdIssue.getFullText ();

    if (saFwdDeliver)
        jvNode["fwd_deliver"] = saFwdDeliver.getFullText ();

    return jvNode;
}

} // path
} // ripple
