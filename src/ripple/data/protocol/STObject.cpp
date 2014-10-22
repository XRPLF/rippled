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

#include <ripple/basics/Log.h>
#include <beast/cxx14/memory.h> // <memory>

namespace ripple {

std::unique_ptr<SerializedType>
STObject::makeDefaultObject (SerializedTypeID id, SField::ref name)
{
    assert ((id == STI_NOTPRESENT) || (id == name.fieldType));

    switch (id)
    {
    case STI_NOTPRESENT:
        return std::make_unique <SerializedType> (name);

    case STI_UINT8:
        return std::make_unique <STUInt8> (name);

    case STI_UINT16:
        return std::make_unique <STUInt16> (name);

    case STI_UINT32:
        return std::make_unique <STUInt32> (name);

    case STI_UINT64:
        return std::make_unique <STUInt64> (name);

    case STI_AMOUNT:
        return std::make_unique <STAmount> (name);

    case STI_HASH128:
        return std::make_unique <STHash128> (name);

    case STI_HASH160:
        return std::make_unique <STHash160> (name);

    case STI_HASH256:
        return std::make_unique <STHash256> (name);

    case STI_VECTOR256:
        return std::make_unique <STVector256> (name);

    case STI_VL:
        return std::make_unique <STVariableLength> (name);

    case STI_ACCOUNT:
        return std::make_unique <STAccount> (name);

    case STI_PATHSET:
        return std::make_unique <STPathSet> (name);

    case STI_OBJECT:
        return std::make_unique <STObject> (name);

    case STI_ARRAY:
        return std::make_unique <STArray> (name);

    default:
        WriteLog (lsFATAL, STObject) <<
            "Object type: " << beast::lexicalCast <std::string> (id);
        assert (false);
        throw std::runtime_error ("Unknown object type");
    }
}

// VFALCO TODO Remove the 'depth' parameter
std::unique_ptr<SerializedType>
STObject::makeDeserializedObject (SerializedTypeID id, SField::ref name,
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

    SerializedType** array = mData.c_array();
    std::size_t count = mData.size ();

    for (auto const& elem : type.peek ())
    {
        // Loop through all the fields in the template
        bool match = false;

        for (std::size_t i = 0; i < count; ++i)
            if ((array[i] != nullptr) &&
                (array[i]->getFName () == elem->e_field))
            {
                // matching entry in the object, move to new vector
                match = true;

                if ((elem->flags == SOE_DEFAULT) && array[i]->isDefault ())
                {
                    WriteLog (lsWARNING, STObject) <<
                        "setType( " << getFName ().getName () <<
                        ") invalid default " << elem->e_field.fieldName;
                    valid = false;
                }

                newData.push_back (array[i]);
                array[i] = nullptr;
                break;
            }

        if (!match)
        {
            // no match found in the object for an entry in the template
            if (elem->flags == SOE_REQUIRED)
            {
                WriteLog (lsWARNING, STObject) <<
                    "setType( " << getFName ().getName () <<
                    ") invalid missing " << elem->e_field.fieldName;
                valid = false;
            }

            // Make a default object
            newData.push_back (makeNonPresentObject (elem->e_field).release ());
        }
    }

    for (std::size_t i = 0; i < count; ++i)
    {
        // Anything left over in the object must be discardable
        if ((array[i] != nullptr) && !array[i]->getFName ().isDiscardable ())
        {
            WriteLog (lsWARNING, STObject) <<
                "setType( " << getFName ().getName () <<
                ") invalid leftover " << array[i]->getFName ().getName ();
            valid = false;
        }
    }

    // Swap the template matching data in for the old data,
    // freeing any leftover junk
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

        if ((type == STI_ARRAY) && (field == 1))
        {
            WriteLog (lsWARNING, STObject) <<
                "Encountered object with end of array marker";
            throw std::runtime_error ("Illegal terminator in object");
        }

        if (!reachedEndOfObject)
        {
            // Figure out the field
            //
            SField::ref fn = SField::getField (type, field);

            if (fn.isInvalid ())
            {
                WriteLog (lsWARNING, STObject) <<
                    "Unknown field: field_type=" << type <<
                    ", field_name=" << field;
                throw std::runtime_error ("Unknown field");
            }

            // Unflatten the field
            //
            giveObject (
                makeDeserializedObject (fn.fieldType, fn, sit, depth + 1));
        }
    }

