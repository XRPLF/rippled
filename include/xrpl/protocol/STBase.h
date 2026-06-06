#pragma once

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/Serializer.h>

#include <ostream>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace xrpl {

/// Note, should be treated as flags that can be | and &
struct JsonOptions
{
    using underlying_t = unsigned int;
    underlying_t value;

    enum class Values : underlying_t {
        None = 0b0000'0000,
        IncludeDate = 0b0000'0001,
        DisableApiPriorV2 = 0b0000'0010,

        // IMPORTANT `All` must be union of all of the above; see also operator~
        All = IncludeDate | DisableApiPriorV2  // 0b0000'0011
    };

    constexpr JsonOptions(underlying_t v) noexcept : value(v)
    {
    }

    constexpr JsonOptions(Values v) noexcept : value(static_cast<JsonOptions::underlying_t>(v))
    {
    }

    [[nodiscard]] constexpr explicit
    operator underlying_t() const noexcept
    {
        return value;
    }
    [[nodiscard]] constexpr explicit
    operator bool() const noexcept
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
    /// difference e.g. `(options & ~JsonOptions::kIncludeDate)`
    [[nodiscard]] constexpr JsonOptions friend
    operator~(JsonOptions v) noexcept
    {
        return {~v.value & static_cast<underlying_t>(Values::All)};
    }
};

template <typename T>
    requires requires(T const& t) {
        { t.getJson(JsonOptions::Values::None) } -> std::convertible_to<json::Value>;
    }
json::Value
toJson(T const& t)
{
    return t.getJson(JsonOptions::Values::None);
}

namespace detail {
class STVar;
}  // namespace detail

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
    SField const* fName_;

public:
    virtual ~STBase() = default;
    STBase();
    STBase(STBase const&) = default;
    STBase&
    operator=(STBase const& t);

    explicit STBase(SField const& n);

    bool
    operator==(STBase const& t) const;
    bool
    operator!=(STBase const& t) const;

    template <class D>
    D&
    downcast();

    template <class D>
    D const&
    downcast() const;

    [[nodiscard]] virtual SerializedTypeID
    getSType() const;

    [[nodiscard]] virtual std::string
    getFullText() const;

    [[nodiscard]] virtual std::string
    getText() const;

    [[nodiscard]] virtual json::Value getJson(JsonOptions = JsonOptions::Values::None) const;

    virtual void
    add(Serializer& s) const;

    [[nodiscard]] virtual bool
    isEquivalent(STBase const& t) const;

    [[nodiscard]] virtual bool
    isDefault() const;

    /** A STBase is a field.
        This sets the name.
    */
    void
    setFName(SField const& n);

    [[nodiscard]] SField const&
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
operator<<(std::ostream& out, STBase const& t);

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
[[nodiscard]] D const&
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

}  // namespace xrpl
