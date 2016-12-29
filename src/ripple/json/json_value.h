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

#ifndef RIPPLE_JSON_JSON_VALUE_H_INCLUDED
#define RIPPLE_JSON_JSON_VALUE_H_INCLUDED

#include <ripple/json/json_forwards.h>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

/** \brief JSON (JavaScript Object Notation).
 */
namespace Json
{

/** \brief Type of the value held by a Value object.
 */
enum ValueType
{
    nullValue = 0, ///< 'null' value
    intValue,      ///< signed integer value
    uintValue,     ///< unsigned integer value
    realValue,     ///< double value
    stringValue,   ///< UTF-8 string value
    booleanValue,  ///< bool value
    arrayValue,    ///< array value (ordered list)
    objectValue    ///< object value (collection of name/value pairs).
};

enum CommentPlacement
{
    commentBefore = 0,        ///< a comment placed on the line before a value
    commentAfterOnSameLine,   ///< a comment just after a value on the same line
    commentAfter,             ///< a comment on the line after a value (only make sense for root value)
    numberOfCommentPlacement
};

/** \brief Lightweight wrapper to tag static string.
 *
 * Value constructor and objectValue member assignement takes advantage of the
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
    constexpr explicit StaticString ( const char* czstring )
        : str_ ( czstring )
    {
    }

    constexpr operator const char* () const
    {
        return str_;
    }

    constexpr const char* c_str () const
    {
        return str_;
    }

private:
    const char* str_;
};

inline bool operator== (StaticString x, StaticString y)
{
    // TODO(tom): could I use x != y here because StaticStrings are supposed to
    // be unique?
    return strcmp (x.c_str(), y.c_str()) == 0;
}

inline bool operator!= (StaticString x, StaticString y)
{
    return ! (x == y);
}

inline bool operator== (std::string const& x, StaticString y)
{
    return strcmp(x.c_str(), y.c_str()) == 0;
}

inline bool operator!= (std::string const& x, StaticString y)
{
    return ! (x == y);
}

inline bool operator== (StaticString x, std::string const& y)
{
    return y == x;
}

inline bool operator!= (StaticString x, std::string const& y)
{
    return ! (y == x);
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
 * values of an #objectValue or #arrayValue can be accessed using operator[]() methods.
 * Non const methods will automatically create the a #nullValue element
 * if it does not exist.
 * The sequence of an #arrayValue will be automatically resize and initialized
 * with #nullValue. resize() can be used to enlarge or truncate an #arrayValue.
 *
 * The get() methods can be used to obtanis default value in the case the required element
 * does not exist.
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

    static const Value null;
    static const Int minInt;
    static const Int maxInt;
    static const UInt maxUInt;

private:
    class CZString
    {
    public:
        enum DuplicationPolicy
        {
            noDuplication = 0,
            duplicate,
            duplicateOnCopy
        };
        CZString ( int index );
        CZString ( const char* cstr, DuplicationPolicy allocate );
        CZString ( const CZString& other );
        ~CZString ();
        CZString& operator = ( const CZString& other );
        bool operator< ( const CZString& other ) const;
        bool operator== ( const CZString& other ) const;
        int index () const;
        const char* c_str () const;
        bool isStaticString () const;
    private:
        void swap ( CZString& other ) noexcept;
        const char* cstr_;
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
    Value ( ValueType type = nullValue );
    Value ( Int value );
    Value ( UInt value );
    Value ( double value );
    explicit Value ( const char* value );
    Value ( const char* beginValue, const char* endValue );
    /** \brief Constructs a value from a static string.

     * Like other value string constructor but do not duplicate the string for
     * internal storage. The given string must remain alive after the call to this
     * constructor.
     * Example of usage:
     * \code
     * Json::Value aValue( StaticString("some text") );
     * \endcode
     */
    explicit Value ( const StaticString& value );
    explicit Value ( std::string const& value );
    explicit Value ( bool value );
    Value ( const Value& other );
    ~Value ();

    Value& operator= ( const Value& other );

    Value ( Value&& other ) noexcept;
    Value& operator= ( Value&& other ) noexcept;

    Value& operator=(std::string const& s) {return *this = Value{s};}
    Value& operator=(Int i) {return *this = Value{i};}
    Value& operator=(UInt u) {return *this = Value{u};}
    Value& operator=(double d) {return *this = Value{d};}
    Value& operator=(const char* s) {return *this = Value{s};}
    Value& operator=(const StaticString& s) {return *this = Value{s};}
    Value& operator=(bool b) {return *this = Value{b};}
    Value& operator=(ValueType type) {return *this = Value{type};}

    /// Swap values.
    /// \note Currently, comments are intentionally not swapped, for
    /// both logic and efficiency.
    void swap ( Value& other ) noexcept;

    ValueType type () const;

    const char* asCString () const;
    std::string asString () const;
    Int asInt () const;
    UInt asUInt () const;
    double asDouble () const;
    bool asBool () const;

