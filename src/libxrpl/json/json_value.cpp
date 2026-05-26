#include <xrpl/json/json_value.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/detail/json_assert.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_writer.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace json {

Value const Value::kNull;

class DefaultValueAllocator : public ValueAllocator
{
public:
    ~DefaultValueAllocator() override = default;

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
    duplicateStringValue(char const* value, unsigned int length = kUnknown) override
    {
        //@todo investigate this old optimization
        // if ( !value  ||  value[0] == 0 )
        //   return 0;

        if (length == kUnknown)
            length = (value != nullptr) ? (unsigned int)strlen(value) : 0;

        char* newString = static_cast<char*>(malloc(length + 1));
        if (value != nullptr)
            memcpy(newString, value, length);
        newString[length] = 0;
        return newString;
    }

    void
    releaseStringValue(char* value) override
    {
        if (value != nullptr)
            free(value);
    }
};

static ValueAllocator*&
valueAllocator()
{
    static ValueAllocator* kValueAllocator = new DefaultValueAllocator;  // NOLINT TODO
    return kValueAllocator;
}

static struct DummyValueAllocatorInitializer
{
    DummyValueAllocatorInitializer()
    {
        valueAllocator();  // ensure valueAllocator() statics are initialized
                           // before main().
    }
} gDummyValueAllocatorInitializer;

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
          allocate == DuplicationPolicy::Duplicate ? valueAllocator()->makeMemberName(cstr) : cstr)
    , index_(static_cast<int>(allocate))
{
}

Value::CZString::CZString(CZString const& other)
    : cstr_(
          other.index_ != static_cast<int>(DuplicationPolicy::NoDuplication) && other.cstr_ != 0
              ? valueAllocator()->makeMemberName(other.cstr_)
              : other.cstr_)
    , index_([&]() -> int {
        if (!other.cstr_)
            return other.index_;
        return other.index_ == static_cast<int>(DuplicationPolicy::NoDuplication)
            ? static_cast<int>(DuplicationPolicy::NoDuplication)
            : static_cast<int>(DuplicationPolicy::Duplicate);
    }())
{
}

Value::CZString::~CZString()
{
    if ((cstr_ != nullptr) && index_ == static_cast<int>(DuplicationPolicy::Duplicate))
        valueAllocator()->releaseMemberName(const_cast<char*>(cstr_));
}

bool
Value::CZString::operator<(CZString const& other) const
{
    if ((cstr_ != nullptr) && (other.cstr_ != nullptr))
        return strcmp(cstr_, other.cstr_) < 0;

    return index_ < other.index_;
}

bool
Value::CZString::operator==(CZString const& other) const
{
    if ((cstr_ != nullptr) && (other.cstr_ != nullptr))
        return strcmp(cstr_, other.cstr_) == 0;

    return index_ == other.index_;
}

int
Value::CZString::index() const
{
    return index_;
}

char const*
Value::CZString::cStr() const
{
    return cstr_;
}

