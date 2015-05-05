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
#include <ripple/basics/Log.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/STBase.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/STParsedJSON.h>
#include <beast/module/core/text/LexicalCast.h>
#include <beast/cxx14/memory.h> // <memory>

namespace ripple {

STObject::~STObject()
{
#if 0
    // Turn this on to get a histogram on exit
    static beast::static_initializer<Log> log;
    (*log)(v_.size());
#endif
}

STObject::STObject(STObject&& other)
    : STBase(other.getFName())
    , v_(std::move(other.v_))
    , mType(other.mType)
{
}

STObject::STObject (SField const& name)
    : STBase (name)
    , mType (nullptr)
{
    // VFALCO TODO See if this is the right thing to do
    //v_.reserve(reserveSize);
}

STObject::STObject (SOTemplate const& type,
        SField const& name)
    : STBase (name)
{
    set (type);
}

STObject::STObject (SOTemplate const& type,
        SerialIter & sit, SField const& name)
    : STBase (name)
{
    v_.reserve(type.peek().size());
    set (sit);
    setType (type);
}

STObject::STObject (SerialIter& sit, SField const& name)
    : STBase(name)
    , mType(nullptr)
{
    set(sit, 0);
}

STObject&
STObject::operator= (STObject&& other)
{
    setFName(other.getFName());
    mType = other.mType;
    v_ = std::move(other.v_);
    return *this;
}

void STObject::set (const SOTemplate& type)
{
    v_.clear();
    v_.reserve(type.peek().size());
    mType = &type;

    for (auto const& elem : type.peek())
    {
        if (elem->flags != SOE_REQUIRED)
            v_.emplace_back(detail::nonPresentObject, elem->e_field);
        else
            v_.emplace_back(detail::defaultObject, elem->e_field);
    }
}

bool STObject::setType (const SOTemplate& type)
{
    bool valid = true;
    mType = &type;
    decltype(v_) v;
    v.reserve(type.peek().size());
    for (auto const& e : type.peek())
    {
        auto const iter = std::find_if(
            v_.begin(), v_.end(), [&](detail::STVar const& b)
                { return b.get().getFName() == e->e_field; });
        if (iter != v_.end())
        {
            if ((e->flags == SOE_DEFAULT) && iter->get().isDefault())
            {
                WriteLog (lsWARNING, STObject) <<
                    "setType( " << getFName ().getName () <<
                    ") invalid default " << e->e_field.fieldName;
                valid = false;
            }
            v.emplace_back(std::move(*iter));
            v_.erase(iter);
        }
        else
        {
            if (e->flags == SOE_REQUIRED)
            {
                WriteLog (lsWARNING, STObject) <<
                    "setType( " << getFName ().getName () <<
                    ") invalid missing " << e->e_field.fieldName;
                valid = false;
            }
            v.emplace_back(detail::nonPresentObject, e->e_field);
        }
    }
    for (auto const& e : v_)
    {
        // Anything left over in the object must be discardable
        if (! e->getFName().isDiscardable())
        {
            WriteLog (lsWARNING, STObject) <<
                "setType( " << getFName ().getName () <<
                ") invalid leftover " << e->getFName ().getName ();
            valid = false;
        }
    }
    // Swap the template matching data in for the old data,
    // freeing any leftover junk
    v_.swap(v);
    return valid;
}

bool STObject::isValidForType ()
{
    auto it = v_.begin();
    for (SOTemplate::value_type const& elem : mType->peek())
    {
        if (it == v_.end())
            return false;
        if (elem->e_field != it->get().getFName())
            return false;
        ++it;
    }
    return true;
}

bool STObject::isFieldAllowed (SField const& field)
{
    if (mType == nullptr)
        return true;

    return mType->getIndex (field) != -1;
}

// return true = terminated with end-of-object
bool STObject::set (SerialIter& sit, int depth)
{
    bool reachedEndOfObject = false;

    v_.clear();

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
            auto const& fn = SField::getField (type, field);

            if (fn.isInvalid ())
            {
                WriteLog (lsWARNING, STObject) <<
                    "Unknown field: field_type=" << type <<
                    ", field_name=" << field;
                throw std::runtime_error ("Unknown field");
            }

            // Unflatten the field
            v_.emplace_back(sit, fn);
        }
    }

    return reachedEndOfObject;
}

bool STObject::hasMatchingEntry (const STBase& t)
{
    const STBase* o = peekAtPField (t.getFName ());

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

    for (auto const& elem : v_)
    {
        if (elem->getSType () != STI_NOTPRESENT)
        {
            if (!first)
                ret += ", ";
            else
                first = false;

            ret += elem->getFullText ();
        }
    }

    ret += "}";
    return ret;
}

