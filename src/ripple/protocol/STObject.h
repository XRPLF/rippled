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

#ifndef RIPPLE_PROTOCOL_STOBJECT_H_INCLUDED
#define RIPPLE_PROTOCOL_STOBJECT_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/basics/FeeUnits.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/contract.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/SOTemplate.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STPathSet.h>
#include <ripple/protocol/STVector256.h>
#include <ripple/protocol/impl/STVar.h>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/optional.hpp>
#include <cassert>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ripple {

class STArray;

inline void
throwFieldNotFound(SField const& field)
{
    Throw<std::runtime_error>("Field not found: " + field.getName());
}

class STObject : public STBase, public CountedObject<STObject>
{
private:
    // Proxy value for a STBase derived class
    template <class T>
    class Proxy
    {
    protected:
        using value_type = typename T::value_type;

        STObject* st_;
        SOEStyle style_;
        TypedField<T> const* f_;

        Proxy(Proxy const&) = default;
        Proxy(STObject* st, TypedField<T> const* f);
        value_type
        value() const;
        T const*
        find() const;

        template <class U>
        void
        assign(U&& u);
    };

    template <class T>
    class ValueProxy : private Proxy<T>
    {
    private:
        using value_type = typename T::value_type;

    public:
        ValueProxy(ValueProxy const&) = default;
        ValueProxy&
        operator=(ValueProxy const&) = delete;

        template <class U>
        std::enable_if_t<std::is_assignable_v<T, U>, ValueProxy&>
        operator=(U&& u);

        operator value_type() const;

    private:
        friend class STObject;

        ValueProxy(STObject* st, TypedField<T> const* f);
    };

    template <class T>
    class OptionalProxy : private Proxy<T>
    {
    private:
        using value_type = typename T::value_type;

        using optional_type =
            boost::optional<typename std::decay<value_type>::type>;

    public:
        OptionalProxy(OptionalProxy const&) = default;
        OptionalProxy&
        operator=(OptionalProxy const&) = delete;

        /** Returns `true` if the field is set.

            Fields with soeDEFAULT and set to the
            default value will return `true`
        */
        explicit operator bool() const noexcept;

        /** Return the contained value

            Throws:

                STObject::FieldErr if !engaged()
        */
        value_type
        operator*() const;

        operator optional_type() const;

        /** Explicit conversion to boost::optional */
        optional_type
        operator~() const;

        friend bool
        operator==(OptionalProxy const& lhs, boost::none_t) noexcept
        {
            return !lhs.engaged();
        }

        friend bool
        operator==(boost::none_t, OptionalProxy const& rhs) noexcept
        {
            return rhs == boost::none;
        }

        friend bool
        operator==(OptionalProxy const& lhs, optional_type const& rhs) noexcept
        {
            if (!lhs.engaged())
                return !rhs;
            if (!rhs)
                return false;
            return *lhs == *rhs;
        }

        friend bool
        operator==(optional_type const& lhs, OptionalProxy const& rhs) noexcept
        {
            return rhs == lhs;
        }

        friend bool
        operator==(OptionalProxy const& lhs, OptionalProxy const& rhs) noexcept
        {
            if (lhs.engaged() != rhs.engaged())
                return false;
            return !lhs.engaged() || *lhs == *rhs;
        }

        friend bool
        operator!=(OptionalProxy const& lhs, boost::none_t) noexcept
        {
            return !(lhs == boost::none);
        }

        friend bool
        operator!=(boost::none_t, OptionalProxy const& rhs) noexcept
        {
            return !(rhs == boost::none);
        }

        friend bool
        operator!=(OptionalProxy const& lhs, optional_type const& rhs) noexcept
        {
            return !(lhs == rhs);
        }

        friend bool
        operator!=(optional_type const& lhs, OptionalProxy const& rhs) noexcept
        {
            return !(lhs == rhs);
        }

        friend bool
        operator!=(OptionalProxy const& lhs, OptionalProxy const& rhs) noexcept
        {
            return !(lhs == rhs);
        }

        // Emulate boost::optional::value_or
        value_type
        value_or(value_type val) const
        {
            return engaged() ? this->value() : val;
        }

        OptionalProxy&
        operator=(boost::none_t const&);
        OptionalProxy&
        operator=(optional_type&& v);
        OptionalProxy&
        operator=(optional_type const& v);

        template <class U>
        std::enable_if_t<std::is_assignable_v<T, U>, OptionalProxy&>
        operator=(U&& u);

