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

SETUP_LOG (STObject)

std::unique_ptr<SerializedType> STObject::makeDefaultObject (SerializedTypeID id, SField::ref name)
{
    assert ((id == STI_NOTPRESENT) || (id == name.fieldType));

    switch (id)
    {
    case STI_NOTPRESENT:
        return std::unique_ptr<SerializedType> (new SerializedType (name));

    case STI_UINT8:
        return std::unique_ptr<SerializedType> (new STUInt8 (name));

    case STI_UINT16:
        return std::unique_ptr<SerializedType> (new STUInt16 (name));

    case STI_UINT32:
        return std::unique_ptr<SerializedType> (new STUInt32 (name));

    case STI_UINT64:
        return std::unique_ptr<SerializedType> (new STUInt64 (name));

    case STI_AMOUNT:
        return std::unique_ptr<SerializedType> (new STAmount (name));

    case STI_HASH128:
        return std::unique_ptr<SerializedType> (new STHash128 (name));

    case STI_HASH160:
        return std::unique_ptr<SerializedType> (new STHash160 (name));

    case STI_HASH256:
        return std::unique_ptr<SerializedType> (new STHash256 (name));

    case STI_VECTOR256:
        return std::unique_ptr<SerializedType> (new STVector256 (name));

    case STI_VL:
        return std::unique_ptr<SerializedType> (new STVariableLength (name));

    case STI_ACCOUNT:
        return std::unique_ptr<SerializedType> (new STAccount (name));

    case STI_PATHSET:
        return std::unique_ptr<SerializedType> (new STPathSet (name));

    case STI_OBJECT:
        return std::unique_ptr<SerializedType> (new STObject (name));

    case STI_ARRAY:
        return std::unique_ptr<SerializedType> (new STArray (name));

    default:
        WriteLog (lsFATAL, STObject) << "Object type: " << beast::lexicalCast <std::string> (id);
        assert (false);
        throw std::runtime_error ("Unknown object type");
    }
}

// VFALCO TODO Remove the 'depth' parameter
std::unique_ptr<SerializedType> STObject::makeDeserializedObject (SerializedTypeID id, SField::ref name,
        SerializerIterator& sit, int depth)
{
    switch (id)
    {
    case STI_NOTPRESENT:
        return SerializedType::deserialize (name);

    case STI_UINT8:
        return STUInt8::deserialize (sit, name);

    case STI_UINT16:
        return STUInt16::deserialize (sit, name);

    case STI_UINT32:
        return STUInt32::deserialize (sit, name);

    case STI_UINT64:
        return STUInt64::deserialize (sit, name);

    case STI_AMOUNT:
        return STAmount::deserialize (sit, name);

    case STI_HASH128:
        return STHash128::deserialize (sit, name);

    case STI_HASH160:
        return STHash160::deserialize (sit, name);

    case STI_HASH256:
        return STHash256::deserialize (sit, name);

    case STI_VECTOR256:
        return STVector256::deserialize (sit, name);

    case STI_VL:
        return STVariableLength::deserialize (sit, name);

    case STI_ACCOUNT:
        return STAccount::deserialize (sit, name);

    case STI_PATHSET:
        return STPathSet::deserialize (sit, name);

    case STI_ARRAY:
        return STArray::deserialize (sit, name);

    case STI_OBJECT:
        return STObject::deserialize (sit, name);

    default:
        throw std::runtime_error ("Unknown object type");
    }
}

void STObject::set (const SOTemplate& type)
{
    mData.clear ();
    mType = &type;

    for (SOTemplate::value_type const& elem : type.peek ())
    {
        if (elem->flags != SOE_REQUIRED)
            giveObject (makeNonPresentObject (elem->e_field));
        else
            giveObject (makeDefaultObject (elem->e_field));
    }
}

