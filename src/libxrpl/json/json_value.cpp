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

#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/detail/json_assert.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/json_writer.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

namespace Json {

Value const Value::null;
Int const Value::minInt = Int(~(UInt(-1) / 2));
Int const Value::maxInt = Int(UInt(-1) / 2);
UInt const Value::maxUInt = UInt(-1);

class DefaultValueAllocator : public ValueAllocator
{
public:
    virtual ~DefaultValueAllocator() = default;

    char*
    makeMemberName(char const* memberName) override
    {
        return duplicateStringValue(memberName);
    }

    void
    releaseMemberName(char* memberName) override
    {
        releaseStringValue(memberName);
    }

    char*
    duplicateStringValue(char const* value, unsigned int length = unknown)
        override
    {
        //@todo investigate this old optimization
        // if ( !value  ||  value[0] == 0 )
        //   return 0;

        if (length == unknown)
            length = value ? (unsigned int)strlen(value) : 0;

        char* newString = static_cast<char*>(malloc(length + 1));
        if (value)
            memcpy(newString, value, length);
        newString[length] = 0;
        return newString;
    }

    void
    releaseStringValue(char* value) override
    {
        if (value)
            free(value);
    }
};

static ValueAllocator*&
valueAllocator()
{
    static ValueAllocator* valueAllocator = new DefaultValueAllocator;
    return valueAllocator;
}

static struct DummyValueAllocatorInitializer
{
    DummyValueAllocatorInitializer()
    {
        valueAllocator();  // ensure valueAllocator() statics are initialized
                           // before main().
    }
} dummyValueAllocatorInitializer;

// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// class Value::CZString
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////

// Notes: index_ indicates if the string was allocated when
// a string is stored.

Value::CZString::CZString(int index) : cstr_(0), index_(index)
{
}

Value::CZString::CZString(char const* cstr, DuplicationPolicy allocate)
    : cstr_(
          allocate == duplicate ? valueAllocator()->makeMemberName(cstr) : cstr)
    , index_(allocate)
{
}

Value::CZString::CZString(CZString const& other)
    : cstr_(
          other.index_ != noDuplication && other.cstr_ != 0
              ? valueAllocator()->makeMemberName(other.cstr_)
              : other.cstr_)
    , index_(
          other.cstr_
              ? (other.index_ == noDuplication ? noDuplication : duplicate)
              : other.index_)
{
}

Value::CZString::~CZString()
{
    if (cstr_ && index_ == duplicate)
        valueAllocator()->releaseMemberName(const_cast<char*>(cstr_));
}

bool
Value::CZString::operator<(CZString const& other) const
{
    if (cstr_ && other.cstr_)
        return strcmp(cstr_, other.cstr_) < 0;

    return index_ < other.index_;
}

bool
Value::CZString::operator==(CZString const& other) const
{
    if (cstr_ && other.cstr_)
        return strcmp(cstr_, other.cstr_) == 0;

    return index_ == other.index_;
}

int
Value::CZString::index() const
{
    return index_;
}

char const*
Value::CZString::c_str() const
{
    return cstr_;
}

bool
Value::CZString::isStaticString() const
{
    return index_ == noDuplication;
}

// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// class Value::Value
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////

/*! \internal Default constructor initialization must be equivalent to:
 * memset( this, 0, sizeof(Value) )
 * This optimization is used in ValueInternalMap fast allocator.
 */
Value::Value(ValueType type) : type_(type), allocated_(0)
{
    switch (type)
    {
        case nullValue:
            break;

        case intValue:
        case uintValue:
            value_.int_ = 0;
            break;

        case realValue:
            value_.real_ = 0.0;
            break;

        case stringValue:
            value_.string_ = 0;
            break;

        case arrayValue:
        case objectValue:
            value_.map_ = new ObjectValues();
            break;

        case booleanValue:
            value_.bool_ = false;
            break;

        default:
            UNREACHABLE("Json::Value::Value(ValueType) : invalid type");
    }
}

Value::Value(Int value) : type_(intValue)
{
    value_.int_ = value;
}

Value::Value(UInt value) : type_(uintValue)
{
    value_.uint_ = value;
}

Value::Value(double value) : type_(realValue)
{
    value_.real_ = value;
}

Value::Value(char const* value) : type_(stringValue), allocated_(true)
{
    value_.string_ = valueAllocator()->duplicateStringValue(value);
}

Value::Value(ripple::Number const& value) : type_(stringValue), allocated_(true)
{
    auto const tmp = to_string(value);
    value_.string_ =
        valueAllocator()->duplicateStringValue(tmp.c_str(), tmp.length());
}

Value::Value(std::string const& value) : type_(stringValue), allocated_(true)
{
    value_.string_ = valueAllocator()->duplicateStringValue(
        value.c_str(), (unsigned int)value.length());
}

Value::Value(StaticString const& value) : type_(stringValue), allocated_(false)
{
    value_.string_ = const_cast<char*>(value.c_str());
}

Value::Value(bool value) : type_(booleanValue)
{
    value_.bool_ = value;
}

Value::Value(Value const& other) : type_(other.type_)
{
    switch (type_)
    {
        case nullValue:
        case intValue:
        case uintValue:
        case realValue:
        case booleanValue:
            value_ = other.value_;
            break;

        case stringValue:
            if (other.value_.string_)
            {
                value_.string_ = valueAllocator()->duplicateStringValue(
                    other.value_.string_);
                allocated_ = true;
            }
            else
                value_.string_ = 0;

            break;

        case arrayValue:
        case objectValue:
            value_.map_ = new ObjectValues(*other.value_.map_);
            break;

        default:
            UNREACHABLE("Json::Value::Value(Value const&) : invalid type");
    }
}

Value::~Value()
{
    switch (type_)
    {
        case nullValue:
        case intValue:
        case uintValue:
        case realValue:
        case booleanValue:
            break;

        case stringValue:
            if (allocated_)
                valueAllocator()->releaseStringValue(value_.string_);

            break;

        case arrayValue:
        case objectValue:
            if (value_.map_)
                delete value_.map_;
            break;

        default:
            UNREACHABLE("Json::Value::~Value : invalid type");
    }
}

Value&
Value::operator=(Value const& other)
{
    Value tmp(other);
    swap(tmp);
    return *this;
}

Value::Value(Value&& other) noexcept
    : value_(other.value_), type_(other.type_), allocated_(other.allocated_)
{
    other.type_ = nullValue;
    other.allocated_ = 0;
}

Value&
Value::operator=(Value&& other)
{
    Value tmp(std::move(other));
    swap(tmp);
    return *this;
}

void
Value::swap(Value& other) noexcept
{
    std::swap(value_, other.value_);

    ValueType temp = type_;
    type_ = other.type_;
    other.type_ = temp;

    int temp2 = allocated_;
    allocated_ = other.allocated_;
    other.allocated_ = temp2;
}

ValueType
Value::type() const
{
    return type_;
}

static int
integerCmp(Int i, UInt ui)
{
    // All negative numbers are less than all unsigned numbers.
    if (i < 0)
        return -1;

    // Now we can safely compare.
    return (i < ui) ? -1 : (i == ui) ? 0 : 1;
}

bool
operator<(Value const& x, Value const& y)
{
    if (auto signum = x.type_ - y.type_)
    {
        if (x.type_ == intValue && y.type_ == uintValue)
            signum = integerCmp(x.value_.int_, y.value_.uint_);
        else if (x.type_ == uintValue && y.type_ == intValue)
            signum = -integerCmp(y.value_.int_, x.value_.uint_);
        return signum < 0;
    }

    switch (x.type_)
    {
        case nullValue:
            return false;

        case intValue:
            return x.value_.int_ < y.value_.int_;

        case uintValue:
            return x.value_.uint_ < y.value_.uint_;

        case realValue:
            return x.value_.real_ < y.value_.real_;

        case booleanValue:
            return x.value_.bool_ < y.value_.bool_;

        case stringValue:
            return (x.value_.string_ == 0 && y.value_.string_) ||
                (y.value_.string_ && x.value_.string_ &&
                 strcmp(x.value_.string_, y.value_.string_) < 0);

        case arrayValue:
        case objectValue: {
            if (int signum = int(x.value_.map_->size()) - y.value_.map_->size())
                return signum < 0;

            return *x.value_.map_ < *y.value_.map_;
        }

        default:
            UNREACHABLE("Json::operator<(Value, Value) : invalid type");
    }

    return 0;  // unreachable
}

bool
operator==(Value const& x, Value const& y)
{
    if (x.type_ != y.type_)
    {
        if (x.type_ == intValue && y.type_ == uintValue)
            return !integerCmp(x.value_.int_, y.value_.uint_);
        if (x.type_ == uintValue && y.type_ == intValue)
            return !integerCmp(y.value_.int_, x.value_.uint_);
        return false;
    }

    switch (x.type_)
    {
        case nullValue:
            return true;

        case intValue:
            return x.value_.int_ == y.value_.int_;

        case uintValue:
            return x.value_.uint_ == y.value_.uint_;

        case realValue:
            return x.value_.real_ == y.value_.real_;

        case booleanValue:
            return x.value_.bool_ == y.value_.bool_;

        case stringValue:
            return x.value_.string_ == y.value_.string_ ||
                (y.value_.string_ && x.value_.string_ &&
                 !strcmp(x.value_.string_, y.value_.string_));

        case arrayValue:
        case objectValue:
            return x.value_.map_->size() == y.value_.map_->size() &&
                *x.value_.map_ == *y.value_.map_;

        default:
            UNREACHABLE("Json::operator==(Value, Value) : invalid type");
    }

    return 0;  // unreachable
}

char const*
Value::asCString() const
{
    XRPL_ASSERT(type_ == stringValue, "Json::Value::asCString : valid type");
    return value_.string_;
}

std::string
Value::asString() const
{
    switch (type_)
    {
        case nullValue:
            return "";

        case stringValue:
            return value_.string_ ? value_.string_ : "";

        case booleanValue:
            return value_.bool_ ? "true" : "false";

        case intValue:
            return std::to_string(value_.int_);

        case uintValue:
            return std::to_string(value_.uint_);

        case realValue:
            return std::to_string(value_.real_);

        case arrayValue:
        case objectValue:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to string");

        default:
            UNREACHABLE("Json::Value::asString : invalid type");
    }

    return "";  // unreachable
}

Value::Int
Value::asInt() const
{
    switch (type_)
    {
        case nullValue:
            return 0;

        case intValue:
            return value_.int_;

        case uintValue:
            JSON_ASSERT_MESSAGE(
                value_.uint_ < (unsigned)maxInt,
                "integer out of signed integer range");
            return value_.uint_;

        case realValue:
            JSON_ASSERT_MESSAGE(
                value_.real_ >= minInt && value_.real_ <= maxInt,
                "Real out of signed integer range");
            return Int(value_.real_);

        case booleanValue:
            return value_.bool_ ? 1 : 0;

        case stringValue: {
            char const* const str{value_.string_ ? value_.string_ : ""};
            return beast::lexicalCastThrow<int>(str);
        }

        case arrayValue:
        case objectValue:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to int");

        default:
            UNREACHABLE("Json::Value::asInt : invalid type");
    }

    return 0;  // unreachable;
}

Value::UInt
Value::asUInt() const
{
    switch (type_)
    {
        case nullValue:
            return 0;

        case intValue:
            JSON_ASSERT_MESSAGE(
                value_.int_ >= 0,
                "Negative integer can not be converted to unsigned integer");
            return value_.int_;

        case uintValue:
            return value_.uint_;

        case realValue:
            JSON_ASSERT_MESSAGE(
                value_.real_ >= 0 && value_.real_ <= maxUInt,
                "Real out of unsigned integer range");
            return UInt(value_.real_);

        case booleanValue:
            return value_.bool_ ? 1 : 0;

        case stringValue: {
            char const* const str{value_.string_ ? value_.string_ : ""};
            return beast::lexicalCastThrow<unsigned int>(str);
        }

        case arrayValue:
        case objectValue:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to uint");

        default:
            UNREACHABLE("Json::Value::asUInt : invalid type");
    }

    return 0;  // unreachable;
}

double
Value::asDouble() const
{
    switch (type_)
    {
        case nullValue:
            return 0.0;

        case intValue:
            return value_.int_;

        case uintValue:
            return value_.uint_;

        case realValue:
            return value_.real_;

        case booleanValue:
            return value_.bool_ ? 1.0 : 0.0;

        case stringValue:
        case arrayValue:
        case objectValue:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to double");

        default:
            UNREACHABLE("Json::Value::asDouble : invalid type");
    }

    return 0;  // unreachable;
}

bool
Value::asBool() const
{
    switch (type_)
    {
        case nullValue:
            return false;

        case intValue:
        case uintValue:
            return value_.int_ != 0;

        case realValue:
            return value_.real_ != 0.0;

        case booleanValue:
            return value_.bool_;

        case stringValue:
            return value_.string_ && value_.string_[0] != 0;

        case arrayValue:
        case objectValue:
            return value_.map_->size() != 0;

        default:
            UNREACHABLE("Json::Value::asBool : invalid type");
    }

    return false;  // unreachable;
}

bool
Value::isConvertibleTo(ValueType other) const
{
    switch (type_)
    {
        case nullValue:
            return true;

        case intValue:
            return (other == nullValue && value_.int_ == 0) ||
                other == intValue || (other == uintValue && value_.int_ >= 0) ||
                other == realValue || other == stringValue ||
                other == booleanValue;

        case uintValue:
            return (other == nullValue && value_.uint_ == 0) ||
                (other == intValue && value_.uint_ <= (unsigned)maxInt) ||
                other == uintValue || other == realValue ||
                other == stringValue || other == booleanValue;

        case realValue:
            return (other == nullValue && value_.real_ == 0.0) ||
                (other == intValue && value_.real_ >= minInt &&
                 value_.real_ <= maxInt) ||
                (other == uintValue && value_.real_ >= 0 &&
                 value_.real_ <= maxUInt) ||
                other == realValue || other == stringValue ||
                other == booleanValue;

        case booleanValue:
            return (other == nullValue && value_.bool_ == false) ||
                other == intValue || other == uintValue || other == realValue ||
                other == stringValue || other == booleanValue;

        case stringValue:
            return other == stringValue ||
                (other == nullValue &&
                 (!value_.string_ || value_.string_[0] == 0));

        case arrayValue:
            return other == arrayValue ||
                (other == nullValue && value_.map_->size() == 0);

        case objectValue:
            return other == objectValue ||
                (other == nullValue && value_.map_->size() == 0);

        default:
            UNREACHABLE("Json::Value::isConvertible : invalid type");
    }

    return false;  // unreachable;
}

/// Number of values in array or object
Value::UInt
Value::size() const
{
    switch (type_)
    {
        case nullValue:
        case intValue:
        case uintValue:
        case realValue:
        case booleanValue:
        case stringValue:
            return 0;

        case arrayValue:  // size of the array is highest index + 1
            if (!value_.map_->empty())
            {
                ObjectValues::const_iterator itLast = value_.map_->end();
                --itLast;
                return (*itLast).first.index() + 1;
            }

            return 0;

        case objectValue:
            return Int(value_.map_->size());

        default:
            UNREACHABLE("Json::Value::size : invalid type");
    }

    return 0;  // unreachable;
}

Value::operator bool() const
{
    if (isNull())
        return false;

    if (isString())
    {
        auto s = asCString();
        return s && s[0];
    }

    return !(isArray() || isObject()) || size();
}

void
Value::clear()
{
    XRPL_ASSERT(
        type_ == nullValue || type_ == arrayValue || type_ == objectValue,
        "Json::Value::clear : valid type");

    switch (type_)
    {
        case arrayValue:
        case objectValue:
            value_.map_->clear();
            break;

        default:
            break;
    }
}

Value&
Value::operator[](UInt index)
{
    XRPL_ASSERT(
        type_ == nullValue || type_ == arrayValue,
        "Json::Value::operator[](UInt) : valid type");

    if (type_ == nullValue)
        *this = Value(arrayValue);

    CZString key(index);
    ObjectValues::iterator it = value_.map_->lower_bound(key);

    if (it != value_.map_->end() && (*it).first == key)
        return (*it).second;

    ObjectValues::value_type defaultValue(key, null);
    it = value_.map_->insert(it, defaultValue);
    return (*it).second;
}

Value const&
Value::operator[](UInt index) const
{
    XRPL_ASSERT(
        type_ == nullValue || type_ == arrayValue,
        "Json::Value::operator[](UInt) const : valid type");

    if (type_ == nullValue)
        return null;

    CZString key(index);
    ObjectValues::const_iterator it = value_.map_->find(key);

    if (it == value_.map_->end())
        return null;

    return (*it).second;
}

Value&
Value::operator[](char const* key)
{
    return resolveReference(key, false);
}

Value&
Value::resolveReference(char const* key, bool isStatic)
{
    XRPL_ASSERT(
        type_ == nullValue || type_ == objectValue,
        "Json::Value::resolveReference : valid type");

    if (type_ == nullValue)
        *this = Value(objectValue);

    CZString actualKey(
        key, isStatic ? CZString::noDuplication : CZString::duplicateOnCopy);
    ObjectValues::iterator it = value_.map_->lower_bound(actualKey);

    if (it != value_.map_->end() && (*it).first == actualKey)
        return (*it).second;

    ObjectValues::value_type defaultValue(actualKey, null);
    it = value_.map_->insert(it, defaultValue);
    Value& value = (*it).second;
    return value;
}

Value
Value::get(UInt index, Value const& defaultValue) const
{
    Value const* value = &((*this)[index]);
    return value == &null ? defaultValue : *value;
}

bool
Value::isValidIndex(UInt index) const
{
    return index < size();
}

Value const&
Value::operator[](char const* key) const
{
    XRPL_ASSERT(
        type_ == nullValue || type_ == objectValue,
        "Json::Value::operator[](const char*) const : valid type");

    if (type_ == nullValue)
        return null;

    CZString actualKey(key, CZString::noDuplication);
    ObjectValues::const_iterator it = value_.map_->find(actualKey);

    if (it == value_.map_->end())
        return null;

    return (*it).second;
}

Value&
Value::operator[](std::string const& key)
{
    return (*this)[key.c_str()];
}

Value const&
Value::operator[](std::string const& key) const
{
    return (*this)[key.c_str()];
}

Value&
Value::operator[](StaticString const& key)
{
    return resolveReference(key, true);
}

Value const&
Value::operator[](StaticString const& key) const
{
    return (*this)[key.c_str()];
}

Value&
Value::append(Value const& value)
{
    return (*this)[size()] = value;
}

Value&
Value::append(Value&& value)
{
    return (*this)[size()] = std::move(value);
}

Value
Value::get(char const* key, Value const& defaultValue) const
{
    Value const* value = &((*this)[key]);
    return value == &null ? defaultValue : *value;
}

Value
Value::get(std::string const& key, Value const& defaultValue) const
{
    return get(key.c_str(), defaultValue);
}

Value
Value::removeMember(char const* key)
{
    XRPL_ASSERT(
        type_ == nullValue || type_ == objectValue,
        "Json::Value::removeMember : valid type");

    if (type_ == nullValue)
        return null;

    CZString actualKey(key, CZString::noDuplication);
    ObjectValues::iterator it = value_.map_->find(actualKey);

    if (it == value_.map_->end())
        return null;

    Value old(it->second);
    value_.map_->erase(it);
    return old;
}

Value
Value::removeMember(std::string const& key)
{
    return removeMember(key.c_str());
}

bool
Value::isMember(char const* key) const
{
    if (type_ != objectValue)
        return false;

    Value const* value = &((*this)[key]);
    return value != &null;
}

bool
Value::isMember(std::string const& key) const
{
    return isMember(key.c_str());
}

Value::Members
Value::getMemberNames() const
{
    XRPL_ASSERT(
        type_ == nullValue || type_ == objectValue,
        "Json::Value::getMemberNames : valid type");

    if (type_ == nullValue)
        return Value::Members();

    Members members;
    members.reserve(value_.map_->size());
    ObjectValues::const_iterator it = value_.map_->begin();
    ObjectValues::const_iterator itEnd = value_.map_->end();

    for (; it != itEnd; ++it)
        members.push_back(std::string((*it).first.c_str()));

    return members;
}

bool
Value::isNull() const
{
    return type_ == nullValue;
}

bool
Value::isBool() const
{
    return type_ == booleanValue;
}

bool
Value::isInt() const
{
    return type_ == intValue;
}

bool
Value::isUInt() const
{
    return type_ == uintValue;
}

bool
Value::isIntegral() const
{
    return type_ == intValue || type_ == uintValue || type_ == booleanValue;
}

bool
Value::isDouble() const
{
    return type_ == realValue;
}

bool
Value::isNumeric() const
{
    return isIntegral() || isDouble();
}

bool
Value::isString() const
{
    return type_ == stringValue;
}

bool
Value::isArray() const
{
    return type_ == arrayValue;
}

bool
Value::isArrayOrNull() const
{
    return type_ == nullValue || type_ == arrayValue;
}

bool
Value::isObject() const
{
    return type_ == objectValue;
}

bool
Value::isObjectOrNull() const
{
    return type_ == nullValue || type_ == objectValue;
}

std::string
Value::toStyledString() const
{
    StyledWriter writer;
    return writer.write(*this);
}

Value::const_iterator
Value::begin() const
{
    switch (type_)
    {
        case arrayValue:
        case objectValue:
            if (value_.map_)
                return const_iterator(value_.map_->begin());

            break;
        default:
            break;
    }

    return const_iterator();
}

Value::const_iterator
Value::end() const
{
    switch (type_)
    {
        case arrayValue:
        case objectValue:
            if (value_.map_)
                return const_iterator(value_.map_->end());

            break;
        default:
            break;
    }

    return const_iterator();
}

Value::iterator
Value::begin()
{
    switch (type_)
    {
        case arrayValue:
        case objectValue:
            if (value_.map_)
                return iterator(value_.map_->begin());
            break;
        default:
            break;
    }

    return iterator();
}

Value::iterator
Value::end()
{
    switch (type_)
    {
        case arrayValue:
        case objectValue:
            if (value_.map_)
                return iterator(value_.map_->end());
            break;
        default:
            break;
    }

    return iterator();
}

}  // namespace Json