    private:
        friend class STObject;

        OptionalProxy(STObject* st, TypedField<T> const* f);

        bool
        engaged() const noexcept;

        void
        disengage();

        optional_type
        optional_value() const;
    };

    struct Transform
    {
        explicit Transform() = default;

        using argument_type = detail::STVar;
        using result_type = STBase;

        STBase const&
        operator()(detail::STVar const& e) const
        {
            return e.get();
        }
    };

    using list_type = std::vector<detail::STVar>;

    list_type v_;
    SOTemplate const* mType;

public:
    using iterator = boost::
        transform_iterator<Transform, STObject::list_type::const_iterator>;

    class FieldErr : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    static char const*
    getCountedObjectName()
    {
        return "STObject";
    }

    STObject(STObject&&);
    STObject(STObject const&) = default;
    STObject(const SOTemplate& type, SField const& name);
    STObject(
        const SOTemplate& type,
        SerialIter& sit,
        SField const& name) noexcept(false);
    STObject(SerialIter& sit, SField const& name, int depth = 0) noexcept(
        false);
    STObject(SerialIter&& sit, SField const& name) noexcept(false)
        : STObject(sit, name)
    {
    }
    STObject&
    operator=(STObject const&) = default;
    STObject&
    operator=(STObject&& other);

    explicit STObject(SField const& name);

    virtual ~STObject() = default;

    STBase*
    copy(std::size_t n, void* buf) const override
    {
        return emplace(n, buf, *this);
    }

    STBase*
    move(std::size_t n, void* buf) override
    {
        return emplace(n, buf, std::move(*this));
    }

    iterator
    begin() const
    {
        return iterator(v_.begin());
    }

    iterator
    end() const
    {
        return iterator(v_.end());
    }

    bool
    empty() const
    {
        return v_.empty();
    }

    void
    reserve(std::size_t n)
    {
        v_.reserve(n);
    }

    void
    applyTemplate(const SOTemplate& type) noexcept(false);

    void
    applyTemplateFromSField(SField const&) noexcept(false);

    bool
    isFree() const
    {
        return mType == nullptr;
    }

    void
    set(const SOTemplate&);
    bool
    set(SerialIter& u, int depth = 0);

    virtual SerializedTypeID
    getSType() const override
    {
        return STI_OBJECT;
    }
    virtual bool
    isEquivalent(const STBase& t) const override;
    virtual bool
    isDefault() const override
    {
        return v_.empty();
    }

    virtual void
    add(Serializer& s) const override
    {
        add(s, withAllFields);  // just inner elements
    }

    void
    addWithoutSigningFields(Serializer& s) const
    {
        add(s, omitSigningFields);
    }

    // VFALCO NOTE does this return an expensive copy of an object with a
    //             dynamic buffer?
    // VFALCO TODO Remove this function and fix the few callers.
    Serializer
    getSerializer() const
    {
        Serializer s;
        add(s, withAllFields);
        return s;
    }

    virtual std::string
    getFullText() const override;
    virtual std::string
    getText() const override;

    // TODO(tom): options should be an enum.
    virtual Json::Value
    getJson(JsonOptions options) const override;

    template <class... Args>
    std::size_t
    emplace_back(Args&&... args)
    {
        v_.emplace_back(std::forward<Args>(args)...);
        return v_.size() - 1;
    }

    int
    getCount() const
    {
        return v_.size();
    }

    bool setFlag(std::uint32_t);
    bool clearFlag(std::uint32_t);
    bool isFlag(std::uint32_t) const;
    std::uint32_t
    getFlags() const;

    uint256
    getHash(HashPrefix prefix) const;
    uint256
    getSigningHash(HashPrefix prefix) const;

    const STBase&
    peekAtIndex(int offset) const
    {
        return v_[offset].get();
    }
    STBase&
    getIndex(int offset)
    {
        return v_[offset].get();
    }
    const STBase*
    peekAtPIndex(int offset) const
    {
        return &v_[offset].get();
    }
    STBase*
    getPIndex(int offset)
    {
        return &v_[offset].get();
    }

    int
    getFieldIndex(SField const& field) const;
    SField const&
    getFieldSType(int index) const;

    const STBase&
    peekAtField(SField const& field) const;
    STBase&
    getField(SField const& field);
    const STBase*
    peekAtPField(SField const& field) const;
    STBase*
    getPField(SField const& field, bool createOkay = false);

