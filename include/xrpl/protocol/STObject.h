#ifndef XRPL_PROTOCOL_STOBJECT_H_INCLUDED
#define XRPL_PROTOCOL_STOBJECT_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/detail/STVar.h>

#include <boost/iterator/transform_iterator.hpp>

#include <optional>
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
    // Proxy value for a STBase derived class
    template <class T>
    class Proxy;
    template <class T>
    class ValueProxy;
    template <class T>
    class OptionalProxy;

    struct Transform
    {
        explicit Transform() = default;

        using argument_type = detail::STVar;
        using result_type = STBase;

        STBase const&
        operator()(detail::STVar const& e) const;
    };

    using list_type = std::vector<detail::STVar>;

    list_type v_;
    SOTemplate const* mType;

public:
    using iterator = boost::
        transform_iterator<Transform, STObject::list_type::const_iterator>;

    virtual ~STObject() = default;
    STObject(STObject const&) = default;

    template <typename F>
    STObject(SOTemplate const& type, SField const& name, F&& f)
        : STObject(type, name)
    {
        f(*this);
    }

    STObject&
    operator=(STObject const&) = default;
    STObject(STObject&&);
    STObject&
    operator=(STObject&& other);

    STObject(SOTemplate const& type, SField const& name);
    STObject(SOTemplate const& type, SerialIter& sit, SField const& name);
    STObject(SerialIter& sit, SField const& name, int depth = 0);
    STObject(SerialIter&& sit, SField const& name);
    explicit STObject(SField const& name);

    static STObject
    makeInnerObject(SField const& name);

    iterator
    begin() const;

    iterator
    end() const;

    bool
    empty() const;

    void
    reserve(std::size_t n);

    void
    applyTemplate(SOTemplate const& type);

    void
    applyTemplateFromSField(SField const&);

    bool
    isFree() const;

    void
    set(SOTemplate const&);

    bool
    set(SerialIter& u, int depth = 0);

    SerializedTypeID
    getSType() const override;

    bool
    isEquivalent(STBase const& t) const override;

    bool
    isDefault() const override;

    void
    add(Serializer& s) const override;

    std::string
    getFullText() const override;

    std::string
    getText() const override;

    // TODO(tom): options should be an enum.
    Json::Value getJson(JsonOptions = JsonOptions::none) const override;

    void
    addWithoutSigningFields(Serializer& s) const;

    Serializer
    getSerializer() const;

    template <class... Args>
    std::size_t
    emplace_back(Args&&... args);

    int
    getCount() const;

    bool setFlag(std::uint32_t);
    bool clearFlag(std::uint32_t);
    bool isFlag(std::uint32_t) const;

    std::uint32_t
    getFlags() const;

    uint256
    getHash(HashPrefix prefix) const;

    uint256
    getSigningHash(HashPrefix prefix) const;

    STBase const&
    peekAtIndex(int offset) const;

    STBase&
    getIndex(int offset);

    STBase const*
    peekAtPIndex(int offset) const;

    STBase*
    getPIndex(int offset);

    int
    getFieldIndex(SField const& field) const;

    SField const&
    getFieldSType(int index) const;

    STBase const&
    peekAtField(SField const& field) const;

    STBase&
    getField(SField const& field);

    STBase const*
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
    uint192
    getFieldH192(SField const& field) const;
    uint256
    getFieldH256(SField const& field) const;
    std::int32_t
    getFieldI32(SField const& field) const;
    AccountID
    getAccountID(SField const& field) const;

    Blob
    getFieldVL(SField const& field) const;
    STAmount const&
    getFieldAmount(SField const& field) const;
    STPathSet const&
    getFieldPathSet(SField const& field) const;
    STVector256 const&
    getFieldV256(SField const& field) const;
    // If not found, returns an object constructed with the given field
    STObject
    getFieldObject(SField const& field) const;
    STArray const&
    getFieldArray(SField const& field) const;
    STCurrency const&
    getFieldCurrency(SField const& field) const;
    STNumber const&
    getFieldNumber(SField const& field) const;

    /** Get the value of a field.
        @param A TypedField built from an SField value representing the desired
            object field. In typical use, the TypedField will be implicitly
            constructed.
        @return The value of the specified field.
        @throws STObject::FieldErr if the field is not present.
    */
    template <class T>
    typename T::value_type
    operator[](TypedField<T> const& f) const;

    /** Get the value of a field as a std::optional

        @param An OptionaledField built from an SField value representing the
           desired object field. In typical use, the OptionaledField will be
           constructed by using the ~ operator on an SField.
        @return std::nullopt if the field is not present, else the value of
           the specified field.
    */
    template <class T>
    std::optional<std::decay_t<typename T::value_type>>
    operator[](OptionaledField<T> const& of) const;

    /** Get a modifiable field value.
        @param A TypedField built from an SField value representing the desired
            object field. In typical use, the TypedField will be implicitly
            constructed.
        @return A modifiable reference to the value of the specified field.
        @throws STObject::FieldErr if the field is not present.
    */
    template <class T>
    ValueProxy<T>
    operator[](TypedField<T> const& f);

    /** Return a modifiable field value as std::optional

        @param An OptionaledField built from an SField value representing the
            desired object field. In typical use, the OptionaledField will be
            constructed by using the ~ operator on an SField.
        @return Transparent proxy object to an `optional` holding a modifiable
            reference to the value of the specified field. Returns
            std::nullopt if the field is not present.
    */
    template <class T>
    OptionalProxy<T>
    operator[](OptionaledField<T> const& of);

    /** Get the value of a field.
        @param A TypedField built from an SField value representing the desired
            object field. In typical use, the TypedField will be implicitly
            constructed.
        @return The value of the specified field.
        @throws STObject::FieldErr if the field is not present.
    */
    template <class T>
    typename T::value_type
    at(TypedField<T> const& f) const;

    /** Get the value of a field as std::optional

        @param An OptionaledField built from an SField value representing the
           desired object field. In typical use, the OptionaledField will be
           constructed by using the ~ operator on an SField.
        @return std::nullopt if the field is not present, else the value of
           the specified field.
    */
    template <class T>
    std::optional<std::decay_t<typename T::value_type>>
    at(OptionaledField<T> const& of) const;

    /** Get a modifiable field value.
        @param A TypedField built from an SField value representing the desired
            object field. In typical use, the TypedField will be implicitly
            constructed.
        @return A modifiable reference to the value of the specified field.
        @throws STObject::FieldErr if the field is not present.
    */
    template <class T>
    ValueProxy<T>
    at(TypedField<T> const& f);

    /** Return a modifiable field value as std::optional

        @param An OptionaledField built from an SField value representing the
            desired object field. In typical use, the OptionaledField will be
            constructed by using the ~ operator on an SField.
        @return Transparent proxy object to an `optional` holding a modifiable
            reference to the value of the specified field. Returns
            std::nullopt if the field is not present.
    */
    template <class T>
    OptionalProxy<T>
    at(OptionaledField<T> const& of);

    /** Set a field.
        if the field already exists, it is replaced.
    */
    void
    set(std::unique_ptr<STBase> v);

    void
    set(STBase&& v);

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
    setFieldI32(SField const& field, std::int32_t);
    void
    setFieldVL(SField const& field, Blob const&);
    void
    setFieldVL(SField const& field, Slice const&);

    void
    setAccountID(SField const& field, AccountID const&);

    void
    setFieldAmount(SField const& field, STAmount const&);
    void
    setFieldIssue(SField const& field, STIssue const&);
    void
    setFieldCurrency(SField const& field, STCurrency const&);
    void
    setFieldNumber(SField const& field, STNumber const&);
    void
    setFieldPathSet(SField const& field, STPathSet const&);
    void
    setFieldV256(SField const& field, STVector256 const& v);
    void
    setFieldArray(SField const& field, STArray const& v);
    void
    setFieldObject(SField const& field, STObject const& v);

    template <class Tag>
    void
    setFieldH160(SField const& field, base_uint<160, Tag> const& v);

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
    hasMatchingEntry(STBase const&);

    bool
    operator==(STObject const& o) const;
    bool
    operator!=(STObject const& o) const;

    class FieldErr;

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
    getFieldByValue(SField const& field) const;

    // Implementations for getting (most) fields that return by const reference.
    //
    // If an absent optional field is deserialized we don't have anything
    // obvious to return.  So we insist on having the call provide an
    // 'empty' value we return in that circumstance.
    template <typename T, typename V>
    V const&
    getFieldByConstRef(SField const& field, V const& empty) const;

    // Implementation for setting most fields with a setValue() method.
    template <typename T, typename V>
    void
    setFieldUsingSetValue(SField const& field, V value);

    // Implementation for setting fields using assignment
    template <typename T>
    void
    setFieldUsingAssignment(SField const& field, T const& value);

    // Implementation for peeking STObjects and STArrays
    template <typename T>
    T&
    peekField(SField const& field);

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