    return reachedEndOfObject;
}


std::unique_ptr<SerializedType>
STObject::deserialize (SerializerIterator& sit, SField::ref name)
{
    std::unique_ptr <STObject> object (std::make_unique <STObject> (name));
    object->set (sit, 1);
    return std::move (object);
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

    for (SerializedType const& elem : mData)
    {
        if (elem.getSType () != STI_NOTPRESENT)
        {
            if (!first)
                ret += ", ";
            else
                first = false;

            ret += elem.getFullText ();
        }
    }

    ret += "}";
    return ret;
}

void STObject::add (Serializer& s, bool withSigningFields) const
{
    std::map<int, const SerializedType*> fields;

    for (SerializedType const& elem : mData)
    {
        // pick out the fields and sort them
        if ((elem.getSType () != STI_NOTPRESENT) &&
            elem.getFName ().shouldInclude (withSigningFields))
        {
            fields.insert (std::make_pair (elem.getFName ().fieldCode, &elem));
        }
    }

    typedef std::map<int, const SerializedType*>::value_type field_iterator;
    for (auto const& mapEntry : fields)
    {
        // insert them in sorted order
        const SerializedType* field = mapEntry.second;

        // When we serialize an object inside another object,
        // the type associated by rule with this field name
        // must be OBJECT, or the object cannot be deserialized
        assert ((field->getSType() != STI_OBJECT) ||
            (field->getFName().fieldType == STI_OBJECT));

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
    for (SerializedType const& elem : mData)
    {
        if (!first)
        {
            ret += ", ";
            first = false;
        }

        ret += elem.getText ();
    }
    ret += "}";
    return ret;
}

bool STObject::isEquivalent (const SerializedType& t) const
{
    const STObject* v = dynamic_cast<const STObject*> (&t);

    if (!v)
    {
        WriteLog (lsDEBUG, STObject) <<
            "notEquiv " << getFullText() << " not object";
        return false;
    }

    typedef boost::ptr_vector<SerializedType>::const_iterator const_iter;
    const_iter it1 = mData.begin (), end1 = mData.end ();
    const_iter it2 = v->mData.begin (), end2 = v->mData.end ();

    while ((it1 != end1) && (it2 != end2))
    {
        if ((it1->getSType () != it2->getSType ()) || !it1->isEquivalent (*it2))
        {
            if (it1->getSType () != it2->getSType ())
            {
                WriteLog (lsDEBUG, STObject) << "notEquiv type " <<
                    it1->getFullText() << " != " <<  it2->getFullText();
            }
            else
            {
                WriteLog (lsDEBUG, STObject) << "notEquiv " <<
                     it1->getFullText() << " != " <<  it2->getFullText();
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
    for (SerializedType const& elem : mData)
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
    return getFieldByValue <STUInt8> (field);
}

std::uint16_t STObject::getFieldU16 (SField::ref field) const
{
    return getFieldByValue <STUInt16> (field);
}

std::uint32_t STObject::getFieldU32 (SField::ref field) const
{
    return getFieldByValue <STUInt32> (field);
}

std::uint64_t STObject::getFieldU64 (SField::ref field) const
{
    return getFieldByValue <STUInt64> (field);
}

uint128 STObject::getFieldH128 (SField::ref field) const
{
    return getFieldByValue <STHash128> (field);
}

uint160 STObject::getFieldH160 (SField::ref field) const
{
    return getFieldByValue <STHash160> (field);
}

uint256 STObject::getFieldH256 (SField::ref field) const
{
    return getFieldByValue <STHash256> (field);
}

RippleAddress STObject::getFieldAccount (SField::ref field) const
{
    const SerializedType* rf = peekAtPField (field);

    if (!rf)
        throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id == STI_NOTPRESENT) return RippleAddress ();

    const STAccount* cf = dynamic_cast<const STAccount*> (rf);

    if (!cf)
        throw std::runtime_error ("Wrong field type");

    return cf->getValueNCA ();
}

Account STObject::getFieldAccount160 (SField::ref field) const
{
    auto rf = peekAtPField (field);
    if (!rf)
        throw std::runtime_error ("Field not found");

    Account account;
    if (rf->getSType () != STI_NOTPRESENT)
    {
        const STAccount* cf = dynamic_cast<const STAccount*> (rf);

        if (!cf)
            throw std::runtime_error ("Wrong field type");

        cf->getValueH160 (account);
    }

    return account;
}

Blob STObject::getFieldVL (SField::ref field) const
{
    return getFieldByValue <STVariableLength> (field);
}

STAmount const& STObject::getFieldAmount (SField::ref field) const
{
    static STAmount const empty;
    return getFieldByConstRef <STAmount> (field, empty);
}

const STArray& STObject::getFieldArray (SField::ref field) const
{
    static STArray const empty;
    return getFieldByConstRef <STArray> (field, empty);
}

STPathSet const& STObject::getFieldPathSet (SField::ref field) const
{
    static STPathSet const empty{};
    return getFieldByConstRef <STPathSet> (field, empty);
}

const STVector256& STObject::getFieldV256 (SField::ref field) const
{
    static STVector256 const empty{};
    return getFieldByConstRef <STVector256> (field, empty);
}

void STObject::setFieldU8 (SField::ref field, unsigned char v)
{
    setFieldUsingSetValue <STUInt8> (field, v);
}

void STObject::setFieldU16 (SField::ref field, std::uint16_t v)
{
    setFieldUsingSetValue <STUInt16> (field, v);
}

void STObject::setFieldU32 (SField::ref field, std::uint32_t v)
{
    setFieldUsingSetValue <STUInt32> (field, v);
}

void STObject::setFieldU64 (SField::ref field, std::uint64_t v)
{
    setFieldUsingSetValue <STUInt64> (field, v);
}

void STObject::setFieldH128 (SField::ref field, uint128 const& v)
{
    setFieldUsingSetValue <STHash128> (field, v);
}

void STObject::setFieldH256 (SField::ref field, uint256 const& v)
{
    setFieldUsingSetValue <STHash256> (field, v);
}

void STObject::setFieldV256 (SField::ref field, STVector256 const& v)
{
    setFieldUsingSetValue <STVector256> (field, v);
}

void STObject::setFieldAccount (SField::ref field, Account const& v)
{
    SerializedType* rf = getPField (field, true);

    if (!rf)
        throw std::runtime_error ("Field not found");

    if (rf->getSType () == STI_NOTPRESENT)
        rf = makeFieldPresent (field);

    STAccount* cf = dynamic_cast<STAccount*> (rf);

    if (!cf)
        throw std::runtime_error ("Wrong field type");

    cf->setValueH160 (v);
}

void STObject::setFieldVL (SField::ref field, Blob const& v)
{
    setFieldUsingSetValue <STVariableLength> (field, v);
}

void STObject::setFieldAmount (SField::ref field, STAmount const& v)
{
    setFieldUsingAssignment (field, v);
}

void STObject::setFieldPathSet (SField::ref field, STPathSet const& v)
{
    setFieldUsingAssignment (field, v);
}

void STObject::setFieldArray (SField::ref field, STArray const& v)
{
    setFieldUsingAssignment (field, v);
}

Json::Value STObject::getJson (int options) const
{
    Json::Value ret (Json::objectValue);

    // TODO(tom): this variable is never changed...?
    int index = 1;
    for (auto const& it: mData)
    {
        if (it.getSType () != STI_NOTPRESENT)
        {
            auto const& n = it.getFName ();
            auto key = n.hasName () ? std::string(n.getJsonName ()) :
                    std::to_string (index);
            ret[key] = it.getJson (options);
        }
    }
    return ret;
}

bool STObject::operator== (const STObject& obj) const
{
    // This is not particularly efficient, and only compares data elements
    // with binary representations
    int matches = 0;
    for (SerializedType const& t1 : mData)
    {
        if ((t1.getSType () != STI_NOTPRESENT) && t1.getFName ().isBinary ())
        {
            // each present field must have a matching field
            bool match = false;
            for (SerializedType const& t2 : obj.mData)
            {
                if (t1.getFName () == t2.getFName ())
                {
                    if (t2 != t1)
                        return false;

                    match = true;
                    ++matches;
                    break;
                }
            }

            if (!match)
            {
                WriteLog (lsTRACE, STObject) <<
                    "STObject::operator==: no match for " <<
                    t1.getFName ().getName ();
                return false;
            }
        }
    }

    int fields = 0;
    for (SerializedType const& t2 : obj.mData)
    {
        if ((t2.getSType () != STI_NOTPRESENT) && t2.getFName ().isBinary ())
            ++fields;
    }

    if (fields != matches)
    {
        WriteLog (lsTRACE, STObject) << "STObject::operator==: " <<
            fields << " fields, " << matches << " matches";
        return false;
    }

    return true;
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

    bool parseJSONString (std::string const& json, Json::Value& to)
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
            STParsedJSONObject parsed ("test", faultyJson);
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
        std::string const json (
            "{\"Template\":[{\"ModifiedNode\":{\"Sequence\":1}}]}\n");

        Json::Value jsonObject;
        bool parsedOK (parseJSONString(json, jsonObject));
        if (parsedOK)
        {
            STParsedJSONObject parsed ("test", jsonObject);
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

        SField const& sfTestVL = SField::getField (STI_VL, 255);
        SField const& sfTestH256 = SField::getField (STI_HASH256, 255);
        SField const& sfTestU32 = SField::getField (STI_UINT32, 255);
        SField const& sfTestObject = SField::getField (STI_OBJECT, 255);

        SOTemplate elements;
        elements.push_back (SOElement (sfFlags, SOE_REQUIRED));
        elements.push_back (SOElement (sfTestVL, SOE_REQUIRED));
        elements.push_back (SOElement (sfTestH256, SOE_OPTIONAL));
        elements.push_back (SOElement (sfTestU32, SOE_REQUIRED));

        STObject object1 (elements, sfTestObject);
        STObject object2 (object1);

        unexpected (object1.getSerializer () != object2.getSerializer (),
            "STObject error 1");

        unexpected (object1.isFieldPresent (sfTestH256) ||
            !object1.isFieldPresent (sfTestVL), "STObject error");

        object1.makeFieldPresent (sfTestH256);

        unexpected (!object1.isFieldPresent (sfTestH256), "STObject Error 2");

        unexpected (object1.getFieldH256 (sfTestH256) != uint256 (),
            "STObject error 3");

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

        unexpected (object1.getSerializer () != object2.getSerializer (),
            "STObject error 7");

        STObject copy (object1);

        unexpected (object1.isFieldPresent (sfTestH256), "STObject error 8");

        unexpected (copy.isFieldPresent (sfTestH256), "STObject error 9");

        unexpected (object1.getSerializer () != copy.getSerializer (),
            "STObject error 10");

        copy.setFieldU32 (sfTestU32, 1);

        unexpected (object1.getSerializer () == copy.getSerializer (),
            "STObject error 11");

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