    // these throw if the field type doesn't match, or return default values
    // if the field is optional but not present
    unsigned char
    getFieldU8(SField const& field) const;
    std::uint16_t
    getFieldU16(SField const& field) const;
    std::uint32_t
    getFieldU32(SField const& field) const;
    std::uint64_t
    getFieldU64(SField const& field) const;
    uint128
    getFieldH128(SField const& field) const;

    uint160
    getFieldH160(SField const& field) const;
    uint256
    getFieldH256(SField const& field) const;
    AccountID
    getAccountID(SField const& field) const;

    Blob
    getFieldVL(SField const& field) const;
    STAmount const&
    getFieldAmount(SField const& field) const;
    STPathSet const&
    getFieldPathSet(SField const& field) const;
    const STVector256&
    getFieldV256(SField const& field) const;
    const STArray&
    getFieldArray(SField const& field) const;

    /** Return the value of a field.

        Throws:

            STObject::FieldErr if the field is
            not present.
    */
    template <class T>
    typename T::value_type
    operator[](TypedField<T> const& f) const;

    /** Return the value of a field as boost::optional

        @return boost::none if the field is not present.
    */
    template <class T>
    boost::optional<std::decay_t<typename T::value_type>>
    operator[](OptionaledField<T> const& of) const;

    /** Return a modifiable field value.

        Throws:

            STObject::FieldErr if the field is
            not present.
    */
    template <class T>
    ValueProxy<T>
    operator[](TypedField<T> const& f);

    /** Return a modifiable field value as boost::optional

        The return value equals boost::none if the
        field is not present.
    */
    template <class T>
    OptionalProxy<T>
    operator[](OptionaledField<T> const& of);

    /** Set a field.
        if the field already exists, it is replaced.
    */
    void
    set(std::unique_ptr<STBase> v);

    void
    setFieldU8(SField const& field, unsigned char);
    void
    setFieldU16(SField const& field, std::uint16_t);
    void
    setFieldU32(SField const& field, std::uint32_t);
    void
    setFieldU64(SField const& field, std::uint64_t);
    void
    setFieldH128(SField const& field, uint128 const&);
    void
    setFieldH256(SField const& field, uint256 const&);
    void
    setFieldVL(SField const& field, Blob const&);
    void
    setFieldVL(SField const& field, Slice const&);

    void
    setAccountID(SField const& field, AccountID const&);

    void
    setFieldAmount(SField const& field, STAmount const&);
    void
    setFieldPathSet(SField const& field, STPathSet const&);
    void
    setFieldV256(SField const& field, STVector256 const& v);
    void
    setFieldArray(SField const& field, STArray const& v);

    template <class Tag>
    void
    setFieldH160(SField const& field, base_uint<160, Tag> const& v)
    {
        STBase* rf = getPField(field, true);

        if (!rf)
            throwFieldNotFound(field);

        if (rf->getSType() == STI_NOTPRESENT)
            rf = makeFieldPresent(field);

        using Bits = STBitString<160>;
        if (auto cf = dynamic_cast<Bits*>(rf))
            cf->setValue(v);
        else
            Throw<std::runtime_error>("Wrong field type");
    }

    STObject&
    peekFieldObject(SField const& field);
    STArray&
    peekFieldArray(SField const& field);

    bool
    isFieldPresent(SField const& field) const;
    STBase*
    makeFieldPresent(SField const& field);
    void
    makeFieldAbsent(SField const& field);
    bool
    delField(SField const& field);
    void
    delField(int index);

    bool
    hasMatchingEntry(const STBase&);

    bool
    operator==(const STObject& o) const;
    bool
    operator!=(const STObject& o) const
    {
        return !(*this == o);
    }

private:
    enum WhichFields : bool {
        // These values are carefully chosen to do the right thing if passed
        // to SField::shouldInclude (bool)
        omitSigningFields = false,
        withAllFields = true
    };

    void
    add(Serializer& s, WhichFields whichFields) const;

    // Sort the entries in an STObject into the order that they will be
    // serialized.  Note: they are not sorted into pointer value order, they
    // are sorted by SField::fieldCode.
    static std::vector<STBase const*>
    getSortedFields(STObject const& objToSort, WhichFields whichFields);