bool STObject::setType (const SOTemplate& type)
{
    boost::ptr_vector<SerializedType> newData (type.peek ().size ());
    bool valid = true;

    mType = &type;

    for (SOTemplate::value_type const& elem : type.peek ())
    {
        bool match = false;

        for (boost::ptr_vector<SerializedType>::iterator it = mData.begin (); it != mData.end (); ++it)
            if (it->getFName () == elem->e_field)
            {
                // matching entry, move to new vector
                match = true;

                if ((elem->flags == SOE_DEFAULT) && it->isDefault ())
                {
                    WriteLog (lsWARNING, STObject) << "setType( " << getFName ().getName () << ") invalid default "
                                                   << elem->e_field.fieldName;
                    valid = false;
                }

                newData.push_back (mData.release (it).release ()); // CAUTION: This renders 'it' invalid
                break;
            }

        if (!match)
        {
            // no match found
            if (elem->flags == SOE_REQUIRED)
            {
                WriteLog (lsWARNING, STObject) << "setType( " << getFName ().getName () << ") invalid missing "
                                               << elem->e_field.fieldName;
                valid = false;
            }

            newData.push_back (makeNonPresentObject (elem->e_field).release ());
        }
    }

    BOOST_FOREACH (const SerializedType & t, mData)
    {
        // Anything left over must be discardable
        if (!t.getFName ().isDiscardable ())
        {
            WriteLog (lsWARNING, STObject) << "setType( " << getFName ().getName () << ") invalid leftover "
                                           << t.getFName ().getName ();
            valid = false;
        }
    }

    mData.swap (newData);
    return valid;
}

bool STObject::isValidForType ()
{
    boost::ptr_vector<SerializedType>::iterator it = mData.begin ();

    for (SOTemplate::value_type const& elem : mType->peek ())
    {
        if (it == mData.end ())
            return false;

        if (elem->e_field != it->getFName ())
            return false;

        ++it;
    }

    return true;
}

bool STObject::isFieldAllowed (SField::ref field)
{
    if (mType == nullptr)
        return true;

    return mType->getIndex (field) != -1;
}

// OLD
/*
bool STObject::set(SerializerIterator& sit, int depth)
{ // return true = terminated with end-of-object
    mData.clear();
    while (!sit.empty())
    {
        int type, field;
        sit.getFieldID(type, field);
        if ((type == STI_OBJECT) && (field == 1)) // end of object indicator
            return true;
        SField::ref fn = SField::getField(type, field);
        if (fn.isInvalid())
        {
            WriteLog (lsWARNING, STObject) << "Unknown field: field_type=" << type << ", field_name=" << field;
            throw std::runtime_error("Unknown field");
        }
        giveObject(makeDeserializedObject(fn.fieldType, fn, sit, depth + 1));
    }
    return false;
}
*/

// return true = terminated with end-of-object
bool STObject::set (SerializerIterator& sit, int depth)
{
    bool reachedEndOfObject = false;

    // Empty the destination buffer
    //
    mData.clear ();

    // Consume data in the pipe until we run out or reach the end
    //
    while (!reachedEndOfObject && !sit.empty ())
    {
        int type;
        int field;

        // Get the metadata for the next field
        //
        sit.getFieldID (type, field);

        reachedEndOfObject = (type == STI_OBJECT) && (field == 1);

        if (!reachedEndOfObject)
        {
            // Figure out the field
            //
            SField::ref fn = SField::getField (type, field);

            if (fn.isInvalid ())
            {
                WriteLog (lsWARNING, STObject) << "Unknown field: field_type=" << type << ", field_name=" << field;
                throw std::runtime_error ("Unknown field");
            }

            // Unflatten the field
            //
            giveObject (makeDeserializedObject (fn.fieldType, fn, sit, depth + 1));
        }
    }

    return reachedEndOfObject;
}


std::unique_ptr<SerializedType> STObject::deserialize (SerializerIterator& sit, SField::ref name)
{
    STObject* o;
    std::unique_ptr<SerializedType> object (o = new STObject (name));
    o->set (sit, 1);
    return object;
}

bool STObject::hasMatchingEntry (const SerializedType& t)
{
    const SerializedType* o = peekAtPField (t.getFName ());

    if (!o)
        return false;

    return t == *o;
}

std::string STObject::getFullText () const
{
    std::string ret;
    bool first = true;

    if (fName->hasName ())
    {
        ret = fName->getName ();
        ret += " = {";
    }
    else ret = "{";

    BOOST_FOREACH (const SerializedType & it, mData)

    if (it.getSType () != STI_NOTPRESENT)
    {
        if (!first) ret += ", ";
        else first = false;

        ret += it.getFullText ();
    }

    ret += "}";
    return ret;
}