//------------------------------------------------------------------------------

template <class T>
class STObject::Proxy
{
public:
    using value_type = typename T::value_type;

    value_type
    value() const;

    value_type
    operator*() const;

    T const*
    operator->() const;

protected:
    STObject* st_;
    SOEStyle style_;
    TypedField<T> const* f_;

    Proxy(Proxy const&) = default;

    Proxy(STObject* st, TypedField<T> const* f);

    T const*
    find() const;

    template <class U>
    void
    assign(U&& u);
};

// Constraint += and -= ValueProxy operators
// to value types that support arithmetic operations
template <typename U>
concept IsArithmetic = std::is_arithmetic_v<U> || std::is_same_v<U, STAmount>;

template <class T>
class STObject::ValueProxy : public Proxy<T>
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

    // Convenience operators for value types supporting
    // arithmetic operations
    template <IsArithmetic U>
    ValueProxy&
    operator+=(U const& u);

    template <IsArithmetic U>
    ValueProxy&
    operator-=(U const& u);

    operator value_type() const;

    template <typename U>
    friend bool
    operator==(U const& lhs, STObject::ValueProxy<T> const& rhs)
    {
        return rhs.value() == lhs;
    }

private:
    friend class STObject;

    ValueProxy(STObject* st, TypedField<T> const* f);
};

