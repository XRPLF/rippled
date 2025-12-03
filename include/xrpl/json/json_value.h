#ifndef XRPL_JSON_JSON_VALUE_H_INCLUDED
#define XRPL_JSON_JSON_VALUE_H_INCLUDED

#include <xrpl/basics/Number.h>
#include <xrpl/json/json_forwards.h>

#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

/** \brief JSON (JavaScript Object Notation).
 */
namespace Json {

/** \brief Type of the value held by a Value object.
 */
enum ValueType {
    nullValue = 0,  ///< 'null' value
    intValue,       ///< signed integer value
    uintValue,      ///< unsigned integer value
    realValue,      ///< double value
    stringValue,    ///< UTF-8 string value
    booleanValue,   ///< bool value
    arrayValue,     ///< array value (ordered list)
    objectValue     ///< object value (collection of name/value pairs).
};

/** \brief Lightweight wrapper to tag static string.
 *
 * Value constructor and objectValue member assignment takes advantage of the
 * StaticString and avoid the cost of string duplication when storing the
 * string or the member name.
 *
 * Example of usage:
 * \code
 * Json::Value aValue( StaticString("some text") );
 * Json::Value object;
 * static const StaticString code("code");
 * object[code] = 1234;
 * \endcode
 */
class StaticString
{
public:
    constexpr explicit StaticString(char const* czstring) : str_(czstring)
    {
    }

    constexpr
    operator char const*() const
    {
        return str_;
    }

    constexpr char const*
    c_str() const
    {
        return str_;
    }

private:
    char const* str_;
};

inline bool
operator==(StaticString x, StaticString y)
{
    return strcmp(x.c_str(), y.c_str()) == 0;
}

inline bool
operator!=(StaticString x, StaticString y)
{
    return !(x == y);
}

inline bool
operator==(std::string const& x, StaticString y)
{
    return strcmp(x.c_str(), y.c_str()) == 0;
}

inline bool
operator!=(std::string const& x, StaticString y)
{
    return !(x == y);
}

inline bool
operator==(StaticString x, std::string const& y)
{
    return y == x;
}

inline bool
operator!=(StaticString x, std::string const& y)
{
    return !(y == x);
}

/** \brief Represents a <a HREF="http://www.json.org">JSON</a> value.
 *
 * This class is a discriminated union wrapper that can represent a:
 * - signed integer [range: Value::minInt - Value::maxInt]
 * - unsigned integer (range: 0 - Value::maxUInt)
 * - double
 * - UTF-8 string
 * - boolean
 * - 'null'
 * - an ordered list of Value
 * - collection of name/value pairs (javascript object)
 *
 * The type of the held value is represented by a #ValueType and
 * can be obtained using type().
 *
 * values of an #objectValue or #arrayValue can be accessed using operator[]()
 * methods. Non const methods will automatically create the a #nullValue element
 * if it does not exist.
 * The sequence of an #arrayValue will be automatically resize and initialized
 * with #nullValue. resize() can be used to enlarge or truncate an #arrayValue.
 *
 * The get() methods can be used to obtain a default value in the case the
 * required element does not exist.
 *
 * It is possible to iterate over the list of a #objectValue values using
 * the getMemberNames() method.
 */
class Value
{
    friend class ValueIteratorBase;

public:
    using Members = std::vector<std::string>;
    using iterator = ValueIterator;
    using const_iterator = ValueConstIterator;
    using UInt = Json::UInt;
    using Int = Json::Int;
    using ArrayIndex = UInt;

    static Value const null;
    static constexpr Int minInt = std::numeric_limits<Int>::min();
    static constexpr Int maxInt = std::numeric_limits<Int>::max();
    static constexpr UInt maxUInt = std::numeric_limits<UInt>::max();

private:
    class CZString
    {
    public:
        enum DuplicationPolicy {
            noDuplication = 0,
            duplicate,
            duplicateOnCopy
        };
        CZString(int index);
        CZString(char const* cstr, DuplicationPolicy allocate);
        CZString(CZString const& other);
        ~CZString();
        CZString&
        operator=(CZString const& other) = delete;
        bool
        operator<(CZString const& other) const;
        bool
        operator==(CZString const& other) const;
        int
        index() const;
        char const*
        c_str() const;
        bool
        isStaticString() const;

    private:
        char const* cstr_;
        int index_;
    };

public:
    using ObjectValues = std::map<CZString, Value>;

public:
    /** \brief Create a default Value of the given type.

      This is a very useful constructor.
      To create an empty array, pass arrayValue.
      To create an empty object, pass objectValue.
      Another Value can then be set to this one by assignment.
    This is useful since clear() and resize() will not alter types.

           Examples:
    \code
    Json::Value null_value; // null
    Json::Value arr_value(Json::arrayValue); // []
    Json::Value obj_value(Json::objectValue); // {}
    \endcode
         */
    Value(ValueType type = nullValue);
    Value(Int value);
    Value(UInt value);
    Value(double value);
    Value(char const* value);
    Value(ripple::Number const& value);
    /** \brief Constructs a value from a static string.

     * Like other value string constructor but do not duplicate the string for
     * internal storage. The given string must remain alive after the call to
     this
     * constructor.
     * Example of usage:
     * \code
     * Json::Value aValue( StaticString("some text") );
     * \endcode
     */
    Value(StaticString const& value);
    Value(std::string const& value);
    Value(bool value);
    Value(Value const& other);
    ~Value();