void STObject::add (Serializer& s, bool withSigningFields) const
{
    std::map<int, const SerializedType*> fields;

    BOOST_FOREACH (const SerializedType & it, mData)
    {
        // pick out the fields and sort them
        if ((it.getSType () != STI_NOTPRESENT) && it.getFName ().shouldInclude (withSigningFields))
            fields.insert (std::make_pair (it.getFName ().fieldCode, &it));
    }


    typedef std::map<int, const SerializedType*>::value_type field_iterator;
    BOOST_FOREACH (field_iterator & it, fields)
    {
        // insert them in sorted order
        const SerializedType* field = it.second;

        field->addFieldID (s);
        field->add (s);

        if (dynamic_cast<const STArray*> (field) != nullptr)
            s.addFieldID (STI_ARRAY, 1);
        else if (dynamic_cast<const STObject*> (field) != nullptr)
            s.addFieldID (STI_OBJECT, 1);
    }
}

std::string STObject::getText () const
{
    std::string ret = "{";
    bool first = false;
    BOOST_FOREACH (const SerializedType & it, mData)
    {
        if (!first)
        {
            ret += ", ";
            first = false;
        }

        ret += it.getText ();
    }
    ret += "}";
    return ret;
}

bool STObject::isEquivalent (const SerializedType& t) const
{
    const STObject* v = dynamic_cast<const STObject*> (&t);

    if (!v)
    {
        WriteLog (lsDEBUG, STObject) << "notEquiv " << getFullText() << " not object";
        return false;
    }

    boost::ptr_vector<SerializedType>::const_iterator it1 = mData.begin (), end1 = mData.end ();
    boost::ptr_vector<SerializedType>::const_iterator it2 = v->mData.begin (), end2 = v->mData.end ();

    while ((it1 != end1) && (it2 != end2))
    {
        if ((it1->getSType () != it2->getSType ()) || !it1->isEquivalent (*it2))
        {
            if (it1->getSType () != it2->getSType ())
            {
                WriteLog (lsDEBUG, STObject) << "notEquiv type " << it1->getFullText() << " != "
                    <<  it2->getFullText();
            }
            else
            {
                WriteLog (lsDEBUG, STObject) << "notEquiv " << it1->getFullText() << " != "
                    <<  it2->getFullText();
            }
            return false;
        }

        ++it1;
        ++it2;
    }

    return (it1 == end1) && (it2 == end2);
}

uint256 STObject::getHash (std::uint32_t prefix) const
{
    Serializer s;
    s.add32 (prefix);
    add (s, true);
    return s.getSHA512Half ();
}

uint256 STObject::getSigningHash (std::uint32_t prefix) const
{
    Serializer s;
    s.add32 (prefix);
    add (s, false);
    return s.getSHA512Half ();
}

int STObject::getFieldIndex (SField::ref field) const
{
    if (mType != nullptr)
        return mType->getIndex (field);

    int i = 0;
    BOOST_FOREACH (const SerializedType & elem, mData)
    {
        if (elem.getFName () == field)
            return i;

        ++i;
    }
    return -1;
}

const SerializedType& STObject::peekAtField (SField::ref field) const
{
    int index = getFieldIndex (field);

    if (index == -1)
        throw std::runtime_error ("Field not found");

    return peekAtIndex (index);
}

SerializedType& STObject::getField (SField::ref field)
{
    int index = getFieldIndex (field);

    if (index == -1)
        throw std::runtime_error ("Field not found");

    return getIndex (index);
}

SField::ref STObject::getFieldSType (int index) const
{
    return mData[index].getFName ();
}

const SerializedType* STObject::peekAtPField (SField::ref field) const
{
    int index = getFieldIndex (field);

    if (index == -1)
        return nullptr;

    return peekAtPIndex (index);
}

SerializedType* STObject::getPField (SField::ref field, bool createOkay)
{
    int index = getFieldIndex (field);

    if (index == -1)
    {
        if (createOkay && isFree ())
            return getPIndex (giveObject (makeDefaultObject (field)));

        return nullptr;
    }

    return getPIndex (index);
}

bool STObject::isFieldPresent (SField::ref field) const
{
    int index = getFieldIndex (field);

    if (index == -1)
        return false;

    return peekAtIndex (index).getSType () != STI_NOTPRESENT;
}