bool
Value::CZString::isStaticString() const
{
    return index_ == static_cast<int>(DuplicationPolicy::NoDuplication);
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
Value::Value(ValueType type) : type_(type)
{
    switch (type)
    {
        case ValueType::Null:
            break;

        case ValueType::Int:
        case ValueType::UInt:
            value_.intVal = 0;
            break;

        case ValueType::Real:
            value_.realVal = 0.0;
            break;

        case ValueType::String:
            value_.stringVal = 0;
            break;

        case ValueType::Array:
        case ValueType::Object:
            value_.mapVal = new ObjectValues();
            break;

        case ValueType::Boolean:
            value_.boolVal = false;
            break;

        // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::Value(ValueType) : invalid type");
            // LCOV_EXCL_STOP
    }
}

Value::Value(Int value) : type_(ValueType::Int)
{
    value_.intVal = value;
}

Value::Value(UInt value) : type_(ValueType::UInt)
{
    value_.uintVal = value;
}

Value::Value(double value) : type_(ValueType::Real)
{
    value_.realVal = value;
}

Value::Value(char const* value) : type_(ValueType::String), allocated_(true)
{
    value_.stringVal = valueAllocator()->duplicateStringValue(value);
}

Value::Value(xrpl::Number const& value) : type_(ValueType::String), allocated_(true)
{
    auto const tmp = to_string(value);
    value_.stringVal = valueAllocator()->duplicateStringValue(tmp.c_str(), tmp.length());
}

Value::Value(std::string const& value) : type_(ValueType::String), allocated_(true)
{
    value_.stringVal =
        valueAllocator()->duplicateStringValue(value.c_str(), (unsigned int)value.length());
}

Value::Value(StaticString const& value) : type_(ValueType::String)
{
    value_.stringVal = const_cast<char*>(value.cStr());
}

Value::Value(bool value) : type_(ValueType::Boolean)
{
    value_.boolVal = value;
}

Value::Value(Value const& other) : type_(other.type_)
{
    switch (type_)
    {
        case ValueType::Null:
        case ValueType::Int:
        case ValueType::UInt:
        case ValueType::Real:
        case ValueType::Boolean:
            value_ = other.value_;
            break;

        case ValueType::String:
            if (other.value_.stringVal != nullptr)
            {
                value_.stringVal = valueAllocator()->duplicateStringValue(other.value_.stringVal);
                allocated_ = true;
            }
            else
            {
                value_.stringVal = 0;
            }

            break;

        case ValueType::Array:
        case ValueType::Object:
            value_.mapVal = new ObjectValues(*other.value_.mapVal);
            break;

        // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::Value(Value const&) : invalid type");
            // LCOV_EXCL_STOP
    }
}

Value::~Value()
{
    switch (type_)
    {
        case ValueType::Null:
        case ValueType::Int:
        case ValueType::UInt:
        case ValueType::Real:
        case ValueType::Boolean:
            break;

        case ValueType::String:
            if (allocated_)
                valueAllocator()->releaseStringValue(value_.stringVal);

            break;

        case ValueType::Array:
        case ValueType::Object:
            if (value_.mapVal != nullptr)
                delete value_.mapVal;
            break;

        // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::~Value : invalid type");
            // LCOV_EXCL_STOP
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
    other.type_ = ValueType::Null;
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

    ValueType const temp = type_;
    type_ = other.type_;
    other.type_ = temp;

    int const temp2 = allocated_;
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
    if (i < ui)
        return -1;
    return (i == ui) ? 0 : 1;
}

bool
operator<(Value const& x, Value const& y)
{
    if (auto signum = static_cast<int>(x.type_) - static_cast<int>(y.type_))
    {
        if (x.type_ == ValueType::Int && y.type_ == ValueType::UInt)
        {
            signum = integerCmp(x.value_.intVal, y.value_.uintVal);
        }
        else if (x.type_ == ValueType::UInt && y.type_ == ValueType::Int)
        {
            signum = -integerCmp(y.value_.intVal, x.value_.uintVal);
        }
        return signum < 0;
    }

    switch (x.type_)
    {
        case ValueType::Null:
            return false;

        case ValueType::Int:
            return x.value_.intVal < y.value_.intVal;

        case ValueType::UInt:
            return x.value_.uintVal < y.value_.uintVal;

        case ValueType::Real:
            return x.value_.realVal < y.value_.realVal;

        case ValueType::Boolean:
            return static_cast<int>(x.value_.boolVal) < static_cast<int>(y.value_.boolVal);

        case ValueType::String:
            return (x.value_.stringVal == 0 && (y.value_.stringVal != nullptr)) ||
                ((y.value_.stringVal != nullptr) && (x.value_.stringVal != nullptr) &&
                 strcmp(x.value_.stringVal, y.value_.stringVal) < 0);

        case ValueType::Array:
        case ValueType::Object: {
            if (int const signum = int(x.value_.mapVal->size()) - y.value_.mapVal->size())
                return signum < 0;

            return *x.value_.mapVal < *y.value_.mapVal;
        }

            // LCOV_EXCL_START
        default:
            UNREACHABLE("json::operator<(Value, Value) : invalid type");
            // LCOV_EXCL_STOP
    }

    return false;  // unreachable
}

bool
operator==(Value const& x, Value const& y)
{
    if (x.type_ != y.type_)
    {
        if (x.type_ == ValueType::Int && y.type_ == ValueType::UInt)
            return integerCmp(x.value_.intVal, y.value_.uintVal) == 0;
        if (x.type_ == ValueType::UInt && y.type_ == ValueType::Int)
            return integerCmp(y.value_.intVal, x.value_.uintVal) == 0;
        return false;
    }

    switch (x.type_)
    {
        case ValueType::Null:
            return true;

        case ValueType::Int:
            return x.value_.intVal == y.value_.intVal;

        case ValueType::UInt:
            return x.value_.uintVal == y.value_.uintVal;

        case ValueType::Real:
            return x.value_.realVal == y.value_.realVal;

        case ValueType::Boolean:
            return x.value_.boolVal == y.value_.boolVal;

        case ValueType::String:
            return x.value_.stringVal == y.value_.stringVal ||
                ((y.value_.stringVal != nullptr) && (x.value_.stringVal != nullptr) &&
                 (strcmp(x.value_.stringVal, y.value_.stringVal) == 0));

        case ValueType::Array:
        case ValueType::Object:
            return x.value_.mapVal->size() == y.value_.mapVal->size() &&
                *x.value_.mapVal == *y.value_.mapVal;

        // LCOV_EXCL_START
        default:
            UNREACHABLE("json::operator==(Value, Value) : invalid type");
            // LCOV_EXCL_STOP
    }

    return false;  // unreachable
}

char const*
Value::asCString() const
{
    XRPL_ASSERT(type_ == ValueType::String, "json::Value::asCString : valid type");
    return value_.stringVal;
}

std::string
Value::asString() const
{
    switch (type_)
    {
        case ValueType::Null:
            return "";

        case ValueType::String:
            return (value_.stringVal != nullptr) ? value_.stringVal : "";

        case ValueType::Boolean:
            return value_.boolVal ? "true" : "false";

        case ValueType::Int:
            return std::to_string(value_.intVal);

        case ValueType::UInt:
            return std::to_string(value_.uintVal);

        case ValueType::Real:
            return std::to_string(value_.realVal);

        case ValueType::Array:
        case ValueType::Object:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to string");

            // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::asString : invalid type");
            // LCOV_EXCL_STOP
    }

    return "";  // unreachable
}

Value::Int
Value::asInt() const
{
    switch (type_)
    {
        case ValueType::Null:
            return 0;

        case ValueType::Int:
            return value_.intVal;

        case ValueType::UInt:
            JSON_ASSERT_MESSAGE(
                value_.uintVal < (unsigned)kMaxInt, "integer out of signed integer range");
            return value_.uintVal;

        case ValueType::Real:
            JSON_ASSERT_MESSAGE(
                (value_.realVal >= kMinInt && value_.realVal <= kMaxInt),
                "Real out of signed integer range");
            return Int(value_.realVal);

        case ValueType::Boolean:
            return value_.boolVal ? 1 : 0;

        case ValueType::String: {
            char const* const str{(value_.stringVal != nullptr) ? value_.stringVal : ""};
            return beast::lexicalCastThrow<int>(str);
        }

        case ValueType::Array:
        case ValueType::Object:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to int");

            // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::asInt : invalid type");
            // LCOV_EXCL_STOP
    }

    return 0;  // unreachable;
}

UInt
Value::asAbsUInt() const
{
    switch (type_)
    {
        case ValueType::Null:
            return 0;

        case ValueType::Int: {
            // Doing this conversion through int64 avoids overflow error for
            // value_.intVal = -1 * 2^31 i.e. numeric_limits<int>::min().
            if (value_.intVal < 0)
                return static_cast<std::int64_t>(value_.intVal) * -1;
            return value_.intVal;
        }

        case ValueType::UInt:
            return value_.uintVal;

        case ValueType::Real: {
            if (value_.realVal < 0)
            {
                JSON_ASSERT_MESSAGE(
                    -1 * value_.realVal <= kMaxUInt, "Real out of unsigned integer range");
                return UInt(-1 * value_.realVal);
            }
            JSON_ASSERT_MESSAGE(value_.realVal <= kMaxUInt, "Real out of unsigned integer range");
            return UInt(value_.realVal);
        }

        case ValueType::Boolean:
            return value_.boolVal ? 1 : 0;

        case ValueType::String: {
            char const* const str{(value_.stringVal != nullptr) ? value_.stringVal : ""};
            auto const temp = beast::lexicalCastThrow<std::int64_t>(str);
            if (temp < 0)
            {
                JSON_ASSERT_MESSAGE(-1 * temp <= kMaxUInt, "String out of unsigned integer range");
                return -1 * temp;
            }
            JSON_ASSERT_MESSAGE(temp <= kMaxUInt, "String out of unsigned integer range");
            return temp;
        }

        case ValueType::Array:
        case ValueType::Object:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to int");

            // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::asAbsInt : invalid type");
            // LCOV_EXCL_STOP
    }

    return 0;  // unreachable;
}

Value::UInt
Value::asUInt() const
{
    switch (type_)
    {
        case ValueType::Null:
            return 0;

        case ValueType::Int:
            JSON_ASSERT_MESSAGE(
                value_.intVal >= 0, "Negative integer can not be converted to unsigned integer");
            return value_.intVal;

        case ValueType::UInt:
            return value_.uintVal;

        case ValueType::Real:
            JSON_ASSERT_MESSAGE(
                (value_.realVal >= 0 && value_.realVal <= kMaxUInt),
                "Real out of unsigned integer range");
            return UInt(value_.realVal);

        case ValueType::Boolean:
            return value_.boolVal ? 1 : 0;

        case ValueType::String: {
            char const* const str{(value_.stringVal != nullptr) ? value_.stringVal : ""};
            return beast::lexicalCastThrow<unsigned int>(str);
        }

        case ValueType::Array:
        case ValueType::Object:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to uint");

            // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::asUInt : invalid type");
            // LCOV_EXCL_STOP
    }

    return 0;  // unreachable;
}

double
Value::asDouble() const
{
    switch (type_)
    {
        case ValueType::Null:
            return 0.0;

        case ValueType::Int:
            return value_.intVal;

        case ValueType::UInt:
            return value_.uintVal;

        case ValueType::Real:
            return value_.realVal;

        case ValueType::Boolean:
            return value_.boolVal ? 1.0 : 0.0;

        case ValueType::String:
        case ValueType::Array:
        case ValueType::Object:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to double");

            // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::asDouble : invalid type");
            // LCOV_EXCL_STOP
    }

    return 0;  // unreachable;
}

bool
Value::asBool() const
{
    switch (type_)
    {
        case ValueType::Null:
            return false;

        case ValueType::Int:
        case ValueType::UInt:
            return value_.intVal != 0;

        case ValueType::Real:
            return value_.realVal != 0.0;

        case ValueType::Boolean:
            return value_.boolVal;

        case ValueType::String:
            return (value_.stringVal != nullptr) && value_.stringVal[0] != 0;

        case ValueType::Array:
        case ValueType::Object:
            return !value_.mapVal->empty();

            // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::asBool : invalid type");
            // LCOV_EXCL_STOP
    }

    return false;  // unreachable;
}

bool
Value::isConvertibleTo(ValueType other) const
{
    switch (type_)
    {
        case ValueType::Null:
            return true;

        case ValueType::Int:
            return (other == ValueType::Null && value_.intVal == 0) || other == ValueType::Int ||
                (other == ValueType::UInt && value_.intVal >= 0) || other == ValueType::Real ||
                other == ValueType::String || other == ValueType::Boolean;

        case ValueType::UInt:
            return (other == ValueType::Null && value_.uintVal == 0) ||
                (other == ValueType::Int && value_.uintVal <= (unsigned)kMaxInt) ||
                other == ValueType::UInt || other == ValueType::Real ||
                other == ValueType::String || other == ValueType::Boolean;

        case ValueType::Real:
            return (other == ValueType::Null && value_.realVal == 0.0) ||
                (other == ValueType::Int && value_.realVal >= kMinInt &&
                 value_.realVal <= kMaxInt) ||
                (other == ValueType::UInt && value_.realVal >= 0 && value_.realVal <= kMaxUInt &&
                 std::fabs(round(value_.realVal) - value_.realVal) <
                     std::numeric_limits<double>::epsilon()) ||
                other == ValueType::Real || other == ValueType::String ||
                other == ValueType::Boolean;

        case ValueType::Boolean:
            return (other == ValueType::Null && !value_.boolVal) || other == ValueType::Int ||
                other == ValueType::UInt || other == ValueType::Real ||
                other == ValueType::String || other == ValueType::Boolean;

        case ValueType::String:
            return other == ValueType::String ||
                (other == ValueType::Null &&
                 ((value_.stringVal == nullptr) || value_.stringVal[0] == 0));

        case ValueType::Array:
            return other == ValueType::Array ||
                (other == ValueType::Null && value_.mapVal->empty());

        case ValueType::Object:
            return other == ValueType::Object ||
                (other == ValueType::Null && value_.mapVal->empty());

        // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::isConvertible : invalid type");
            // LCOV_EXCL_STOP
    }

    return false;  // unreachable;
}

/// Number of values in array or object
Value::UInt
Value::size() const
{
    switch (type_)
    {
        case ValueType::Null:
        case ValueType::Int:
        case ValueType::UInt:
        case ValueType::Real:
        case ValueType::Boolean:
        case ValueType::String:
            return 0;

        case ValueType::Array:  // size of the array is highest index + 1
            if (!value_.mapVal->empty())
            {
                ObjectValues::const_iterator itLast = value_.mapVal->end();
                --itLast;
                return (*itLast).first.index() + 1;
            }

            return 0;

        case ValueType::Object:
            return Int(value_.mapVal->size());

            // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::size : invalid type");
            // LCOV_EXCL_STOP
    }

    return 0;  // unreachable;
}

Value::
operator bool() const
{
    if (isNull())
        return false;

    if (isString())
    {
        auto s = asCString();
        return (s != nullptr) && (s[0] != 0);
    }

    return !(isArray() || isObject()) || (size() != 0u);
}

void
Value::clear()
{
    XRPL_ASSERT(
        type_ == ValueType::Null || type_ == ValueType::Array || type_ == ValueType::Object,
        "json::Value::clear : valid type");

    switch (type_)
    {
        case ValueType::Array:
        case ValueType::Object:
            value_.mapVal->clear();
            break;

        default:
            break;
    }
}

Value&
Value::operator[](UInt index)
{
    XRPL_ASSERT(
        type_ == ValueType::Null || type_ == ValueType::Array,
        "json::Value::operator[](UInt) : valid type");

    if (type_ == ValueType::Null)
        *this = Value(ValueType::Array);

    CZString const key(index);
    ObjectValues::iterator it = value_.mapVal->lower_bound(key);

    if (it != value_.mapVal->end() && (*it).first == key)
        return (*it).second;

    ObjectValues::value_type const defaultValue(key, kNull);
    it = value_.mapVal->insert(it, defaultValue);
    return (*it).second;
}

Value const&
Value::operator[](UInt index) const
{
    XRPL_ASSERT(
        type_ == ValueType::Null || type_ == ValueType::Array,
        "json::Value::operator[](UInt) const : valid type");

    if (type_ == ValueType::Null)
        return kNull;

    CZString const key(index);
    ObjectValues::const_iterator const it = value_.mapVal->find(key);

    if (it == value_.mapVal->end())
        return kNull;

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
        type_ == ValueType::Null || type_ == ValueType::Object,
        "json::Value::resolveReference : valid type");

    if (type_ == ValueType::Null)
        *this = Value(ValueType::Object);

    CZString const actualKey(
        key,
        isStatic ? CZString::DuplicationPolicy::NoDuplication
                 : CZString::DuplicationPolicy::DuplicateOnCopy);
    ObjectValues::iterator it = value_.mapVal->lower_bound(actualKey);

    if (it != value_.mapVal->end() && (*it).first == actualKey)
        return (*it).second;

    ObjectValues::value_type const defaultValue(actualKey, kNull);
    it = value_.mapVal->insert(it, defaultValue);
    Value& value = (*it).second;
    return value;
}

Value
Value::get(UInt index, Value const& defaultValue) const
{
    Value const* value = &((*this)[index]);
    return value == &kNull ? defaultValue : *value;
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
        type_ == ValueType::Null || type_ == ValueType::Object,
        "json::Value::operator[](const char*) const : valid type");

    if (type_ == ValueType::Null)
        return kNull;

    CZString const actualKey(key, CZString::DuplicationPolicy::NoDuplication);
    ObjectValues::const_iterator const it = value_.mapVal->find(actualKey);

    if (it == value_.mapVal->end())
        return kNull;

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
    return (*this)[key.cStr()];
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
    return value == &kNull ? defaultValue : *value;
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
        type_ == ValueType::Null || type_ == ValueType::Object,
        "json::Value::removeMember : valid type");

    if (type_ == ValueType::Null)
        return kNull;

    CZString const actualKey(key, CZString::DuplicationPolicy::NoDuplication);
    ObjectValues::iterator const it = value_.mapVal->find(actualKey);

    if (it == value_.mapVal->end())
        return kNull;

    Value old(it->second);
    value_.mapVal->erase(it);
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
    if (type_ != ValueType::Object)
        return false;

    Value const* value = &((*this)[key]);
    return value != &kNull;
}

