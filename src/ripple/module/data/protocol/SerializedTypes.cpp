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

SETUP_LOG (SerializedType)

const STAmount saZero (CURRENCY_ONE, ACCOUNT_ONE, 0);
const STAmount saOne (CURRENCY_ONE, ACCOUNT_ONE, 1);

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

void STPathSet::printDebug ()
{
    // VFALCO NOTE Can't use Log::out() because of std::endl
    //
    for (int i = 0; i < value.size (); i++)
    {
        std::cerr << i << ": ";

        for (int j = 0; j < value[i].mPath.size (); j++)
        {
            //STPathElement pe = value[i].mPath[j];
            RippleAddress nad;
            nad.setAccountID (value[i].mPath[j].mAccountID);
            std::cerr << "    " << nad.humanAccountID ();
            //std::cerr << "    " << pe.mAccountID.GetHex();
        }

        std::cerr << std::endl;
    }

}

void STPath::printDebug ()
{
    Log::out() << "STPath:";

    for (int i = 0; i < mPath.size (); i++)
    {
        RippleAddress nad;
        nad.setAccountID (mPath[i].mAccountID);
        Log::out() << "   " << i << ": " << nad.humanAccountID ();
    }
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

STUInt8* STUInt8::construct (SerializerIterator& u, SField::ref name)
{
    return new STUInt8 (name, u.get8 ());
}

std::string STUInt8::getText () const
{
    if (getFName () == sfTransactionResult)
    {
        std::string token, human;

        if (transResultInfo (static_cast<TER> (value), token, human))
            return human;
    }

    return beast::lexicalCastThrow <std::string> (value);
}

Json::Value STUInt8::getJson (int) const
{
    if (getFName () == sfTransactionResult)
    {
        std::string token, human;

        if (transResultInfo (static_cast<TER> (value), token, human))
            return token;
        else
            WriteLog (lsWARNING, SerializedType) << "Unknown result code in metadata: " << value;
    }

    return value;
}

bool STUInt8::isEquivalent (const SerializedType& t) const
{
    const STUInt8* v = dynamic_cast<const STUInt8*> (&t);
    return v && (value == v->value);
}

STUInt16* STUInt16::construct (SerializerIterator& u, SField::ref name)
{
    return new STUInt16 (name, u.get16 ());
}

std::string STUInt16::getText () const
{
    if (getFName () == sfLedgerEntryType)
    {
        LedgerFormats::Item const* const item =
            LedgerFormats::getInstance ()->findByType (static_cast <LedgerEntryType> (value));

        if (item != nullptr)
            return item->getName ();
    }

    if (getFName () == sfTransactionType)
    {
        TxFormats::Item const* const item =
            TxFormats::getInstance()->findByType (static_cast <TxType> (value));

        if (item != nullptr)
            return item->getName ();
    }

    return beast::lexicalCastThrow <std::string> (value);
}

Json::Value STUInt16::getJson (int) const
{
    if (getFName () == sfLedgerEntryType)
    {
        LedgerFormats::Item const* const item =
            LedgerFormats::getInstance ()->findByType (static_cast <LedgerEntryType> (value));

        if (item != nullptr)
            return item->getName ();
    }

    if (getFName () == sfTransactionType)
    {
        TxFormats::Item const* const item =
            TxFormats::getInstance()->findByType (static_cast <TxType> (value));

        if (item != nullptr)
            return item->getName ();
    }

    return value;
}

bool STUInt16::isEquivalent (const SerializedType& t) const
{
    const STUInt16* v = dynamic_cast<const STUInt16*> (&t);
    return v && (value == v->value);
}

STUInt32* STUInt32::construct (SerializerIterator& u, SField::ref name)
{
    return new STUInt32 (name, u.get32 ());
}

std::string STUInt32::getText () const
{
    return beast::lexicalCastThrow <std::string> (value);
}

Json::Value STUInt32::getJson (int) const
{
    return value;
}

bool STUInt32::isEquivalent (const SerializedType& t) const
{
    const STUInt32* v = dynamic_cast<const STUInt32*> (&t);
    return v && (value == v->value);
}

STUInt64* STUInt64::construct (SerializerIterator& u, SField::ref name)
{
    return new STUInt64 (name, u.get64 ());
}

std::string STUInt64::getText () const
{
    return beast::lexicalCastThrow <std::string> (value);
}

Json::Value STUInt64::getJson (int) const
{
    return strHex (value);
}

bool STUInt64::isEquivalent (const SerializedType& t) const
{
    const STUInt64* v = dynamic_cast<const STUInt64*> (&t);
    return v && (value == v->value);
}

STHash128* STHash128::construct (SerializerIterator& u, SField::ref name)
{
    return new STHash128 (name, u.get128 ());
}

std::string STHash128::getText () const
{
    return to_string (value);
}

bool STHash128::isEquivalent (const SerializedType& t) const
{
    const STHash128* v = dynamic_cast<const STHash128*> (&t);
    return v && (value == v->value);
}

STHash160* STHash160::construct (SerializerIterator& u, SField::ref name)
{
    return new STHash160 (name, u.get160 ());
}

std::string STHash160::getText () const
{
    return to_string (value);
}

bool STHash160::isEquivalent (const SerializedType& t) const
{
    const STHash160* v = dynamic_cast<const STHash160*> (&t);
    return v && (value == v->value);
}

STHash256* STHash256::construct (SerializerIterator& u, SField::ref name)
{
    return new STHash256 (name, u.get256 ());
}

std::string STHash256::getText () const
{
    return to_string (value);
}

bool STHash256::isEquivalent (const SerializedType& t) const
{
    const STHash256* v = dynamic_cast<const STHash256*> (&t);
    return v && (value == v->value);
}

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
    uint160 u;
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
    s.addVL (mValue.empty () ? nullptr : mValue[0].begin (), mValue.size () * (256 / 8));
}