STObject& STObject::peekFieldObject (SField::ref field)
{
    SerializedType* rf = getPField (field, true);

    if (!rf)
        throw std::runtime_error ("Field not found");

    if (rf->getSType () == STI_NOTPRESENT)
        rf = makeFieldPresent (field);

    STObject* cf = dynamic_cast<STObject*> (rf);

    if (!cf)
        throw std::runtime_error ("Wrong field type");

    return *cf;
}

bool STObject::setFlag (std::uint32_t f)
{
    STUInt32* t = dynamic_cast<STUInt32*> (getPField (sfFlags, true));

    if (!t)
        return false;

    t->setValue (t->getValue () | f);
    return true;
}

bool STObject::clearFlag (std::uint32_t f)
{
    STUInt32* t = dynamic_cast<STUInt32*> (getPField (sfFlags));

    if (!t)
        return false;

    t->setValue (t->getValue () & ~f);
    return true;
}

bool STObject::isFlag (std::uint32_t f) const
{
    return (getFlags () & f) == f;
}

std::uint32_t STObject::getFlags (void) const
{
    const STUInt32* t = dynamic_cast<const STUInt32*> (peekAtPField (sfFlags));

    if (!t)
        return 0;

    return t->getValue ();
}

SerializedType* STObject::makeFieldPresent (SField::ref field)
{
    int index = getFieldIndex (field);

    if (index == -1)
    {
        if (!isFree ())
            throw std::runtime_error ("Field not found");

        return getPIndex (giveObject (makeNonPresentObject (field)));
    }

    SerializedType* f = getPIndex (index);

    if (f->getSType () != STI_NOTPRESENT)
        return f;

    mData.replace (index, makeDefaultObject (f->getFName ()).release ());
    return getPIndex (index);
}

void STObject::makeFieldAbsent (SField::ref field)
{
    int index = getFieldIndex (field);

    if (index == -1)
        throw std::runtime_error ("Field not found");

    const SerializedType& f = peekAtIndex (index);

    if (f.getSType () == STI_NOTPRESENT)
        return;

    mData.replace (index, makeNonPresentObject (f.getFName ()).release ());
}

bool STObject::delField (SField::ref field)
{
    int index = getFieldIndex (field);

    if (index == -1)
        return false;

    delField (index);
    return true;
}

void STObject::delField (int index)
{
    mData.erase (mData.begin () + index);
}

std::string STObject::getFieldString (SField::ref field) const
{
    const SerializedType* rf = peekAtPField (field);

    if (!rf) throw std::runtime_error ("Field not found");

    return rf->getText ();
}

unsigned char STObject::getFieldU8 (SField::ref field) const
{
    const SerializedType* rf = peekAtPField (field);

    if (!rf) throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id == STI_NOTPRESENT) return 0; // optional field not present

    const STUInt8* cf = dynamic_cast<const STUInt8*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    return cf->getValue ();
}

std::uint16_t STObject::getFieldU16 (SField::ref field) const
{
    const SerializedType* rf = peekAtPField (field);

    if (!rf) throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id == STI_NOTPRESENT) return 0; // optional field not present

    const STUInt16* cf = dynamic_cast<const STUInt16*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    return cf->getValue ();
}

std::uint32_t STObject::getFieldU32 (SField::ref field) const
{
    const SerializedType* rf = peekAtPField (field);

    if (!rf) throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id == STI_NOTPRESENT) return 0; // optional field not present

    const STUInt32* cf = dynamic_cast<const STUInt32*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    return cf->getValue ();
}

std::uint64_t STObject::getFieldU64 (SField::ref field) const
{
    const SerializedType* rf = peekAtPField (field);

    if (!rf) throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id == STI_NOTPRESENT) return 0; // optional field not present

    const STUInt64* cf = dynamic_cast<const STUInt64*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    return cf->getValue ();
}

uint128 STObject::getFieldH128 (SField::ref field) const
{
    const SerializedType* rf = peekAtPField (field);

    if (!rf) throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id == STI_NOTPRESENT) return uint128 (); // optional field not present

    const STHash128* cf = dynamic_cast<const STHash128*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    return cf->getValue ();
}