template <class T>
class STObject::OptionalProxy : public Proxy<T>
{
private:
    using value_type = typename T::value_type;

    using optional_type = std::optional<typename std::decay<value_type>::type>;

public:
    OptionalProxy(OptionalProxy const&) = default;
    OptionalProxy&
    operator=(OptionalProxy const&) = delete;

    /** Returns `true` if the field is set.

        Fields with soeDEFAULT and set to the
        default value will return `true`
    */
    explicit
    operator bool() const noexcept;

    operator optional_type() const;

    /** Explicit conversion to std::optional */
    optional_type
    operator~() const;

    friend bool
    operator==(OptionalProxy const& lhs, std::nullopt_t) noexcept
    {
        return !lhs.engaged();
    }

    friend bool
    operator==(std::nullopt_t, OptionalProxy const& rhs) noexcept
    {
        return rhs == std::nullopt;
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
    operator!=(OptionalProxy const& lhs, std::nullopt_t) noexcept
    {
        return !(lhs == std::nullopt);
    }

    friend bool
    operator!=(std::nullopt_t, OptionalProxy const& rhs) noexcept
    {
        return !(rhs == std::nullopt);
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

    // Emulate std::optional::value_or
    value_type
    value_or(value_type val) const;

    OptionalProxy&
    operator=(std::nullopt_t const&);
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

class STObject::FieldErr : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

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
    if (style_ == soeINVALID)
    {
        Throw<STObject::FieldErr>("Value requested from invalid STObject.");
    }
    if (style_ != soeDEFAULT)
    {
        Throw<STObject::FieldErr>(
            "Missing field '" + this->f_->getName() + "'");
    }
    return value_type{};
}

template <class T>
auto
STObject::Proxy<T>::operator*() const -> value_type
{
    return this->value();
}

template <class T>
T const*
STObject::Proxy<T>::operator->() const
{
    return this->find();
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
    XRPL_ASSERT(t, "ripple::STObject::Proxy::assign : type cast succeeded");
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

template <typename T>
template <IsArithmetic U>
STObject::ValueProxy<T>&
STObject::ValueProxy<T>::operator+=(U const& u)
{
    this->assign(this->value() + u);
    return *this;
}

template <class T>
template <IsArithmetic U>
STObject::ValueProxy<T>&
STObject::ValueProxy<T>::operator-=(U const& u)
{
    this->assign(this->value() - u);
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
STObject::OptionalProxy<T>::operator=(std::nullopt_t const&) -> OptionalProxy&
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
        return std::nullopt;
    return this->value();
}

template <class T>
typename STObject::OptionalProxy<T>::value_type
STObject::OptionalProxy<T>::value_or(value_type val) const
{
    return engaged() ? this->value() : val;
}

//------------------------------------------------------------------------------

inline STBase const&
STObject::Transform::operator()(detail::STVar const& e) const
{
    return e.get();
}

//------------------------------------------------------------------------------

inline STObject::STObject(SerialIter&& sit, SField const& name)
    : STObject(sit, name)
{
}

inline STObject::iterator
STObject::begin() const
{
    return iterator(v_.begin());
}

inline STObject::iterator
STObject::end() const
{
    return iterator(v_.end());
}

inline bool
STObject::empty() const
{
    return v_.empty();
}

inline void
STObject::reserve(std::size_t n)
{
    v_.reserve(n);
}

inline bool
STObject::isFree() const
{
    return mType == nullptr;
}

inline void
STObject::addWithoutSigningFields(Serializer& s) const
{
    add(s, omitSigningFields);
}

// VFALCO NOTE does this return an expensive copy of an object with a
//             dynamic buffer?
// VFALCO TODO Remove this function and fix the few callers.
inline Serializer
STObject::getSerializer() const
{
    Serializer s;
    add(s, withAllFields);
    return s;
}

template <class... Args>
inline std::size_t
STObject::emplace_back(Args&&... args)
{
    v_.emplace_back(std::forward<Args>(args)...);
    return v_.size() - 1;
}

inline int
STObject::getCount() const
{
    return v_.size();
}

inline STBase const&
STObject::peekAtIndex(int offset) const
{
    return v_[offset].get();
}

inline STBase&
STObject::getIndex(int offset)
{
    return v_[offset].get();
}

inline STBase const*
STObject::peekAtPIndex(int offset) const
{
    return &v_[offset].get();
}

inline STBase*
STObject::getPIndex(int offset)
{
    return &v_[offset].get();
}

template <class T>
typename T::value_type
STObject::operator[](TypedField<T> const& f) const
{
    return at(f);
}

template <class T>
std::optional<std::decay_t<typename T::value_type>>
STObject::operator[](OptionaledField<T> const& of) const
{
    return at(of);
}

template <class T>
inline auto
STObject::operator[](TypedField<T> const& f) -> ValueProxy<T>
{
    return at(f);
}

template <class T>
inline auto
STObject::operator[](OptionaledField<T> const& of) -> OptionalProxy<T>
{
    return at(of);
}

template <class T>
typename T::value_type
STObject::at(TypedField<T> const& f) const
{
    auto const b = peekAtPField(f);
    if (!b)
        // This is a free object (no constraints)
        // with no template
        Throw<STObject::FieldErr>("Missing field: " + f.getName());

    if (auto const u = dynamic_cast<T const*>(b))
        return u->value();

    XRPL_ASSERT(
        mType,
        "ripple::STObject::at(TypedField auto) : field template non-null");
    XRPL_ASSERT(
        b->getSType() == STI_NOTPRESENT,
        "ripple::STObject::at(TypedField auto) : type not present");

    if (mType->style(f) == soeOPTIONAL)
        Throw<STObject::FieldErr>("Missing optional field: " + f.getName());

    XRPL_ASSERT(
        mType->style(f) == soeDEFAULT,
        "ripple::STObject::at(TypedField auto) : template style is default");

    // Used to help handle the case where value_type is a const reference,
    // otherwise we would return the address of a temporary.
    static std::decay_t<typename T::value_type> const dv{};
    return dv;
}

template <class T>
std::optional<std::decay_t<typename T::value_type>>
STObject::at(OptionaledField<T> const& of) const
{
    auto const b = peekAtPField(*of.f);
    if (!b)
        return std::nullopt;
    auto const u = dynamic_cast<T const*>(b);
    if (!u)
    {
        XRPL_ASSERT(
            mType,
            "ripple::STObject::at(OptionaledField auto) : field template "
            "non-null");
        XRPL_ASSERT(
            b->getSType() == STI_NOTPRESENT,
            "ripple::STObject::at(OptionaledField auto) : type not present");
        if (mType->style(*of.f) == soeOPTIONAL)
            return std::nullopt;
        XRPL_ASSERT(
            mType->style(*of.f) == soeDEFAULT,
            "ripple::STObject::at(OptionaledField auto) : template style is "
            "default");
        return typename T::value_type{};
    }
    return u->value();
}

template <class T>
inline auto
STObject::at(TypedField<T> const& f) -> ValueProxy<T>
{
    return ValueProxy<T>(this, &f);
}

template <class T>
inline auto
STObject::at(OptionaledField<T> const& of) -> OptionalProxy<T>
{
    return OptionalProxy<T>(this, of.f);
}

template <class Tag>
void
STObject::setFieldH160(SField const& field, base_uint<160, Tag> const& v)
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

inline bool
STObject::operator!=(STObject const& o) const
{
    return !(*this == o);
}

template <typename T, typename V>
V
STObject::getFieldByValue(SField const& field) const
{
    STBase const* rf = peekAtPField(field);

    if (!rf)
        throwFieldNotFound(field);

    SerializedTypeID id = rf->getSType();

    if (id == STI_NOTPRESENT)
        return V();  // optional field not present

    T const* cf = dynamic_cast<T const*>(rf);

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
STObject::getFieldByConstRef(SField const& field, V const& empty) const
{
    STBase const* rf = peekAtPField(field);

    if (!rf)
        throwFieldNotFound(field);

    SerializedTypeID id = rf->getSType();

    if (id == STI_NOTPRESENT)
        return empty;  // optional field not present

    T const* cf = dynamic_cast<T const*>(rf);

    if (!cf)
        Throw<std::runtime_error>("Wrong field type");

    return *cf;
}

// Implementation for setting most fields with a setValue() method.
template <typename T, typename V>
void
STObject::setFieldUsingSetValue(SField const& field, V value)
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
STObject::setFieldUsingAssignment(SField const& field, T const& value)
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
STObject::peekField(SField const& field)
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

}  // namespace ripple

#endif