bool STVector256::isEquivalent (const SerializedType& t) const
{
    const STVector256* v = dynamic_cast<const STVector256*> (&t);
    return v && (mValue == v->mValue);
}

bool STVector256::hasValue (uint256 const& v) const
{
    BOOST_FOREACH (uint256 const & hash, mValue)
    {
        if (hash == v)
            return true;
    }

    return false;
}

//
// STAccount
//

STAccount::STAccount (SField::ref n, const uint160& v) : STVariableLength (n)
{
    peekValue ().insert (peekValue ().end (), v.begin (), v.end ());
}

bool STAccount::isValueH160 () const
{
    return peekValue ().size () == (160 / 8);
}

void STAccount::setValueH160 (const uint160& v)
{
    peekValue ().clear ();
    peekValue ().insert (peekValue ().end (), v.begin (), v.end ());
    assert (peekValue ().size () == (160 / 8));
}

bool STAccount::getValueH160 (uint160& v) const
{
    if (!isValueH160 ()) return false;

    memcpy (v.begin (), & (peekValue ().front ()), (160 / 8));
    return true;
}

RippleAddress STAccount::getValueNCA () const
{
    RippleAddress a;
    uint160 v;

    if (getValueH160 (v))
        a.setAccountID (v);

    return a;
}

void STAccount::setValueNCA (const RippleAddress& nca)
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

        if (iType == STPathElement::typeNone || iType == STPathElement::typeBoundary)
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
            WriteLog (lsINFO, SerializedType) << "STPathSet: Bad path element: " << iType;

            throw std::runtime_error ("bad path element");
        }
        else
        {
            const bool  bAccount    = !! (iType & STPathElement::typeAccount);
            const bool  bCurrency   = !! (iType & STPathElement::typeCurrency);
            const bool  bIssuer     = !! (iType & STPathElement::typeIssuer);

            uint160 account;
            uint160 uCurrency;
            uint160 issuer;

            if (bAccount)
                account  = s.get160 ();

            if (bCurrency)
                uCurrency   = s.get160 ();

            if (bIssuer)
                issuer   = s.get160 ();

            path.push_back (STPathElement (account, uCurrency, issuer, bCurrency));
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
    const uint160& uAccountId, const uint160& currency,
    const uint160& issuer) const
{
    for (int i = 0; i < mPath.size (); ++i)
    {
        const STPathElement& ele = getElement (i);

        if (ele.getAccountID () == uAccountId
                && ele.getCurrency () == currency
                && ele.getIssuerID () == issuer)
            return true;
    }

    return false;
}

Json::Value STPath::getJson (int) const
{
    Json::Value ret (Json::arrayValue);

    BOOST_FOREACH (std::vector<STPathElement>::const_iterator::value_type it, mPath)
    {
        Json::Value elem (Json::objectValue);
        int         iType   = it.getNodeType ();

        elem[jss::type]      = iType;
        elem[jss::type_hex]  = strHex (iType);

        if (iType & STPathElement::typeAccount)
            elem[jss::account]  = RippleAddress::createHumanAccountID (it.getAccountID ());

        if (iType & STPathElement::typeCurrency)
            elem[jss::currency] = STAmount::createHumanCurrency (it.getCurrency ());

        if (iType & STPathElement::typeIssuer)
            elem[jss::issuer]   = RippleAddress::createHumanAccountID (it.getIssuerID ());

        ret.append (elem);
    }

    return ret;
}

Json::Value STPathSet::getJson (int options) const
{
    Json::Value ret (Json::arrayValue);

    BOOST_FOREACH (std::vector<STPath>::const_iterator::value_type it, value)
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
            ret += RippleAddress::createHumanAccountID (it.getNode ());
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

#if 0
std::string STPathSet::getText () const
{
    std::string ret ("{");
    bool firstPath = true;

    BOOST_FOREACH (std::vector<STPath>::const_iterator::value_type it, value)
    {
        if (!firstPath)
        {
            ret += ", ";
            firstPath = false;
        }

        ret += it.getText ();
    }
    return ret + "}";
}
#endif

void STPathSet::add (Serializer& s) const
{
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