uint160 STObject::getFieldH160 (SField::ref field) const
{
    const SerializedType* rf = peekAtPField (field);

    if (!rf) throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id == STI_NOTPRESENT) return uint160 (); // optional field not present

    const STHash160* cf = dynamic_cast<const STHash160*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    return cf->getValue ();
}

uint256 STObject::getFieldH256 (SField::ref field) const
{
    const SerializedType* rf = peekAtPField (field);

    if (!rf)
        throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id == STI_NOTPRESENT) return uint256 (); // optional field not present

    const STHash256* cf = dynamic_cast<const STHash256*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    return cf->getValue ();
}

RippleAddress STObject::getFieldAccount (SField::ref field) const
{
    const SerializedType* rf = peekAtPField (field);

    if (!rf)
        throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id == STI_NOTPRESENT) return RippleAddress (); // optional field not present

    const STAccount* cf = dynamic_cast<const STAccount*> (rf);

    if (!cf)
        throw std::runtime_error ("Wrong field type");

    return cf->getValueNCA ();
}

uint160 STObject::getFieldAccount160 (SField::ref field) const
{
    uint160 a;
    const SerializedType* rf = peekAtPField (field);

    if (!rf)
        throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id != STI_NOTPRESENT)
    {
        const STAccount* cf = dynamic_cast<const STAccount*> (rf);

        if (!cf)
            throw std::runtime_error ("Wrong field type");

        cf->getValueH160 (a);
    }

    return a;
}

Blob STObject::getFieldVL (SField::ref field) const
{
    const SerializedType* rf = peekAtPField (field);

    if (!rf) throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id == STI_NOTPRESENT) return Blob (); // optional field not present

    const STVariableLength* cf = dynamic_cast<const STVariableLength*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    return cf->getValue ();
}

const STAmount& STObject::getFieldAmount (SField::ref field) const
{
    static STAmount empty;
    const SerializedType* rf = peekAtPField (field);

    if (!rf)
        throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id == STI_NOTPRESENT)
        return empty; // optional field not present

    const STAmount* cf = dynamic_cast<const STAmount*> (rf);

    if (!cf)
        throw std::runtime_error ("Wrong field type");

    return *cf;
}

const STArray& STObject::getFieldArray (SField::ref field) const
{
    static STArray empty;
    const SerializedType* rf = peekAtPField (field);

    if (!rf) throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id == STI_NOTPRESENT) return empty;

    const STArray* cf = dynamic_cast<const STArray*> (rf);

    if (!cf)
        throw std::runtime_error ("Wrong field type");

    return *cf;
}

const STPathSet& STObject::getFieldPathSet (SField::ref field) const
{
    static STPathSet empty;
    const SerializedType* rf = peekAtPField (field);

    if (!rf) throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id == STI_NOTPRESENT) return empty; // optional field not present

    const STPathSet* cf = dynamic_cast<const STPathSet*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    return *cf;
}

const STVector256& STObject::getFieldV256 (SField::ref field) const
{
    static STVector256 empty;
    const SerializedType* rf = peekAtPField (field);

    if (!rf) throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id == STI_NOTPRESENT) return empty; // optional field not present

    const STVector256* cf = dynamic_cast<const STVector256*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    return *cf;
}

void STObject::setFieldU8 (SField::ref field, unsigned char v)
{
    SerializedType* rf = getPField (field, true);

    if (!rf) throw std::runtime_error ("Field not found");

    if (rf->getSType () == STI_NOTPRESENT) rf = makeFieldPresent (field);

    STUInt8* cf = dynamic_cast<STUInt8*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    cf->setValue (v);
}

void STObject::setFieldU16 (SField::ref field, std::uint16_t v)
{
    SerializedType* rf = getPField (field, true);

    if (!rf) throw std::runtime_error ("Field not found");

    if (rf->getSType () == STI_NOTPRESENT) rf = makeFieldPresent (field);

    STUInt16* cf = dynamic_cast<STUInt16*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    cf->setValue (v);
}

void STObject::setFieldU32 (SField::ref field, std::uint32_t v)
{
    SerializedType* rf = getPField (field, true);

    if (!rf) throw std::runtime_error ("Field not found");

    if (rf->getSType () == STI_NOTPRESENT) rf = makeFieldPresent (field);

    STUInt32* cf = dynamic_cast<STUInt32*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    cf->setValue (v);
}