    /** isNull() tests to see if this field is null.  Don't use this method to
        test for emptiness: use empty(). */
    bool isNull () const;
    bool isBool () const;
    bool isInt () const;
    bool isUInt () const;
    bool isIntegral () const;
    bool isDouble () const;
    bool isNumeric () const;
    bool isString () const;
    bool isArray () const;
    bool isObject () const;

    bool isConvertibleTo ( ValueType other ) const;

    /// Number of values in array or object
    UInt size () const;

    /** Returns false if this is an empty array, empty object, empty string,
        or null. */
    explicit
    operator bool() const;

    /// Remove all object members and array elements.
    /// \pre type() is arrayValue, objectValue, or nullValue
    /// \post type() is unchanged
    void clear ();

    /// Resize the array to size elements.
    /// New elements are initialized to null.
    /// May only be called on nullValue or arrayValue.
    /// \pre type() is arrayValue or nullValue
    /// \post type() is arrayValue
    void resize ( UInt size );

    /// Access an array element (zero based index ).
    /// If the array contains less than index element, then null value are inserted
    /// in the array so that its size is index+1.
    /// (You may need to say 'value[0u]' to get your compiler to distinguish
    ///  this from the operator[] which takes a string.)
    Value& operator[] ( UInt index );
    /// Access an array element (zero based index )
    /// (You may need to say 'value[0u]' to get your compiler to distinguish
    ///  this from the operator[] which takes a string.)
    const Value& operator[] ( UInt index ) const;
    /// If the array contains at least index+1 elements, returns the element value,
    /// otherwise returns defaultValue.
    Value get ( UInt index,
                const Value& defaultValue ) const;
    /// Return true if index < size().
    bool isValidIndex ( UInt index ) const;
    /// \brief Append value to array at the end.
    ///
    /// Equivalent to jsonvalue[jsonvalue.size()] = value;
    Value& append ( const Value& value );
    Value& append ( const std::string& value ) {return append(Value{value});}
    Value& append ( Int value ) {return append(Value{value});}
    Value& append ( UInt value ) {return append(Value{value});}
    Value& append ( double value ) {return append(Value{value});}
    Value& append ( const char* value ) {return append(Value{value});}
    Value& append ( const StaticString& value ) {return append(Value{value});}
    Value& append ( bool value ) {return append(Value{value});}
    Value& append ( ValueType type ) {return append(Value{type});}

    /// Access an object value by name, create a null member if it does not exist.
    Value& operator[] ( const char* key );
    /// Access an object value by name, returns null if there is no member with that name.
    const Value& operator[] ( const char* key ) const;
    /// Access an object value by name, create a null member if it does not exist.
    Value& operator[] ( std::string const& key );
    /// Access an object value by name, returns null if there is no member with that name.
    const Value& operator[] ( std::string const& key ) const;
    /** \brief Access an object value by name, create a null member if it does not exist.

     * If the object as no entry for that name, then the member name used to store
     * the new entry is not duplicated.
     * Example of use:
     * \code
     * Json::Value object;
     * static const StaticString code("code");
     * object[code] = 1234;
     * \endcode
     */
    Value& operator[] ( const StaticString& key );

    /// Return the member named key if it exist, defaultValue otherwise.
    Value get ( const char* key,
                const Value& defaultValue ) const;
    /// Return the member named key if it exist, defaultValue otherwise.
    Value get ( std::string const& key,
                const Value& defaultValue ) const;

    /// \brief Remove and return the named member.
    ///
    /// Do nothing if it did not exist.
    /// \return the removed Value, or null.
    /// \pre type() is objectValue or nullValue
    /// \post type() is unchanged
    Value removeMember ( const char* key );
    /// Same as removeMember(const char*)
    Value removeMember ( std::string const& key );

    /// Return true if the object has a member named key.
    bool isMember ( const char* key ) const;
    /// Return true if the object has a member named key.
    bool isMember ( std::string const& key ) const;

    /// \brief Return a list of the member names.
    ///
    /// If null, return an empty list.
    /// \pre type() is objectValue or nullValue
    /// \post if type() was nullValue, it remains nullValue
    Members getMemberNames () const;

    bool hasComment ( CommentPlacement placement ) const;
    /// Include delimiters and embedded newlines.
    std::string getComment ( CommentPlacement placement ) const;

    std::string toStyledString () const;

    const_iterator begin () const;
    const_iterator end () const;

    iterator begin ();
    iterator end ();

