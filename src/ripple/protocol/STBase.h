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

#ifndef RIPPLE_PROTOCOL_STBASE_H_INCLUDED
#define RIPPLE_PROTOCOL_STBASE_H_INCLUDED

#include <ripple/basics/contract.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/Serializer.h>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
namespace ripple {

/// Note, should be treated as flags that can be | and &
struct JsonOptions
{
    using underlying_t = unsigned int;
    underlying_t value;

    enum values : underlying_t {
        // clang-format off
        none                        = 0b0000'0000,
        include_date                = 0b0000'0001,
        disable_API_prior_V2        = 0b0000'0010,

        // IMPORTANT `_all` must be union of all of the above; see also operator~
        _all                        = 0b0000'0011
        // clang-format on
    };

    constexpr JsonOptions(underlying_t v) noexcept : value(v)
    {
    }

    [[nodiscard]] constexpr explicit operator underlying_t() const noexcept
    {
        return value;
    }
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return value != 0u;
    }
    [[nodiscard]] constexpr auto friend
    operator==(JsonOptions lh, JsonOptions rh) noexcept -> bool = default;
    [[nodiscard]] constexpr auto friend
    operator!=(JsonOptions lh, JsonOptions rh) noexcept -> bool = default;

    /// Returns JsonOptions union of lh and rh
    [[nodiscard]] constexpr JsonOptions friend
    operator|(JsonOptions lh, JsonOptions rh) noexcept
    {
        return {lh.value | rh.value};
    }

    /// Returns JsonOptions intersection of lh and rh
    [[nodiscard]] constexpr JsonOptions friend
    operator&(JsonOptions lh, JsonOptions rh) noexcept
    {
        return {lh.value & rh.value};
    }

    /// Returns JsonOptions binary negation, can be used with & (above) for set
    /// difference e.g. `(options & ~JsonOptions::include_date)`
    [[nodiscard]] constexpr JsonOptions friend
    operator~(JsonOptions v) noexcept
    {
        return {~v.value & static_cast<underlying_t>(_all)};
    }
};

namespace detail {
class STVar;
}

// VFALCO TODO fix this restriction on copy assignment.
//
// CAUTION: Do not create a vector (or similar container) of any object derived
// from STBase. Use Boost ptr_* containers. The copy assignment operator
// of STBase has semantics that will cause contained types to change
// their names when an object is deleted because copy assignment is used to
// "slide down" the remaining types and this will not copy the field
// name. Changing the copy assignment operator to copy the field name breaks the
// use of copy assignment just to copy values, which is used in the transaction
// engine code.

//------------------------------------------------------------------------------

/** A type which can be exported to a well known binary format.

    A STBase:
        - Always a field
        - Can always go inside an eligible enclosing STBase
            (such as STArray)
        - Has a field name

    Like JSON, a SerializedObject is a basket which has rules
    on what it can hold.

    @note "ST" stands for "Serialized Type."
*/
class STBase
{
    SField const* fName;

public:
    virtual ~STBase() = default;
    STBase();
    STBase(const STBase&) = default;
    STBase&
    operator=(const STBase& t);

    explicit STBase(SField const& n);

    bool
    operator==(const STBase& t) const;
    bool
    operator!=(const STBase& t) const;

    template <class D>
    D&
    downcast();

    template <class D>
    D const&
    downcast() const;

    virtual SerializedTypeID
    getSType() const;

    virtual std::string
    getFullText() const;

    virtual std::string
    getText() const;

    virtual Json::Value getJson(JsonOptions /*options*/) const;

    virtual void
    add(Serializer& s) const;

    virtual bool
    isEquivalent(STBase const& t) const;

    virtual bool
    isDefault() const;

    /** A STBase is a field.
        This sets the name.
    */
    void
    setFName(SField const& n);

    SField const&
    getFName() const;

    void
    addFieldID(Serializer& s) const;

protected:
    template <class T>
    static STBase*
    emplace(std::size_t n, void* buf, T&& val);

private:
    virtual STBase*
    copy(std::size_t n, void* buf) const;
    virtual STBase*
    move(std::size_t n, void* buf);

    friend class detail::STVar;
};

//------------------------------------------------------------------------------

std::ostream&
operator<<(std::ostream& out, const STBase& t);

template <class D>
D&
STBase::downcast()
{
    D* ptr = dynamic_cast<D*>(this);
    if (ptr == nullptr)
        Throw<std::bad_cast>();
    return *ptr;
}

template <class D>
D const&
STBase::downcast() const
{
    D const* ptr = dynamic_cast<D const*>(this);
    if (ptr == nullptr)
        Throw<std::bad_cast>();
    return *ptr;
}

template <class T>
STBase*
STBase::emplace(std::size_t n, void* buf, T&& val)
{
    using U = std::decay_t<T>;
    if (sizeof(U) > n)
        return new U(std::forward<T>(val));
    return new (buf) U(std::forward<T>(val));
}

}  // namespace ripple

#endif