void STObject::setFieldU64 (SField::ref field, std::uint64_t v)
{
    SerializedType* rf = getPField (field, true);

    if (!rf) throw std::runtime_error ("Field not found");

    if (rf->getSType () == STI_NOTPRESENT) rf = makeFieldPresent (field);

    STUInt64* cf = dynamic_cast<STUInt64*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    cf->setValue (v);
}

void STObject::setFieldH128 (SField::ref field, const uint128& v)
{
    SerializedType* rf = getPField (field, true);

    if (!rf) throw std::runtime_error ("Field not found");

    if (rf->getSType () == STI_NOTPRESENT) rf = makeFieldPresent (field);

    STHash128* cf = dynamic_cast<STHash128*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    cf->setValue (v);
}

void STObject::setFieldH160 (SField::ref field, const uint160& v)
{
    SerializedType* rf = getPField (field, true);

    if (!rf) throw std::runtime_error ("Field not found");

    if (rf->getSType () == STI_NOTPRESENT) rf = makeFieldPresent (field);

    STHash160* cf = dynamic_cast<STHash160*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    cf->setValue (v);
}

void STObject::setFieldH256 (SField::ref field, uint256 const& v)
{
    SerializedType* rf = getPField (field, true);

    if (!rf) throw std::runtime_error ("Field not found");

    if (rf->getSType () == STI_NOTPRESENT) rf = makeFieldPresent (field);

    STHash256* cf = dynamic_cast<STHash256*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    cf->setValue (v);
}

void STObject::setFieldV256 (SField::ref field, const STVector256& v)
{
    SerializedType* rf = getPField (field, true);

    if (!rf) throw std::runtime_error ("Field not found");

    if (rf->getSType () == STI_NOTPRESENT) rf = makeFieldPresent (field);

    STVector256* cf = dynamic_cast<STVector256*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    cf->setValue (v);
}

void STObject::setFieldAccount (SField::ref field, const uint160& v)
{
    SerializedType* rf = getPField (field, true);

    if (!rf) throw std::runtime_error ("Field not found");

    if (rf->getSType () == STI_NOTPRESENT) rf = makeFieldPresent (field);

    STAccount* cf = dynamic_cast<STAccount*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    cf->setValueH160 (v);
}

void STObject::setFieldVL (SField::ref field, Blob const& v)
{
    SerializedType* rf = getPField (field, true);

    if (!rf) throw std::runtime_error ("Field not found");

    if (rf->getSType () == STI_NOTPRESENT) rf = makeFieldPresent (field);

    STVariableLength* cf = dynamic_cast<STVariableLength*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    cf->setValue (v);
}

void STObject::setFieldAmount (SField::ref field, const STAmount& v)
{
    SerializedType* rf = getPField (field, true);

    if (!rf) throw std::runtime_error ("Field not found");

    if (rf->getSType () == STI_NOTPRESENT) rf = makeFieldPresent (field);

    STAmount* cf = dynamic_cast<STAmount*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    (*cf) = v;
}

void STObject::setFieldPathSet (SField::ref field, const STPathSet& v)
{
    SerializedType* rf = getPField (field, true);

    if (!rf) throw std::runtime_error ("Field not found");

    if (rf->getSType () == STI_NOTPRESENT) rf = makeFieldPresent (field);

    STPathSet* cf = dynamic_cast<STPathSet*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    (*cf) = v;
}

Json::Value STObject::getJson (int options) const
{
    Json::Value ret (Json::objectValue);
    int index = 1;
    BOOST_FOREACH (const SerializedType & it, mData)
    {
        if (it.getSType () != STI_NOTPRESENT)
        {
            if (!it.getFName ().hasName ())
                ret[beast::lexicalCast <std::string> (index)] = it.getJson (options);
            else
                ret[it.getJsonName ()] = it.getJson (options);
        }
    }
    return ret;
}