void STObject::add (Serializer& s, bool withSigningFields) const
{
    std::map<int, STBase const*> fields;
    for (auto const& e : v_)
    {
        // pick out the fields and sort them
        if ((e->getSType() != STI_NOTPRESENT) &&
            e->getFName().shouldInclude (withSigningFields))
        {
            fields.insert (std::make_pair (
                e->getFName().fieldCode, &e.get()));
        }
    }

    // insert sorted
    for (auto const& e : fields)
    {
        auto const field = e.second;

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
    for (auto const& elem : v_)
    {
        if (! first)
        {
            ret += ", ";
            first = false;
        }

        ret += elem->getText ();
    }
    ret += "}";
    return ret;
}

bool STObject::isEquivalent (const STBase& t) const
{
    const STObject* v = dynamic_cast<const STObject*> (&t);

    if (!v)
    {
        WriteLog (lsDEBUG, STObject) <<
            "notEquiv " << getFullText() << " not object";
        return false;
    }

    auto it1 = v_.begin (), end1 = v_.end ();
    auto it2 = v->v_.begin (), end2 = v->v_.end ();

    while ((it1 != end1) && (it2 != end2))
    {
        if ((it1->get().getSType () != it2->get().getSType ()) ||
            !it1->get().isEquivalent (it2->get()))
        {
            if (it1->get().getSType () != it2->get().getSType ())
            {
                WriteLog (lsDEBUG, STObject) << "notEquiv type " <<
                    it1->get().getFullText() << " != " <<  it2->get().getFullText();
            }
            else
            {
                WriteLog (lsDEBUG, STObject) << "notEquiv " <<
                     it1->get().getFullText() << " != " <<  it2->get().getFullText();
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

int STObject::getFieldIndex (SField const& field) const
{
    if (mType != nullptr)
        return mType->getIndex (field);

    int i = 0;
    for (auto const& elem : v_)
    {
        if (elem->getFName () == field)
            return i;
        ++i;
    }
    return -1;
}

const STBase& STObject::peekAtField (SField const& field) const
{
    int index = getFieldIndex (field);

    if (index == -1)
        throw std::runtime_error ("Field not found");

    return peekAtIndex (index);
}

STBase& STObject::getField (SField const& field)
{
    int index = getFieldIndex (field);

    if (index == -1)
        throw std::runtime_error ("Field not found");

    return getIndex (index);
}

SField const&
STObject::getFieldSType (int index) const
{
    return v_[index]->getFName ();
}

const STBase* STObject::peekAtPField (SField const& field) const
{
    int index = getFieldIndex (field);

    if (index == -1)
        return nullptr;

    return peekAtPIndex (index);
}

STBase* STObject::getPField (SField const& field, bool createOkay)
{
    int index = getFieldIndex (field);

    if (index == -1)
    {
        if (createOkay && isFree ())
            return getPIndex(emplace_back(detail::defaultObject, field));

        return nullptr;
    }

    return getPIndex (index);
}

bool STObject::isFieldPresent (SField const& field) const
{
    int index = getFieldIndex (field);

    if (index == -1)
        return false;

    return peekAtIndex (index).getSType () != STI_NOTPRESENT;
}

STObject& STObject::peekFieldObject (SField const& field)
{
    STBase* rf = getPField (field, true);

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

STBase* STObject::makeFieldPresent (SField const& field)
{
    int index = getFieldIndex (field);

    if (index == -1)
    {
        if (!isFree ())
            throw std::runtime_error ("Field not found");

        return getPIndex (emplace_back(detail::nonPresentObject, field));
    }

    STBase* f = getPIndex (index);

    if (f->getSType () != STI_NOTPRESENT)
        return f;

    v_[index] = detail::STVar(
        detail::defaultObject, f->getFName());
    return getPIndex (index);
}

void STObject::makeFieldAbsent (SField const& field)
{
    int index = getFieldIndex (field);

    if (index == -1)
        throw std::runtime_error ("Field not found");

    const STBase& f = peekAtIndex (index);

    if (f.getSType () == STI_NOTPRESENT)
        return;
    v_[index] = detail::STVar(
        detail::nonPresentObject, f.getFName());
}

bool STObject::delField (SField const& field)
{
    int index = getFieldIndex (field);

    if (index == -1)
        return false;

    delField (index);
    return true;
}

void STObject::delField (int index)
{
    v_.erase (v_.begin () + index);
}

std::string STObject::getFieldString (SField const& field) const
{
    const STBase* rf = peekAtPField (field);

    if (!rf) throw std::runtime_error ("Field not found");

    return rf->getText ();
}

unsigned char STObject::getFieldU8 (SField const& field) const
{
    return getFieldByValue <STUInt8> (field);
}

std::uint16_t STObject::getFieldU16 (SField const& field) const
{
    return getFieldByValue <STUInt16> (field);
}

std::uint32_t STObject::getFieldU32 (SField const& field) const
{
    return getFieldByValue <STUInt32> (field);
}

std::uint64_t STObject::getFieldU64 (SField const& field) const
{
    return getFieldByValue <STUInt64> (field);
}

uint128 STObject::getFieldH128 (SField const& field) const
{
    return getFieldByValue <STHash128> (field);
}

uint160 STObject::getFieldH160 (SField const& field) const
{
    return getFieldByValue <STHash160> (field);
}

uint256 STObject::getFieldH256 (SField const& field) const
{
    return getFieldByValue <STHash256> (field);
}

RippleAddress STObject::getFieldAccount (SField const& field) const
{
    const STBase* rf = peekAtPField (field);

    if (!rf)
        throw std::runtime_error ("Field not found");

    SerializedTypeID id = rf->getSType ();

    if (id == STI_NOTPRESENT) return RippleAddress ();

    const STAccount* cf = dynamic_cast<const STAccount*> (rf);

    if (!cf)
        throw std::runtime_error ("Wrong field type");

    return cf->getValueNCA ();
}

Account STObject::getFieldAccount160 (SField const& field) const
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

Blob STObject::getFieldVL (SField const& field) const
{
    STBlob empty;
    STBlob const& b = getFieldByConstRef <STBlob> (field, empty);
    return Blob (b.data (), b.data () + b.size ());
}

STAmount const& STObject::getFieldAmount (SField const& field) const
{
    static STAmount const empty{};
    return getFieldByConstRef <STAmount> (field, empty);
}

const STArray& STObject::getFieldArray (SField const& field) const
{
    static STArray const empty{};
    return getFieldByConstRef <STArray> (field, empty);
}

STPathSet const& STObject::getFieldPathSet (SField const& field) const
{
    static STPathSet const empty{};
    return getFieldByConstRef <STPathSet> (field, empty);
}

const STVector256& STObject::getFieldV256 (SField const& field) const
{
    static STVector256 const empty{};
    return getFieldByConstRef <STVector256> (field, empty);
}

void
STObject::set (std::unique_ptr<STBase> v)
{
    auto const i =
        getFieldIndex(v->getFName());
    if (i != -1)
    {
        v_[i] = std::move(*v);
    }
    else
    {
        if (! isFree())
            throw std::runtime_error(
                "missing field in templated STObject");
        v_.emplace_back(std::move(*v));
    }
}

void STObject::setFieldU8 (SField const& field, unsigned char v)
{
    setFieldUsingSetValue <STUInt8> (field, v);
}

void STObject::setFieldU16 (SField const& field, std::uint16_t v)
{
    setFieldUsingSetValue <STUInt16> (field, v);
}

void STObject::setFieldU32 (SField const& field, std::uint32_t v)
{
    setFieldUsingSetValue <STUInt32> (field, v);
}

void STObject::setFieldU64 (SField const& field, std::uint64_t v)
{
    setFieldUsingSetValue <STUInt64> (field, v);
}

void STObject::setFieldH128 (SField const& field, uint128 const& v)
{
    setFieldUsingSetValue <STHash128> (field, v);
}

void STObject::setFieldH256 (SField const& field, uint256 const& v)
{
    setFieldUsingSetValue <STHash256> (field, v);
}

void STObject::setFieldV256 (SField const& field, STVector256 const& v)
{
    setFieldUsingSetValue <STVector256> (field, v);
}

void STObject::setFieldAccount (SField const& field, Account const& v)
{
    STBase* rf = getPField (field, true);

    if (!rf)
        throw std::runtime_error ("Field not found");

    if (rf->getSType () == STI_NOTPRESENT)
        rf = makeFieldPresent (field);

    STAccount* cf = dynamic_cast<STAccount*> (rf);

    if (!cf)
        throw std::runtime_error ("Wrong field type");

    cf->setValueH160 (v);
}

void STObject::setFieldVL (SField const& field, Blob const& v)
{
    setFieldUsingSetValue <STBlob>
            (field, Buffer(v.data (), v.size ()));
}

void STObject::setFieldAmount (SField const& field, STAmount const& v)
{
    setFieldUsingAssignment (field, v);
}

void STObject::setFieldPathSet (SField const& field, STPathSet const& v)
{
    setFieldUsingAssignment (field, v);
}

void STObject::setFieldArray (SField const& field, STArray const& v)
{
    setFieldUsingAssignment (field, v);
}

Json::Value STObject::getJson (int options) const
{
    Json::Value ret (Json::objectValue);

    // TODO(tom): this variable is never changed...?
    int index = 1;
    for (auto const& elem : v_)
    {
        if (elem->getSType () != STI_NOTPRESENT)
        {
            auto const& n = elem->getFName ();
            auto key = n.hasName () ? std::string(n.getJsonName ()) :
                    std::to_string (index);
            ret[key] = elem->getJson (options);
        }
    }
    return ret;
}

bool STObject::operator== (const STObject& obj) const
{
    // This is not particularly efficient, and only compares data elements
    // with binary representations
    int matches = 0;
    for (auto const& t1 : v_)
    {
        if ((t1->getSType () != STI_NOTPRESENT) && t1->getFName ().isBinary ())
        {
            // each present field must have a matching field
            bool match = false;
            for (auto const& t2 : obj.v_)
            {
                if (t1->getFName () == t2->getFName ())
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
                    t1->getFName ().getName ();
                return false;
            }
        }
    }

    int fields = 0;
    for (auto const& t2 : obj.v_)
    {
        if ((t2->getSType () != STI_NOTPRESENT) && t2->getFName ().isBinary ())
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

} // ripple