    // Implementation for getting (most) fields that return by value.
    //
    // The remove_cv and remove_reference are necessitated by the STBitString
    // types.  Their value() returns by const ref.  We return those types
    // by value.
    template <
        typename T,
        typename V = typename std::remove_cv<typename std::remove_reference<
            decltype(std::declval<T>().value())>::type>::type>
    V
    getFieldByValue(SField const& field) const
    {
        const STBase* rf = peekAtPField(field);

        if (!rf)
            throwFieldNotFound(field);

        SerializedTypeID id = rf->getSType();

        if (id == STI_NOTPRESENT)
            return V();  // optional field not present

        const T* cf = dynamic_cast<const T*>(rf);

        if (!cf)
            Throw<std::runtime_error>("Wrong field type");

        return cf->value();
    }

    // Implementations for getting (most) fields that return by const reference.
    //
    // If an absent optional field is deserialized we don't have anything
    // obvious to return.  So we insist on having the call provide an
    // 'empty' value we return in that circumstance.
    template <typename T, typename V>
    V const&
    getFieldByConstRef(SField const& field, V const& empty) const
    {
        const STBase* rf = peekAtPField(field);

        if (!rf)
            throwFieldNotFound(field);

        SerializedTypeID id = rf->getSType();

        if (id == STI_NOTPRESENT)
            return empty;  // optional field not present

        const T* cf = dynamic_cast<const T*>(rf);

        if (!cf)
            Throw<std::runtime_error>("Wrong field type");

        return *cf;
    }

    // Implementation for setting most fields with a setValue() method.
    template <typename T, typename V>
    void
    setFieldUsingSetValue(SField const& field, V value)
    {
        static_assert(!std::is_lvalue_reference<V>::value, "");

        STBase* rf = getPField(field, true);

        if (!rf)
            throwFieldNotFound(field);

        if (rf->getSType() == STI_NOTPRESENT)
            rf = makeFieldPresent(field);

        T* cf = dynamic_cast<T*>(rf);

        if (!cf)
            Throw<std::runtime_error>("Wrong field type");

        cf->setValue(std::move(value));
    }

    // Implementation for setting fields using assignment
    template <typename T>
    void
    setFieldUsingAssignment(SField const& field, T const& value)
    {
        STBase* rf = getPField(field, true);

        if (!rf)
            throwFieldNotFound(field);

        if (rf->getSType() == STI_NOTPRESENT)
            rf = makeFieldPresent(field);

        T* cf = dynamic_cast<T*>(rf);

        if (!cf)
            Throw<std::runtime_error>("Wrong field type");

        (*cf) = value;
    }

    // Implementation for peeking STObjects and STArrays
    template <typename T>
    T&
    peekField(SField const& field)
    {
        STBase* rf = getPField(field, true);

        if (!rf)
            throwFieldNotFound(field);

        if (rf->getSType() == STI_NOTPRESENT)
            rf = makeFieldPresent(field);

        T* cf = dynamic_cast<T*>(rf);

        if (!cf)
            Throw<std::runtime_error>("Wrong field type");

        return *cf;
    }
};

//------------------------------------------------------------------------------

template <class T>
STObject::Proxy<T>::Proxy(STObject* st, TypedField<T> const* f) : st_(st), f_(f)
{
    if (st_->mType)
    {
        // STObject has associated template
        if (!st_->peekAtPField(*f_))
            Throw<STObject::FieldErr>(
                "Template field error '" + this->f_->getName() + "'");
        style_ = st_->mType->style(*f_);
    }
    else
    {
        style_ = soeINVALID;
    }
}

template <class T>
auto
STObject::Proxy<T>::value() const -> value_type
{
    auto const t = find();
    if (t)
        return t->value();
    if (style_ != soeDEFAULT)
        Throw<STObject::FieldErr>(
            "Missing field '" + this->f_->getName() + "'");
    return value_type{};
}

template <class T>
inline T const*
STObject::Proxy<T>::find() const
{
    return dynamic_cast<T const*>(st_->peekAtPField(*f_));
}

template <class T>
template <class U>
void
STObject::Proxy<T>::assign(U&& u)
{
    if (style_ == soeDEFAULT && u == value_type{})
    {
        st_->makeFieldAbsent(*f_);
        return;
    }
    T* t;
    if (style_ == soeINVALID)
        t = dynamic_cast<T*>(st_->getPField(*f_, true));
    else
        t = dynamic_cast<T*>(st_->makeFieldPresent(*f_));
    assert(t);
    *t = std::forward<U>(u);
}

//------------------------------------------------------------------------------

template <class T>
template <class U>
std::enable_if_t<std::is_assignable_v<T, U>, STObject::ValueProxy<T>&>
STObject::ValueProxy<T>::operator=(U&& u)
{
    this->assign(std::forward<U>(u));
    return *this;
}