bool STObject::operator== (const STObject& obj) const
{
    // This is not particularly efficient, and only compares data elements with binary representations
    int matches = 0;
    BOOST_FOREACH (const SerializedType & t, mData)

    if ((t.getSType () != STI_NOTPRESENT) && t.getFName ().isBinary ())
    {
        // each present field must have a matching field
        bool match = false;
        BOOST_FOREACH (const SerializedType & t2, obj.mData)

        if (t.getFName () == t2.getFName ())
        {
            if (t2 != t)
                return false;

            match = true;
            ++matches;
            break;
        }

        if (!match)
        {
            Log (lsTRACE) << "STObject::operator==: no match for " << t.getFName ().getName ();
            return false;
        }
    }

    int fields = 0;
    BOOST_FOREACH (const SerializedType & t2, obj.mData)

    if ((t2.getSType () != STI_NOTPRESENT) && t2.getFName ().isBinary ())
        ++fields;

    if (fields != matches)
    {
        Log (lsTRACE) << "STObject::operator==: " << fields << " fields, " << matches << " matches";
        return false;
    }

    return true;
}

Json::Value STVector256::getJson (int options) const
{
    Json::Value ret (Json::arrayValue);

    for (auto const& vEntry : mValue)
        ret.append (to_string (vEntry));

    return ret;
}

std::string STArray::getFullText () const
{
    std::string r = "[";

    bool first = true;
    BOOST_FOREACH (const STObject & o, value)
    {
        if (!first)
            r += ",";

        r += o.getFullText ();
        first = false;
    }

    r += "]";
    return r;
}

std::string STArray::getText () const
{
    std::string r = "[";

    bool first = true;
    BOOST_FOREACH (const STObject & o, value)
    {
        if (!first)
            r += ",";

        r += o.getText ();
        first = false;
    }

    r += "]";
    return r;
}

Json::Value STArray::getJson (int p) const
{
    Json::Value v = Json::arrayValue;
    int index = 1;
    BOOST_FOREACH (const STObject & object, value)
    {
        if (object.getSType () != STI_NOTPRESENT)
        {
            Json::Value inner = Json::objectValue;

            if (!object.getFName ().hasName ())
                inner[beast::lexicalCast <std::string> (index)] = object.getJson (p);
            else
                inner[object.getName ()] = object.getJson (p);

            v.append (inner);
            index++;
        }
    }
    return v;
}

void STArray::add (Serializer& s) const
{
    BOOST_FOREACH (const STObject & object, value)
    {
        object.addFieldID (s);
        object.add (s);
        s.addFieldID (STI_OBJECT, 1);
    }
}

bool STArray::isEquivalent (const SerializedType& t) const
{
    const STArray* v = dynamic_cast<const STArray*> (&t);

    if (!v)
    {
        WriteLog (lsDEBUG, STObject) << "notEquiv " << getFullText() << " not array";
        return false;
    }

    return value == v->value;
}

STArray* STArray::construct (SerializerIterator& sit, SField::ref field)
{
    vector value;

    while (!sit.empty ())
    {
        int type, field;
        sit.getFieldID (type, field);

        if ((type == STI_ARRAY) && (field == 1))
            break;

        SField::ref fn = SField::getField (type, field);

        if (fn.isInvalid ())
        {
            WriteLog (lsTRACE, STObject) << "Unknown field: " << type << "/" << field;
            throw std::runtime_error ("Unknown field");
        }

        value.push_back (new STObject (fn));
        value.rbegin ()->set (sit, 1);
    }

    return new STArray (field, value);
}

void STArray::sort (bool (*compare) (const STObject&, const STObject&))
{
    value.sort (compare);
}

//------------------------------------------------------------------------------

class SerializedObject_test : public beast::unit_test::suite
{
public:
    void run()
    {
        testSerialization();
        testParseJSONArray();
        testParseJSONArrayWithInvalidChildrenObjects();
    }

    bool parseJSONString (const std::string& json, Json::Value& to)
    {
        Json::Reader reader;
        return (reader.parse(json, to) &&
               !to.isNull() &&
                to.isObject());
    }