    friend bool operator== (const Value&, const Value&);
    friend bool operator< (const Value&, const Value&);
    friend bool operator== (const Value&, const std::string&);
    friend bool operator== (const Value&, const StaticString&);
    friend bool operator== (const Value&, const char*);
    friend bool operator== (const Value&, Json::Int);
    friend bool operator== (const Value&, Json::UInt);
    friend bool operator== (const Value&, double);
    friend bool operator== (const Value&, bool);
    friend bool operator== (const Value&, ValueType);

private:
    Value& resolveReference ( const char* key,
                              bool isStatic );

private:
    union ValueHolder
    {
        Int int_;
        UInt uint_;
        double real_;
        bool bool_;
        char* string_;
        ObjectValues* map_;
    } value_;
    ValueType type_ : 8;
    int allocated_ : 1;     // Notes: if declared as bool, bitfield is useless.
};

bool operator== (const Value&, const Value&);

inline
bool operator!= (const Value& x, const Value& y)
{
    return ! (x == y);
}

bool operator< (const Value&, const Value&);

inline
bool operator<= (const Value& x, const Value& y)
{
    return ! (y < x);
}

inline
bool operator> (const Value& x, const Value& y)
{
    return y < x;
}

inline
bool operator>= (const Value& x, const Value& y)
{
    return ! (x < y);
}

bool operator== (const Value&, const std::string&);
bool operator== (const Value&, const StaticString&);
bool operator== (const Value&, const char*);
bool operator== (const Value&, Json::Int);
bool operator== (const Value&, Json::UInt);
bool operator== (const Value&, double);
bool operator== (const Value&, bool);
bool operator== (const Value&, ValueType);

inline
bool
operator!= (const Value& x, const std::string& y)
{
    return !(x == y);
}

/** \brief Experimental do not use: Allocator to customize member name and string value memory management done by Value.
 *
 * - makeMemberName() and releaseMemberName() are called to respectively duplicate and
 *   free an Json::objectValue member name.
 * - duplicateStringValue() and releaseStringValue() are called similarly to
 *   duplicate and free a Json::stringValue value.
 */
class ValueAllocator
{
public:
    enum { unknown = (unsigned) - 1 };

    virtual ~ValueAllocator ();

    virtual char* makeMemberName ( const char* memberName ) = 0;
    virtual void releaseMemberName ( char* memberName ) = 0;
    virtual char* duplicateStringValue ( const char* value,
                                         unsigned int length = unknown ) = 0;
    virtual void releaseStringValue ( char* value ) = 0;
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

    ValueIteratorBase ();

    explicit ValueIteratorBase ( const Value::ObjectValues::iterator& current );

    bool operator == ( const SelfType& other ) const
    {
        return isEqual ( other );
    }

    bool operator != ( const SelfType& other ) const
    {
        return !isEqual ( other );
    }

    difference_type operator - ( const SelfType& other ) const
    {
        return computeDistance ( other );
    }

    /// Return either the index or the member name of the referenced value as a Value.
    Value key () const;

    /// Return the index of the referenced Value. -1 if it is not an arrayValue.
    UInt index () const;

    /// Return the member name of the referenced Value. "" if it is not an objectValue.
    const char* memberName () const;

protected:
    Value& deref () const;

    void increment ();

    void decrement ();

    difference_type computeDistance ( const SelfType& other ) const;

    bool isEqual ( const SelfType& other ) const;

    void copy ( const SelfType& other );

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
    using reference = const Value&;
    using pointer = const Value*;
    using SelfType = ValueConstIterator;

    ValueConstIterator ();
private:
    /*! \internal Use by Value to create an iterator.
     */
    explicit ValueConstIterator ( const Value::ObjectValues::iterator& current );
public:
    SelfType& operator = ( const ValueIteratorBase& other );

    SelfType operator++ ( int )
    {
        SelfType temp ( *this );
        ++*this;
        return temp;
    }

    SelfType operator-- ( int )
    {
        SelfType temp ( *this );
        --*this;
        return temp;
    }

    SelfType& operator-- ()
    {
        decrement ();
        return *this;
    }

    SelfType& operator++ ()
    {
        increment ();
        return *this;
    }

    reference operator * () const
    {
        return deref ();
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

    ValueIterator ();
    ValueIterator ( const ValueConstIterator& other );
    ValueIterator ( const ValueIterator& other );
private:
    /*! \internal Use by Value to create an iterator.
     */
    explicit ValueIterator ( const Value::ObjectValues::iterator& current );
public:

    SelfType& operator = ( const SelfType& other );

    SelfType operator++ ( int )
    {
        SelfType temp ( *this );
        ++*this;
        return temp;
    }

    SelfType operator-- ( int )
    {
        SelfType temp ( *this );
        --*this;
        return temp;
    }

    SelfType& operator-- ()
    {
        decrement ();
        return *this;
    }

    SelfType& operator++ ()
    {
        increment ();
        return *this;
    }

    reference operator * () const
    {
        return deref ();
    }
};

//------------------------------------------------------------------------------

using write_t = std::function<void(void const*, std::size_t)>;

/** Stream compact JSON to the specified function. */
void
stream (Json::Value const& jv, write_t write);

} // namespace Json


#endif // CPPTL_JSON_H_INCLUDED