template <class T>
STObject::ValueProxy<T>::operator value_type() const
{
    return this->value();
}

template <class T>
STObject::ValueProxy<T>::ValueProxy(STObject* st, TypedField<T> const* f)
    : Proxy<T>(st, f)
{
}

//------------------------------------------------------------------------------

template <class T>
STObject::OptionalProxy<T>::operator bool() const noexcept
{
    return engaged();
}

template <class T>
auto
STObject::OptionalProxy<T>::operator*() const -> value_type
{
    return this->value();
}

template <class T>
STObject::OptionalProxy<T>::operator typename STObject::OptionalProxy<
    T>::optional_type() const
{
    return optional_value();
}

template <class T>
typename STObject::OptionalProxy<T>::optional_type
STObject::OptionalProxy<T>::operator~() const
{
    return optional_value();
}

template <class T>
auto
STObject::OptionalProxy<T>::operator=(boost::none_t const&) -> OptionalProxy&
{
    disengage();
    return *this;
}

template <class T>
auto
STObject::OptionalProxy<T>::operator=(optional_type&& v) -> OptionalProxy&
{
    if (v)
        this->assign(std::move(*v));
    else
        disengage();
    return *this;
}

template <class T>
auto
STObject::OptionalProxy<T>::operator=(optional_type const& v) -> OptionalProxy&
{
    if (v)
        this->assign(*v);
    else
        disengage();
    return *this;
}

template <class T>
template <class U>
std::enable_if_t<std::is_assignable_v<T, U>, STObject::OptionalProxy<T>&>
STObject::OptionalProxy<T>::operator=(U&& u)
{
    this->assign(std::forward<U>(u));
    return *this;
}

template <class T>
STObject::OptionalProxy<T>::OptionalProxy(STObject* st, TypedField<T> const* f)
    : Proxy<T>(st, f)
{
}

template <class T>
bool
STObject::OptionalProxy<T>::engaged() const noexcept
{
    return this->style_ == soeDEFAULT || this->find() != nullptr;
}

template <class T>
void
STObject::OptionalProxy<T>::disengage()
{
    if (this->style_ == soeREQUIRED || this->style_ == soeDEFAULT)
        Throw<STObject::FieldErr>(
            "Template field error '" + this->f_->getName() + "'");
    if (this->style_ == soeINVALID)
        this->st_->delField(*this->f_);
    else
        this->st_->makeFieldAbsent(*this->f_);
}

template <class T>
auto
STObject::OptionalProxy<T>::optional_value() const -> optional_type
{
    if (!engaged())
        return boost::none;
    return this->value();
}

//------------------------------------------------------------------------------

template <class T>
typename T::value_type
STObject::operator[](TypedField<T> const& f) const
{
    auto const b = peekAtPField(f);
    if (!b)
        // This is a free object (no constraints)
        // with no template
        Throw<STObject::FieldErr>("Missing field '" + f.getName() + "'");
    auto const u = dynamic_cast<T const*>(b);
    if (!u)
    {
        assert(mType);
        assert(b->getSType() == STI_NOTPRESENT);
        if (mType->style(f) == soeOPTIONAL)
            Throw<STObject::FieldErr>("Missing field '" + f.getName() + "'");
        assert(mType->style(f) == soeDEFAULT);
        // Handle the case where value_type is a
        // const reference, otherwise we return
        // the address of a temporary.
        static std::decay_t<typename T::value_type> const dv{};
        return dv;
    }
    return u->value();
}

template <class T>
boost::optional<std::decay_t<typename T::value_type>>
STObject::operator[](OptionaledField<T> const& of) const
{
    auto const b = peekAtPField(*of.f);
    if (!b)
        return boost::none;
    auto const u = dynamic_cast<T const*>(b);
    if (!u)
    {
        assert(mType);
        assert(b->getSType() == STI_NOTPRESENT);
        if (mType->style(*of.f) == soeOPTIONAL)
            return boost::none;
        assert(mType->style(*of.f) == soeDEFAULT);
        return typename T::value_type{};
    }
    return u->value();
}

template <class T>
inline auto
STObject::operator[](TypedField<T> const& f) -> ValueProxy<T>
{
    return ValueProxy<T>(this, &f);
}

template <class T>
inline auto
STObject::operator[](OptionaledField<T> const& of) -> OptionalProxy<T>
{
    return OptionalProxy<T>(this, of.f);
}

}  // namespace ripple

#endif