    void testParseJSONArrayWithInvalidChildrenObjects ()
    {
        testcase ("parse json array invalid children");
        try
        {
            /*

            STArray/STObject constructs don't really map perfectly to json
            arrays/objects.

            STObject is an associative container, mapping fields to value, but
            an STObject may also have a Field as it's name, stored outside the
            associative structure. The name is important, so to maintain
            fidelity, it will take TWO json objects to represent them.

            */
            std::string faulty ("{\"Template\":[{"
                                    "\"ModifiedNode\":{\"Sequence\":1}, "
                                    "\"DeletedNode\":{\"Sequence\":1}"
                                "}]}");

            std::unique_ptr<STObject> so;
            Json::Value faultyJson;
            bool parsedOK (parseJSONString(faulty, faultyJson));
            unexpected(!parsedOK, "failed to parse");
            STParsedJSON parsed ("test", faultyJson);
            expect (parsed.object.get() == nullptr,
                "It should have thrown. "
                  "Immediate children of STArray encoded as json must "
                  "have one key only.");
        }
        catch(std::runtime_error& e)
        {
            std::string what(e.what());
            unexpected (what.find("First level children of `Template`") != 0);
        }
    }

    void testParseJSONArray ()
    {
        testcase ("parse json array");
        std::string const json ("{\"Template\":[{\"ModifiedNode\":{\"Sequence\":1}}]}\n");

        Json::Value jsonObject;
        bool parsedOK (parseJSONString(json, jsonObject));
        if (parsedOK)
        {
            STParsedJSON parsed ("test", jsonObject);
            Json::FastWriter writer;
            std::string const& serialized (
                writer.write (parsed.object->getJson(0)));
            expect (serialized == json, serialized + " should equal: " + json);
        }
        else
        {
            fail ("Couldn't parse json: " + json);
        }
    }

    void testSerialization ()
    {
        testcase ("serialization");

        unexpected (sfGeneric.isUseful (), "sfGeneric must not be useful");

        SField sfTestVL (STI_VL, 255, "TestVL");
        SField sfTestH256 (STI_HASH256, 255, "TestH256");
        SField sfTestU32 (STI_UINT32, 255, "TestU32");
        SField sfTestObject (STI_OBJECT, 255, "TestObject");

        SOTemplate elements;
        elements.push_back (SOElement (sfFlags, SOE_REQUIRED));
        elements.push_back (SOElement (sfTestVL, SOE_REQUIRED));
        elements.push_back (SOElement (sfTestH256, SOE_OPTIONAL));
        elements.push_back (SOElement (sfTestU32, SOE_REQUIRED));

        STObject object1 (elements, sfTestObject);
        STObject object2 (object1);

        unexpected (object1.getSerializer () != object2.getSerializer (), "STObject error 1");

        unexpected (object1.isFieldPresent (sfTestH256) || !object1.isFieldPresent (sfTestVL),
            "STObject error");

        object1.makeFieldPresent (sfTestH256);

        unexpected (!object1.isFieldPresent (sfTestH256), "STObject Error 2");

        unexpected (object1.getFieldH256 (sfTestH256) != uint256 (), "STObject error 3");

        if (object1.getSerializer () == object2.getSerializer ())
        {
            WriteLog (lsINFO, STObject) << "O1: " << object1.getJson (0);
            WriteLog (lsINFO, STObject) << "O2: " << object2.getJson (0);
            fail ("STObject error 4");
        }
        else
        {
            pass ();
        }

        object1.makeFieldAbsent (sfTestH256);

        unexpected (object1.isFieldPresent (sfTestH256), "STObject error 5");

        unexpected (object1.getFlags () != 0, "STObject error 6");

        unexpected (object1.getSerializer () != object2.getSerializer (), "STObject error 7");

        STObject copy (object1);

        unexpected (object1.isFieldPresent (sfTestH256), "STObject error 8");

        unexpected (copy.isFieldPresent (sfTestH256), "STObject error 9");

        unexpected (object1.getSerializer () != copy.getSerializer (), "STObject error 10");

        copy.setFieldU32 (sfTestU32, 1);

        unexpected (object1.getSerializer () == copy.getSerializer (), "STObject error 11");

        for (int i = 0; i < 1000; i++)
        {
            Blob j (i, 2);

            object1.setFieldVL (sfTestVL, j);

            Serializer s;
            object1.add (s);
            SerializerIterator it (s);

            STObject object3 (elements, it, sfTestObject);

            unexpected (object1.getFieldVL (sfTestVL) != j, "STObject error");

            unexpected (object3.getFieldVL (sfTestVL) != j, "STObject error");
        }
    }
};

BEAST_DEFINE_TESTSUITE(SerializedObject,ripple_data,ripple);

} // ripple