    Value&
    operator=(Value const& other);
    Value&
    operator=(Value&& other);

    Value(Value&& other) noexcept;

    /// Swap values.
    void
    swap(Value& other) noexcept;

    ValueType
    type() const;

    char const*
    asCString() const;
    /** Returns the unquoted string value. */
    std::string
    asString() const;
    Int
    asInt() const;
    UInt
    asUInt() const;
    double
    asDouble() const;
    bool
    asBool() const;

    /** Correct absolute value from int or unsigned int */
    UInt
    asAbsUInt() const;

    // TODO: What is the "empty()" method this docstring mentions?
    /** isNull() tests to see if this field is null.  Don't use this method to
        test for emptiness: use empty(). */
    bool
    isNull() const;
    bool
    isBool() const;
    bool
    isInt() const;
    bool
    isUInt() const;
    bool
    isIntegral() const;
    bool
    isDouble() const;
    bool
    isNumeric() const;
    bool
    isString() const;
    bool
    isArray() const;
    bool
    isArrayOrNull() const;
    bool
    isObject() const;
    bool
    isObjectOrNull() const;

    bool
    isConvertibleTo(ValueType other) const;

    /// Number of values in array or object
    UInt
    size() const;

    /** Returns false if this is an empty array, empty object, empty string,
        or null. */
    explicit
    operator bool() const;

    /// Remove all object members and array elements.
    /// \pre type() is arrayValue, objectValue, or nullValue
    /// \post type() is unchanged
    void
    clear();

    /// Access an array element (zero based index ).
    /// If the array contains less than index element, then null value are
    /// inserted in the array so that its size is index+1. (You may need to say
    /// 'value[0u]' to get your compiler to distinguish
    ///  this from the operator[] which takes a string.)
    Value&
    operator[](UInt index);
    /// Access an array element (zero based index )
    /// (You may need to say 'value[0u]' to get your compiler to distinguish
    ///  this from the operator[] which takes a string.)
    Value const&
    operator[](UInt index) const;
    /// If the array contains at least index+1 elements, returns the element
    /// value, otherwise returns defaultValue.
    Value
    get(UInt index, Value const& defaultValue) const;
    /// Return true if index < size().
    bool
    isValidIndex(UInt index) const;
    /// \brief Append value to array at the end.
    ///
    /// Equivalent to jsonvalue[jsonvalue.size()] = value;
    Value&
    append(Value const& value);
    Value&
    append(Value&& value);

    /// Access an object value by name, create a null member if it does not
    /// exist.
    Value&
    operator[](char const* key);
    /// Access an object value by name, returns null if there is no member with
    /// that name.
    Value const&
    operator[](char const* key) const;
    /// Access an object value by name, create a null member if it does not
    /// exist.
    Value&
    operator[](std::string const& key);
    /// Access an object value by name, returns null if there is no member with
    /// that name.
    Value const&
    operator[](std::string const& key) const;
    /** \brief Access an object value by name, create a null member if it does
     not exist.

     * If the object as no entry for that name, then the member name used to
     store
     * the new entry is not duplicated.
     * Example of use:
     * \code
     * Json::Value object;
     * static const StaticString code("code");
     * object[code] = 1234;
     * \endcode
     */
    Value&
    operator[](StaticString const& key);
    Value const&
    operator[](StaticString const& key) const;

    /// Return the member named key if it exist, defaultValue otherwise.
    Value
    get(char const* key, Value const& defaultValue) const;
    /// Return the member named key if it exist, defaultValue otherwise.
    Value
    get(std::string const& key, Value const& defaultValue) const;

    /// \brief Remove and return the named member.
    ///
    /// Do nothing if it did not exist.
    /// \return the removed Value, or null.
    /// \pre type() is objectValue or nullValue
    /// \post type() is unchanged
    Value
    removeMember(char const* key);
    /// Same as removeMember(const char*)
    Value
    removeMember(std::string const& key);

    /// Return true if the object has a member named key.
    bool
    isMember(char const* key) const;
    /// Return true if the object has a member named key.
    bool
    isMember(std::string const& key) const;
    /// Return true if the object has a member named key.
    bool
    isMember(StaticString const& key) const;

    /// \brief Return a list of the member names.
    ///
    /// If null, return an empty list.
    /// \pre type() is objectValue or nullValue
    /// \post if type() was nullValue, it remains nullValue
    Members
    getMemberNames() const;

    std::string
    toStyledString() const;

    const_iterator
    begin() const;
    const_iterator
    end() const;

    iterator
    begin();
    iterator
    end();

