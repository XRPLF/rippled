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

#include <BeastConfig.h>
#include <ripple/protocol/STPathSet.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/strHex.h>
#include <ripple/basics/StringUtilities.h>
#include <cstddef>

namespace ripple {

std::size_t
STPathElement::get_hash (STPathElement const& element)
{
    std::size_t hash_account  = 2654435761;
    std::size_t hash_currency = 2654435761;
    std::size_t hash_issuer   = 2654435761;

    // NIKB NOTE: This doesn't have to be a secure hash as speed is more
    //            important. We don't even really need to fully hash the whole
    //            base_uint here, as a few bytes would do for our use.

    for (auto const x : element.getAccountID ())
        hash_account += (hash_account * 257) ^ x;

    for (auto const x : element.getCurrency ())
        hash_currency += (hash_currency * 509) ^ x;

    for (auto const x : element.getIssuerID ())
        hash_issuer += (hash_issuer * 911) ^ x;

    return (hash_account ^ hash_currency ^ hash_issuer);
}

STPathSet::STPathSet (SerialIter& sit, SField const& name)
    : STBase(name)
{
    std::vector<STPathElement> path;
    for(;;)
    {
        int iType = sit.get8 ();

        if (iType == STPathElement::typeNone ||
            iType == STPathElement::typeBoundary)
        {
            if (path.empty ())
            {
                WriteLog (lsINFO, STBase)
                    << "STPathSet: Empty path.";
                Throw<std::runtime_error> ("empty path");
            }

            push_back (path);
            path.clear ();

            if (iType == STPathElement::typeNone)
                return;
        }
        else if (iType & ~STPathElement::typeAll)
        {
            WriteLog (lsINFO, STBase)
                << "STPathSet: Bad path element: " << iType;

            Throw<std::runtime_error> ("bad path element");
        }
        else
        {
            auto hasAccount = iType & STPathElement::typeAccount;
            auto hasCurrency = iType & STPathElement::typeCurrency;
            auto hasIssuer = iType & STPathElement::typeIssuer;

            AccountID account;
            Currency currency;
            AccountID issuer;

            if (hasAccount)
                account.copyFrom (sit.get160 ());

            if (hasCurrency)
                currency.copyFrom (sit.get160 ());

            if (hasIssuer)
                issuer.copyFrom (sit.get160 ());

            path.emplace_back (account, currency, issuer, hasCurrency);
        }
    }
}

bool
STPathSet::assembleAdd(STPath const& base, STPathElement const& tail)
{ // assemble base+tail and add it to the set if it's not a duplicate
    value.push_back (base);

    std::vector<STPath>::reverse_iterator it = value.rbegin ();

    STPath& newPath = *it;
    newPath.push_back (tail);

    while (++it != value.rend ())
    {
        if (*it == newPath)
        {
            value.pop_back ();
            return false;
        }
    }
    return true;
}

bool
STPathSet::isEquivalent (const STBase& t) const
{
    const STPathSet* v = dynamic_cast<const STPathSet*> (&t);
    return v && (value == v->value);
}

bool
STPath::hasSeen (
    AccountID const& account, Currency const& currency,
    AccountID const& issuer) const
{
    for (auto& p: mPath)
    {
        if (p.getAccountID () == account
            && p.getCurrency () == currency
            && p.getIssuerID () == issuer)
            return true;
    }

    return false;
}

Json::Value
STPath::getJson (int) const
{
    Json::Value ret (Json::arrayValue);

    for (auto it: mPath)
    {
        Json::Value elem (Json::objectValue);
        int         iType   = it.getNodeType ();

        elem[jss::type]      = iType;
        elem[jss::type_hex]  = strHex (iType);

        if (iType & STPathElement::typeAccount)
            elem[jss::account]  = to_string (it.getAccountID ());

        if (iType & STPathElement::typeCurrency)
            elem[jss::currency] = to_string (it.getCurrency ());

        if (iType & STPathElement::typeIssuer)
            elem[jss::issuer]   = to_string (it.getIssuerID ());

        ret.append (elem);
    }

    return ret;
}

Json::Value
STPathSet::getJson (int options) const
{
    Json::Value ret (Json::arrayValue);
    for (auto it: value)
        ret.append (it.getJson (options));

    return ret;
}

void
STPathSet::add (Serializer& s) const
{
    assert (fName->isBinary ());
    assert (fName->fieldType == STI_PATHSET);
    bool first = true;

    for (auto const& spPath : value)
    {
        if (!first)
            s.add8 (STPathElement::typeBoundary);

        for (auto const& speElement : spPath)
        {
            int iType = speElement.getNodeType ();

            s.add8 (iType);

            if (iType & STPathElement::typeAccount)
                s.add160 (speElement.getAccountID ());

            if (iType & STPathElement::typeCurrency)
                s.add160 (speElement.getCurrency ());

            if (iType & STPathElement::typeIssuer)
                s.add160 (speElement.getIssuerID ());
        }

        first = false;
    }

    s.add8 (STPathElement::typeNone);
}

} // ripple
