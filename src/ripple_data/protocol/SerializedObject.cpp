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


SETUP_LOG (STObject)

UPTR_T<SerializedType> STObject::makeDefaultObject (SerializedTypeID id, SField::ref name)
{
    assert ((id == STI_NOTPRESENT) || (id == name.fieldType));

    switch (id)
    {
    case STI_NOTPRESENT:
        return UPTR_T<SerializedType> (new SerializedType (name));

    case STI_UINT8:
        return UPTR_T<SerializedType> (new STUInt8 (name));

    case STI_UINT16:
        return UPTR_T<SerializedType> (new STUInt16 (name));

    case STI_UINT32:
        return UPTR_T<SerializedType> (new STUInt32 (name));

    case STI_UINT64:
        return UPTR_T<SerializedType> (new STUInt64 (name));

    case STI_AMOUNT:
        return UPTR_T<SerializedType> (new STAmount (name));

    case STI_HASH128:
        return UPTR_T<SerializedType> (new STHash128 (name));

    case STI_HASH160:
        return UPTR_T<SerializedType> (new STHash160 (name));

    case STI_HASH256:
        return UPTR_T<SerializedType> (new STHash256 (name));

    case STI_VECTOR256:
        return UPTR_T<SerializedType> (new STVector256 (name));

    case STI_VL:
        return UPTR_T<SerializedType> (new STVariableLength (name));

    case STI_ACCOUNT:
        return UPTR_T<SerializedType> (new STAccount (name));

    case STI_PATHSET:
        return UPTR_T<SerializedType> (new STPathSet (name));

    case STI_OBJECT:
        return UPTR_T<SerializedType> (new STObject (name));

    case STI_ARRAY:
        return UPTR_T<SerializedType> (new STArray (name));

    default:
        WriteLog (lsFATAL, STObject) << "Object type: " << lexicalCast <std::string> (id);
        assert (false);
        throw std::runtime_error ("Unknown object type");
    }
}