    friend bool
    operator==(Value const&, Value const&);
    friend bool
    operator<(Value const&, Value const&);

private:
    Value&
    resolveReference(char const* key, bool isStatic);

private:
    union ValueHolder
    {
        Int int_;
        UInt uint_;
        double real_;
        bool bool_;
        char* string_;
        ObjectValues* map_{nullptr};
    } value_;
    ValueType type_ : 8;
    int allocated_ : 1;  // Notes: if declared as bool, bitfield is useless.
};

inline Value
to_json(ripple::Number const& number)
{
    return to_string(number);
}

bool
operator==(Value const&, Value const&);

inline bool
operator!=(Value const& x, Value const& y)
{
    return !(x == y);
}

bool
operator<(Value const&, Value const&);

inline bool
operator<=(Value const& x, Value const& y)
{
    return !(y < x);
}

inline bool
operator>(Value const& x, Value const& y)
{
    return y < x;
}

inline bool
operator>=(Value const& x, Value const& y)
{
    return !(x < y);
}

/** \brief Experimental do not use: Allocator to customize member name and
 * string value memory management done by Value.
 *
 * - makeMemberName() and releaseMemberName() are called to respectively
 * duplicate and free an Json::objectValue member name.
 * - duplicateStringValue() and releaseStringValue() are called similarly to
 *   duplicate and free a Json::stringValue value.
 */
class ValueAllocator
{
public:
    enum { unknown = (unsigned)-1 };

    virtual ~ValueAllocator() = default;

    virtual char*
    makeMemberName(char const* memberName) = 0;
    virtual void
    releaseMemberName(char* memberName) = 0;
    virtual char*
    duplicateStringValue(char const* value, unsigned int length = unknown) = 0;
    virtual void
    releaseStringValue(char* value) = 0;
};

/** \brief base class for Value iterators.
 *
 */
class ValueIteratorBase
{
public:
    using size_t = unsigned int;
    using difference_type = int;
    using SelfType = ValueIteratorBase;

    ValueIteratorBase();

    explicit ValueIteratorBase(Value::ObjectValues::iterator const& current);

    bool
    operator==(SelfType const& other) const
    {
        return isEqual(other);
    }

    bool
    operator!=(SelfType const& other) const
    {
        return !isEqual(other);
    }

    /// Return either the index or the member name of the referenced value as a
    /// Value.
    Value
    key() const;

    /// Return the index of the referenced Value. -1 if it is not an arrayValue.
    UInt
    index() const;

    /// Return the member name of the referenced Value. "" if it is not an
    /// objectValue.
    char const*
    memberName() const;

protected:
    Value&
    deref() const;

    void
    increment();

    void
    decrement();

    difference_type
    computeDistance(SelfType const& other) const;

    bool
    isEqual(SelfType const& other) const;

    void
    copy(SelfType const& other);

private:
    Value::ObjectValues::iterator current_;
    // Indicates that iterator is for a null value.
    bool isNull_;
};

/** \brief const iterator for object and array value.
 *
 */
class ValueConstIterator : public ValueIteratorBase
{
    friend class Value;

public:
    using size_t = unsigned int;
    using difference_type = int;
    using reference = Value const&;
    using pointer = Value const*;
    using SelfType = ValueConstIterator;

    ValueConstIterator() = default;

private:
    /*! \internal Use by Value to create an iterator.
     */
    explicit ValueConstIterator(Value::ObjectValues::iterator const& current);

public:
    SelfType&
    operator=(ValueIteratorBase const& other);

    SelfType
    operator++(int)
    {
        SelfType temp(*this);
        ++*this;
        return temp;
    }

    SelfType
    operator--(int)
    {
        SelfType temp(*this);
        --*this;
        return temp;
    }

    SelfType&
    operator--()
    {
        decrement();
        return *this;
    }

    SelfType&
    operator++()
    {
        increment();
        return *this;
    }

    reference
    operator*() const
    {
        return deref();
    }
};

/** \brief Iterator for object and array value.
 */
class ValueIterator : public ValueIteratorBase
{
    friend class Value;

public:
    using size_t = unsigned int;
    using difference_type = int;
    using reference = Value&;
    using pointer = Value*;
    using SelfType = ValueIterator;

    ValueIterator() = default;
    ValueIterator(ValueConstIterator const& other);
    ValueIterator(ValueIterator const& other);

private:
    /*! \internal Use by Value to create an iterator.
     */
    explicit ValueIterator(Value::ObjectValues::iterator const& current);

public:
    SelfType&
    operator=(SelfType const& other);

    SelfType
    operator++(int)
    {
        SelfType temp(*this);
        ++*this;
        return temp;
    }

    SelfType
    operator--(int)
    {
        SelfType temp(*this);
        --*this;
        return temp;
    }

    SelfType&
    operator--()
    {
        decrement();
        return *this;
    }

    SelfType&
    operator++()
    {
        increment();
        return *this;
    }

    reference
    operator*() const
    {
        return deref();
    }
};

}  // namespace Json

#endif  // CPPTL_JSON_H_INCLUDED