bool
Value::isMember(std::string const& key) const
{
    return isMember(key.c_str());
}

bool
Value::isMember(StaticString const& key) const
{
    return isMember(key.cStr());
}

Value::Members
Value::getMemberNames() const
{
    XRPL_ASSERT(
        type_ == ValueType::Null || type_ == ValueType::Object,
        "json::Value::getMemberNames : valid type");

    if (type_ == ValueType::Null)
        return Value::Members();

    Members members;
    members.reserve(value_.mapVal->size());
    ObjectValues::const_iterator it = value_.mapVal->begin();
    ObjectValues::const_iterator const itEnd = value_.mapVal->end();

    for (; it != itEnd; ++it)
        members.emplace_back((*it).first.cStr());

    return members;
}

bool
Value::isNull() const
{
    return type_ == ValueType::Null;
}

bool
Value::isBool() const
{
    return type_ == ValueType::Boolean;
}

bool
Value::isInt() const
{
    return type_ == ValueType::Int;
}

bool
Value::isUInt() const
{
    return type_ == ValueType::UInt;
}

bool
Value::isIntegral() const
{
    return type_ == ValueType::Int || type_ == ValueType::UInt || type_ == ValueType::Boolean;
}

bool
Value::isDouble() const
{
    return type_ == ValueType::Real;
}

bool
Value::isNumeric() const
{
    return isIntegral() || isDouble();
}

bool
Value::isString() const
{
    return type_ == ValueType::String;
}

bool
Value::isArray() const
{
    return type_ == ValueType::Array;
}

bool
Value::isArrayOrNull() const
{
    return type_ == ValueType::Null || type_ == ValueType::Array;
}

bool
Value::isObject() const
{
    return type_ == ValueType::Object;
}

bool
Value::isObjectOrNull() const
{
    return type_ == ValueType::Null || type_ == ValueType::Object;
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
        case ValueType::Array:
        case ValueType::Object:
            if (value_.mapVal != nullptr)
                return const_iterator(value_.mapVal->begin());

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
        case ValueType::Array:
        case ValueType::Object:
            if (value_.mapVal != nullptr)
                return const_iterator(value_.mapVal->end());

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
        case ValueType::Array:
        case ValueType::Object:
            if (value_.mapVal != nullptr)
                return iterator(value_.mapVal->begin());
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
        case ValueType::Array:
        case ValueType::Object:
            if (value_.mapVal != nullptr)
                return iterator(value_.mapVal->end());
            break;
        default:
            break;
    }

    return iterator();
}

}  // namespace json