// VFALCO TODO Remove the 'depth' parameter
UPTR_T<SerializedType> STObject::makeDeserializedObject (SerializedTypeID id, SField::ref name,
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

    BOOST_FOREACH (const SOElement * elem, type.peek ())
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

    BOOST_FOREACH (const SOElement * elem, type.peek ())
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

    BOOST_FOREACH (const SOElement * elem, mType->peek ())
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
    if (mType == NULL)
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


UPTR_T<SerializedType> STObject::deserialize (SerializerIterator& sit, SField::ref name)
{
    STObject* o;
    UPTR_T<SerializedType> object (o = new STObject (name));
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

        if (dynamic_cast<const STArray*> (field) != NULL)
            s.addFieldID (STI_ARRAY, 1);
        else if (dynamic_cast<const STObject*> (field) != NULL)
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

uint256 STObject::getHash (uint32 prefix) const
{
    Serializer s;
    s.add32 (prefix);
    add (s, true);
    return s.getSHA512Half ();
}

uint256 STObject::getSigningHash (uint32 prefix) const
{
    Serializer s;
    s.add32 (prefix);
    add (s, false);
    return s.getSHA512Half ();
}

int STObject::getFieldIndex (SField::ref field) const
{
    if (mType != NULL)
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
        return NULL;

    return peekAtPIndex (index);
}

SerializedType* STObject::getPField (SField::ref field, bool createOkay)
{
    int index = getFieldIndex (field);

    if (index == -1)
    {
        if (createOkay && isFree ())
            return getPIndex (giveObject (makeDefaultObject (field)));

        return NULL;
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

bool STObject::setFlag (uint32 f)
{
    STUInt32* t = dynamic_cast<STUInt32*> (getPField (sfFlags, true));

    if (!t)
        return false;

    t->setValue (t->getValue () | f);
    return true;
}

bool STObject::clearFlag (uint32 f)
{
    STUInt32* t = dynamic_cast<STUInt32*> (getPField (sfFlags));

    if (!t)
        return false;

    t->setValue (t->getValue () & ~f);
    return true;
}

bool STObject::isFlag (uint32 f)
{
    return (getFlags () & f) == f;
}

uint32 STObject::getFlags (void) const
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

uint16 STObject::getFieldU16 (SField::ref field) const
{
    const SerializedType* rf = peekAtPField (field);

    if (!rf) throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id == STI_NOTPRESENT) return 0; // optional field not present

    const STUInt16* cf = dynamic_cast<const STUInt16*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    return cf->getValue ();
}

uint32 STObject::getFieldU32 (SField::ref field) const
{
    const SerializedType* rf = peekAtPField (field);

    if (!rf) throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id == STI_NOTPRESENT) return 0; // optional field not present

    const STUInt32* cf = dynamic_cast<const STUInt32*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    return cf->getValue ();
}

uint64 STObject::getFieldU64 (SField::ref field) const
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

void STObject::setFieldU16 (SField::ref field, uint16 v)
{
    SerializedType* rf = getPField (field, true);

    if (!rf) throw std::runtime_error ("Field not found");

    if (rf->getSType () == STI_NOTPRESENT) rf = makeFieldPresent (field);

    STUInt16* cf = dynamic_cast<STUInt16*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    cf->setValue (v);
}

void STObject::setFieldU32 (SField::ref field, uint32 v)
{
    SerializedType* rf = getPField (field, true);

    if (!rf) throw std::runtime_error ("Field not found");

    if (rf->getSType () == STI_NOTPRESENT) rf = makeFieldPresent (field);

    STUInt32* cf = dynamic_cast<STUInt32*> (rf);

    if (!cf) throw std::runtime_error ("Wrong field type");

    cf->setValue (v);
}

void STObject::setFieldU64 (SField::ref field, uint64 v)
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
                ret[lexicalCast <std::string> (index)] = it.getJson (options);
            else
                ret[it.getName ()] = it.getJson (options);
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

    BOOST_FOREACH (std::vector<uint256>::const_iterator::value_type vEntry, mValue)
    ret.append (vEntry.ToString ());

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
                inner[lexicalCast <std::string> (index)] = object.getJson (p);
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

UPTR_T<STObject> STObject::parseJson (const Json::Value& object, SField::ref inName, int depth)
{
    if (!object.isObject ())
        throw std::runtime_error ("Value is not an object");

    SField::ptr name = &inName;

    boost::ptr_vector<SerializedType> data;
    Json::Value::Members members (object.getMemberNames ());

    for (Json::Value::Members::iterator it = members.begin (), end = members.end (); it != end; ++it)
    {
        const std::string& fieldName = *it;
        const Json::Value& value = object[fieldName];

        SField::ref field = SField::getField (fieldName);

        if (field == sfInvalid)
            throw std::runtime_error ("Unknown field: " + fieldName);

        switch (field.fieldType)
        {
        case STI_UINT8:
            if (value.isString ())
            {
#if 0

                if (field == sfTransactionResult)
                {
                    TER terCode;

                    if (FUNCTION_THAT_DOESNT_EXIST (value.asString (), terCode))
                        value = static_cast<int> (terCode);
                    else
                        data.push_back (new STUInt8 (field, lexicalCastThrow <unsigned char> (value.asString ())));
                }

                data.push_back (new STUInt8 (field, lexicalCastThrow <unsigned char> (value.asString ())));
#endif
            }
            else if (value.isInt ())
            {
                if (value.asInt () < 0 || value.asInt () > 255)
                    throw std::runtime_error ("value out of range");

                data.push_back (new STUInt8 (field, range_check_cast<unsigned char> (value.asInt (), 0, 255)));
            }
            else if (value.isUInt ())
            {
                if (value.asUInt () > 255)
                    throw std::runtime_error ("value out of range");

                data.push_back (new STUInt8 (field, range_check_cast<unsigned char> (value.asUInt (), 0, 255)));
            }
            else
                throw std::runtime_error ("Incorrect type");

            break;

        case STI_UINT16:
            if (value.isString ())
            {
                std::string strValue = value.asString ();

                if (!strValue.empty () && ((strValue[0] < '0') || (strValue[0] > '9')))
                {
                    if (field == sfTransactionType)
                    {
                        // Retrieve type from name. Throws if not found.
                        TxType const txType = TxFormats::getInstance()->findTypeByName (strValue);

                        data.push_back (new STUInt16 (field, static_cast<uint16> (txType)));

                        if (*name == sfGeneric)
                            name = &sfTransaction;
                    }
                    else if (field == sfLedgerEntryType)
                    {
                        LedgerEntryType const type = LedgerFormats::getInstance()->findTypeByName (strValue);

                        data.push_back (new STUInt16 (field, static_cast<uint16> (type)));

                        if (*name == sfGeneric)
                            name = &sfLedgerEntry;
                    }
                    else
                        throw std::runtime_error ("Invalid field data");
                }
                else
                    data.push_back (new STUInt16 (field, lexicalCastThrow <uint16> (strValue)));
            }
            else if (value.isInt ())
                data.push_back (new STUInt16 (field, range_check_cast<uint16> (value.asInt (), 0, 65535)));
            else if (value.isUInt ())
                data.push_back (new STUInt16 (field, range_check_cast<uint16> (value.asUInt (), 0, 65535)));
            else
                throw std::runtime_error ("Incorrect type");

            break;

        case STI_UINT32:
            if (value.isString ())
            {
                data.push_back (new STUInt32 (field, lexicalCastThrow <uint32> (value.asString ())));
            }
            else if (value.isInt ())
            {
                data.push_back (new STUInt32 (field, range_check_cast <uint32> (value.asInt (), 0u, 4294967295u)));
            }
            else if (value.isUInt ())
            {
                data.push_back (new STUInt32 (field, static_cast<uint32> (value.asUInt ())));
            }
            else
            {
                throw std::runtime_error ("Incorrect type");
            }
            break;

        case STI_UINT64:
            if (value.isString ())
                data.push_back (new STUInt64 (field, uintFromHex (value.asString ())));
            else if (value.isInt ())
                data.push_back (new STUInt64 (field,
                                              range_check_cast<uint64> (value.asInt (), 0, 18446744073709551615ull)));
            else if (value.isUInt ())
                data.push_back (new STUInt64 (field, static_cast<uint64> (value.asUInt ())));
            else
                throw std::runtime_error ("Incorrect type");

            break;


        case STI_HASH128:
            if (value.isString ())
                data.push_back (new STHash128 (field, value.asString ()));
            else
                throw std::runtime_error ("Incorrect type");

            break;

        case STI_HASH160:
            if (value.isString ())
                data.push_back (new STHash160 (field, value.asString ()));
            else
                throw std::runtime_error ("Incorrect type");

            break;

        case STI_HASH256:
            if (value.isString ())
                data.push_back (new STHash256 (field, value.asString ()));
            else
                throw std::runtime_error ("Incorrect type");

            break;

        case STI_VL:
            if (!value.isString ())
                throw std::runtime_error ("Incorrect type");

            data.push_back (new STVariableLength (field, strUnHex (value.asString ())));
            break;

        case STI_AMOUNT:
            data.push_back (new STAmount (field, value));
            break;

        case STI_VECTOR256:
            if (!value.isArray ())
                throw std::runtime_error ("Incorrect type");

            {
                data.push_back (new STVector256 (field));
                STVector256* tail = dynamic_cast<STVector256*> (&data.back ());
                assert (tail);

                for (Json::UInt i = 0; !object.isValidIndex (i); ++i)
                {
                    uint256 s;
                    s.SetHex (object[i].asString ());
                    tail->addValue (s);
                }
            }
            break;

        case STI_PATHSET:
            if (!value.isArray ())
                throw std::runtime_error ("Path set must be array");

            {
                data.push_back (new STPathSet (field));
                STPathSet* tail = dynamic_cast<STPathSet*> (&data.back ());
                assert (tail);

                for (Json::UInt i = 0; value.isValidIndex (i); ++i)
                {
                    STPath p;

                    if (!value[i].isArray ())
                        throw std::runtime_error ("Path must be array");

                    for (Json::UInt j = 0; value[i].isValidIndex (j); ++j)
                    {
                        // each element in this path has some combination of account, currency, or issuer

                        Json::Value pathEl = value[i][j];

                        if (!pathEl.isObject ())
                            throw std::runtime_error ("Path elements must be objects");

                        const Json::Value& account  = pathEl["account"];
                        const Json::Value& currency = pathEl["currency"];
                        const Json::Value& issuer   = pathEl["issuer"];
                        bool hasCurrency            = false;
                        uint160 uAccount, uCurrency, uIssuer;

                        if (!account.isNull ())
                        {
                            // human account id
                            if (!account.isString ())
                                throw std::runtime_error ("path element accounts must be strings");

                            std::string strValue = account.asString ();

                            if (value.size () == 40) // 160-bit hex account value
                                uAccount.SetHex (strValue);

                            {
                                RippleAddress a;

                                if (!a.setAccountID (strValue))
                                    throw std::runtime_error ("Account in path element invalid");

                                uAccount = a.getAccountID ();
                            }
                        }

                        if (!currency.isNull ())
                        {
                            // human currency
                            if (!currency.isString ())
                                throw std::runtime_error ("path element currencies must be strings");

                            hasCurrency = true;

                            if (currency.asString ().size () == 40)
                                uCurrency.SetHex (currency.asString ());
                            else if (!STAmount::currencyFromString (uCurrency, currency.asString ()))
                                throw std::runtime_error ("invalid currency");
                        }

                        if (!issuer.isNull ())
                        {
                            // human account id
                            if (!issuer.isString ())
                                throw std::runtime_error ("path element issuers must be strings");

                            if (issuer.asString ().size () == 40)
                                uIssuer.SetHex (issuer.asString ());
                            else
                            {
                                RippleAddress a;

                                if (!a.setAccountID (issuer.asString ()))
                                    throw std::runtime_error ("path element issuer invalid");

                                uIssuer = a.getAccountID ();
                            }
                        }

                        p.addElement (STPathElement (uAccount, uCurrency, uIssuer, hasCurrency));
                    }

                    tail->addPath (p);
                }
            }
            break;

        case STI_ACCOUNT:
        {
            if (!value.isString ())
                throw std::runtime_error ("Incorrect type");

            std::string strValue = value.asString ();

            if (value.size () == 40) // 160-bit hex account value
            {
                uint160 v;
                v.SetHex (strValue);
                data.push_back (new STAccount (field, v));
            }
            else
            {
                // ripple address
                RippleAddress a;

                if (!a.setAccountID (strValue))
                {
                    WriteLog (lsINFO, STObject) << "Invalid acccount JSON: " << fieldName << ": " << strValue;
                    throw std::runtime_error ("Account invalid");
                }

                data.push_back (new STAccount (field, a.getAccountID ()));
            }
        }
        break;

        case STI_OBJECT:
        case STI_TRANSACTION:
        case STI_LEDGERENTRY:
        case STI_VALIDATION:
            if (!value.isObject ())
                throw std::runtime_error ("Inner value is not an object");

            if (depth > 64)
                throw std::runtime_error ("Json nest depth exceeded");

            data.push_back (parseJson (value, field, depth + 1).release ());
            break;

        case STI_ARRAY:
            if (!value.isArray ())
                throw std::runtime_error ("Inner value is not an array");

            {
                data.push_back (new STArray (field));
                STArray* tail = dynamic_cast<STArray*> (&data.back ());
                assert (tail);

                for (Json::UInt i = 0; value.isValidIndex (i); ++i)
                {

                    bool isObject (value[i].isObject());
                    bool singleKey (true);

                    if (isObject)
                    {
                         singleKey = value[i].size() == 1;
                    }

                    if (!isObject || !singleKey)
                    {
                        std::stringstream err;

                        err << "First level children of `"
                            << field.getName()
                            << "`must be objects containing a single key with"
                            << " an object value";

                        throw std::runtime_error (err.str());
                    }

                    // TODO: There doesn't seem to be a nice way to get just the
                    // first/only key in an object without copying all keys into
                    // a vector
                    std::string objectName (value[i].getMemberNames()[0]);;
                    SField::ref nameField (SField::getField(objectName));
                    Json::Value objectFields (value[i][objectName]);

                    tail->push_back (*STObject::parseJson (objectFields,
                                                           nameField,
                                                           depth + 1));
                }
            }
            break;

        default:
            throw std::runtime_error ("Invalid field type");
        }
    }

    return UPTR_T<STObject> (new STObject (*name, data));
}

//------------------------------------------------------------------------------

class SerializedObjectTests : public UnitTest
{
public:
    SerializedObjectTests () : UnitTest ("SerializedObject", "ripple")
    {
    }

    void runTest ()
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
        beginTestCase ("parse json array invalid children");
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

            UPTR_T<STObject> so;
            Json::Value faultyJson;
            bool parsedOK (parseJSONString(faulty, faultyJson));
            unexpected(!parsedOK, "failed to parse");
            so = STObject::parseJson (faultyJson);
            fail ("It should have thrown. "
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
        beginTestCase ("parse json array");
        std::string const json ("{\"Template\":[{\"ModifiedNode\":{\"Sequence\":1}}]}\n");

        Json::Value jsonObject;
        bool parsedOK (parseJSONString(json, jsonObject));
        if (parsedOK)
        {
          UPTR_T<STObject> so;
          so = STObject::parseJson (jsonObject);
          Json::FastWriter writer;
          std::string const& serialized (writer.write(so->getJson(0)));
          bool serializedOK = serialized == json;
          unexpected (!serializedOK, serialized + " should equal: " + json);
        }
        else
        {
          fail ("Couldn't parse json: " + json);
        }
    }

    void testSerialization ()
    {
        beginTestCase ("serialization");

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

static SerializedObjectTests serializedObjectTests;
