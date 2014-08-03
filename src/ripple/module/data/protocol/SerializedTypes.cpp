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

const STAmount saZero (noIssue(), 0);
const STAmount saOne (noIssue(), 1);

SerializedType& SerializedType::operator= (const SerializedType& t)
{
    if ((t.fName != fName) && fName->isUseful () && t.fName->isUseful ())
    {
        WriteLog ((t.getSType () == STI_AMOUNT) ? lsTRACE : lsWARNING, SerializedType) // This is common for amounts
                << "Caution: " << t.fName->getName () << " not replacing " << fName->getName ();
    }

    if (!fName->isUseful ()) fName = t.fName;

    return *this;
}

bool SerializedType::isEquivalent (const SerializedType& t) const
{
    assert (getSType () == STI_NOTPRESENT);
    if (t.getSType () == STI_NOTPRESENT)
        return true;
    WriteLog (lsDEBUG, SerializedType) << "notEquiv " << getFullText() << " not STI_NOTPRESENT";
    return false;
}

std::string SerializedType::getFullText () const
{
    std::string ret;

    if (getSType () != STI_NOTPRESENT)
    {
        if (fName->hasName ())
        {
            ret = fName->fieldName;
            ret += " = ";
        }

        ret += getText ();
    }

    return ret;
}

//
// STVariableLength
//

STVariableLength::STVariableLength (SerializerIterator& st, SField::ref name) : SerializedType (name)
{
    value = st.getVL ();
}

std::string STVariableLength::getText () const
{
    return strHex (value);
}

STVariableLength* STVariableLength::construct (SerializerIterator& u, SField::ref name)
{
    return new STVariableLength (name, u.getVL ());
}

bool STVariableLength::isEquivalent (const SerializedType& t) const
{
    const STVariableLength* v = dynamic_cast<const STVariableLength*> (&t);
    return v && (value == v->value);
}

std::string STAccount::getText () const
{
    Account u;
    RippleAddress a;

    if (!getValueH160 (u))
        return STVariableLength::getText ();

    a.setAccountID (u);
    return a.humanAccountID ();
}

STAccount* STAccount::construct (SerializerIterator& u, SField::ref name)
{
    return new STAccount (name, u.getVL ());
}

//
// STVector256
//

// Return a new object from a SerializerIterator.
STVector256* STVector256::construct (SerializerIterator& u, SField::ref name)
{
    Blob data = u.getVL ();
    Blob ::iterator begin = data.begin ();

    std::unique_ptr<STVector256> vec (new STVector256 (name));

    int count = data.size () / (256 / 8);
    vec->mValue.reserve (count);

    unsigned int    uStart  = 0;

    for (unsigned int i = 0; i != count; i++)
    {
        unsigned int    uEnd    = uStart + (256 / 8);

        // This next line could be optimized to construct a default uint256 in the vector and then copy into it
        vec->mValue.push_back (uint256 (Blob (begin + uStart, begin + uEnd)));
        uStart  = uEnd;
    }

    return vec.release ();
}

void STVector256::add (Serializer& s) const
{
    assert (fName->isBinary ());
    assert (fName->fieldType == STI_VECTOR256);
    s.addVL (mValue.empty () ? nullptr : mValue[0].begin (), mValue.size () * (256 / 8));
}

bool STVector256::isEquivalent (const SerializedType& t) const
{
    const STVector256* v = dynamic_cast<const STVector256*> (&t);
    return v && (mValue == v->mValue);
}

bool STVector256::hasValue (uint256 const& v) const
{
    BOOST_FOREACH (uint256 const& hash, mValue)
    {
        if (hash == v)
            return true;
    }

    return false;
}

//
// STAccount
//

STAccount::STAccount (SField::ref n, Account const& v) : STVariableLength (n)
{
    peekValue ().insert (peekValue ().end (), v.begin (), v.end ());
}

bool STAccount::isValueH160 () const
{
    return peekValue ().size () == (160 / 8);
}

RippleAddress STAccount::getValueNCA () const
{
    RippleAddress a;
    Account account;

    if (getValueH160 (account))
        a.setAccountID (account);

    return a;
}

void STAccount::setValueNCA (RippleAddress const& nca)
{
    setValueH160 (nca.getAccountID ());
}

STPathSet* STPathSet::construct (SerializerIterator& s, SField::ref name)
{
    std::vector<STPath> paths;
    std::vector<STPathElement> path;

    do
    {
        int iType   = s.get8 ();

        if (iType == STPathElement::typeNone ||
            iType == STPathElement::typeBoundary)
        {
            if (path.empty ())
            {
                WriteLog (lsINFO, SerializedType) << "STPathSet: Empty path.";

                throw std::runtime_error ("empty path");
            }

            paths.push_back (path);
            path.clear ();

            if (iType == STPathElement::typeNone)
            {
                return new STPathSet (name, paths);
            }
        }
        else if (iType & ~STPathElement::typeAll)
        {
            WriteLog (lsINFO, SerializedType)
                    << "STPathSet: Bad path element: " << iType;

            throw std::runtime_error ("bad path element");
        }
        else
        {
            auto hasAccount = iType & STPathElement::typeAccount;
            auto hasCurrency = iType & STPathElement::typeCurrency;
            auto hasIssuer = iType & STPathElement::typeIssuer;

            Account account;
            Currency currency;
            Account issuer;

            if (hasAccount)
                account.copyFrom (s.get160 ());

            if (hasCurrency)
                currency.copyFrom (s.get160 ());

            if (hasIssuer)
                issuer.copyFrom (s.get160 ());

            path.emplace_back (account, currency, issuer, hasCurrency);
        }
    }
    while (1);
}

bool STPathSet::isEquivalent (const SerializedType& t) const
{
    const STPathSet* v = dynamic_cast<const STPathSet*> (&t);
    return v && (value == v->value);
}

bool STPath::hasSeen (
    Account const& account, Currency const& currency,
    Account const& issuer) const
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

Json::Value STPath::getJson (int) const
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

Json::Value STPathSet::getJson (int options) const
{
    Json::Value ret (Json::arrayValue);
    for (auto it: value)
        ret.append (it.getJson (options));

    return ret;
}

#if 0
std::string STPath::getText () const
{
    std::string ret ("[");
    bool first = true;

    BOOST_FOREACH (const STPathElement & it, mPath)
    {
        if (!first) ret += ", ";

        switch (it.getNodeType ())
        {
        case STPathElement::typeAccount:
        {
            ret += to_string (it.getNode ());
            break;
        }

        case STPathElement::typeOffer:
        {
            ret += "Offer(";
            ret += it.getNode ().GetHex ();
            ret += ")";
            break;
        }

        default:
            throw std::runtime_error ("Unknown path element");
        }

        first = false;
    }

    return ret + "]";
}
#endif

void STPathSet::add (Serializer& s) const
{
    assert (fName->isBinary ());
    assert (fName->fieldType == STI_PATHSET);
    bool bFirst = true;

    BOOST_FOREACH (const STPath & spPath, value)
    {
        if (!bFirst)
        {
            s.add8 (STPathElement::typeBoundary);
        }

        BOOST_FOREACH (const STPathElement & speElement, spPath)
        {
            int     iType   = speElement.getNodeType ();

            s.add8 (iType);

            if (iType & STPathElement::typeAccount)
                s.add160 (speElement.getAccountID ());

            if (iType & STPathElement::typeCurrency)
                s.add160 (speElement.getCurrency ());

            if (iType & STPathElement::typeIssuer)
                s.add160 (speElement.getIssuerID ());
        }

        bFirst = false;
    }
    s.add8 (STPathElement::typeNone);
}

} // ripple
