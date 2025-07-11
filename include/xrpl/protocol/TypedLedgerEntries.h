//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#ifndef TYPED_LEDGER_ENTRIES_H
#define TYPED_LEDGER_ENTRIES_H

#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>

#include <optional>

namespace ripple {

// This is a proxy that returns a strongly-typed object instead of an STObject
template <typename ProxyType>
class STArrayProxy
{
public:
    using value_type = ProxyType;
    using size_type = std::size_t;

    class iterator
    {
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = ProxyType;
        using reference = ProxyType;
        using iterator_category = std::bidirectional_iterator_tag;
        struct pointer
        {
            ProxyType object_;

            ProxyType const*
            operator->() const
            {
                return &object_;
            }

            ProxyType const&
            operator*() const
            {
                return object_;
            }

            ProxyType*
            operator->()
            {
                return &object_;
            }

            ProxyType&
            operator*()
            {
                return object_;
            }
        };

        explicit iterator(STArray::iterator it) : it_(it)
        {
        }

        reference
        operator*()
        {
            return ProxyType::fromObject(*it_);
        }

        pointer
        operator->()
        {
            return pointer{ProxyType::fromObject(*it_)};
        }

        iterator&
        operator++()
        {
            ++it_;
            return *this;
        }

        iterator
        operator++(int)
        {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool
        operator==(iterator const& other) const
        {
            return it_ == other.it_;
        }

        bool
        operator!=(iterator const& other) const
        {
            return it_ != other.it_;
        }

    private:
        STArray::iterator it_;
    };

    explicit STArrayProxy(STArray* array) : array_(array)
    {
    }

    [[nodiscard]] bool
    valid() const
    {
        return array_ != nullptr;
    }

    explicit
    operator bool() const
    {
        return valid();
    }

    /**
     * Returns the underlying STArray
     */
    STArray&
    value()
    {
        return *array_;
    }

    /**
     * Creates an instance of ProxyType. The ProxyType may be something
     * complicated this is just a shortcut.
     * @tparam TArgs types of the arguments
     * @param args arguments
     * @return The instance
     */
    template <typename... TArgs>
    ProxyType
    createItem(TArgs&&... args)
    {
        return ProxyType::create(std::forward<TArgs>(args)...);
    }

    void
    push_back(ProxyType const& obj)
    {
        array_->push_back(obj);
    }

    [[nodiscard]] size_type
    size() const
    {
        return array_ ? array_->size() : 0;
    }

    [[nodiscard]] bool
    empty() const
    {
        return size() == 0;
    }

    [[nodiscard]] ProxyType
    at(size_type idx) const
    {
        if (!array_ || idx >= array_->size())
        {
            return ProxyType::fromObject(nullptr);
        }
        return ProxyType::fromObject((*array_)[idx]);
    }

    ProxyType
    operator[](size_type idx) const
    {
        return at(idx);
    }

    [[nodiscard]] ProxyType
    back() const
    {
        return ProxyType::fromObject(array_->back());
    }

    [[nodiscard]] iterator
    begin() const
    {
        return array_ ? iterator{array_->begin()}
                      : iterator{STArray::iterator{}};
    }

    [[nodiscard]] iterator
    end() const
    {
        return array_ ? iterator{array_->end()} : iterator{STArray::iterator{}};
    }

private:
    STArray* array_;
};

/*
 * We need a way to address each field so that we can map the field names
 * to field types.
 * How it works is:
 * 1. We take the name of an item defined in sfields.macro. e.g. sfScale
 * 2. We prefix it with field_, becoming field_sfScale
 * 3. Put everything into the enum class SFieldNames
 *
 * Then the enum SFieldNames has all the field names defined in it.
*/
#pragma push_macro("UNTYPED_SFIELD")
#undef UNTYPED_SFIELD
#pragma push_macro("TYPED_SFIELD")
#undef TYPED_SFIELD
#pragma push_macro("ARRAY_SFIELD")
#undef ARRAY_SFIELD

#define UNTYPED_SFIELD(sfName, stiSuffix, fieldValue, ...) field_##sfName,
#define TYPED_SFIELD(sfName, stiSuffix, fieldValue, ...) field_##sfName,
#define ARRAY_SFIELD(sfName, sfItemName, stiSuffix, fieldValue, ...) \
    UNTYPED_SFIELD(sfName, stiSuffix, fieldValue, __VA_ARGS__)

enum class SFieldNames {
    field_sfInvalid,
#include <xrpl/protocol/detail/sfields.macro>
};
#undef TYPED_SFIELD
#pragma pop_macro("TYPED_SFIELD")
#undef UNTYPED_SFIELD
#pragma pop_macro("UNTYPED_SFIELD")
#undef ARRAY_SFIELD
#pragma pop_macro("ARRAY_SFIELD")

namespace detail {

// This enum defines what an UNTYPED_SFIELD is holding,
// and we only care about objects and arrays
enum class AggregateFieldTypes {
    NONE,
    OBJECT,
    ARRAY,
    LEDGERENTRY,
    TRANSACTION,
    VALIDATION,
    METADATA,
    PATHSET,
};

// The base case for GetFieldType. We'll define more specialisations for each
// SFieldName so that we can find the field type and the item type if it's
// an array
template <SFieldNames FieldName>
struct GetFieldType
{
    constexpr static AggregateFieldTypes Value = AggregateFieldTypes::NONE;
    constexpr static SFieldNames ItemField = SFieldNames::field_sfInvalid;
};

/*
 * For each field defined in sfields.macro, we will have a template
 * specialisation of GetFieldType for it, and each GetFieldType will
 * have a static Value member and a static ItemField member
 *
 * How this works is:
 * 1. We have a base definition of GetFieldType which basically gives no information.
 * 2. Take an untyped field definition from sfields.macro. e.g. UNTYPED_SFIELD(sfMemo, OBJECT, 10)
 * 3. Create a new template specialisation for GetFieldType, with the template parameter being the corresponding SFieldNames enum we defined earliler
 * 4. Set the Value field to the stiSuffix
 * 5. If it's an object, Value will be AggregateFieldTypes::OBJECT
 * 6. If it's an array, Value will be AggregateFieldTypes::ARRAY, and ItemField will be the value provided in the macro
 * 7. We don't care about the typed fields because we can get the type easily
 */
#pragma push_macro("UNTYPED_SFIELD")
#undef UNTYPED_SFIELD
#pragma push_macro("TYPED_SFIELD")
#undef TYPED_SFIELD
#pragma push_macro("ARRAY_SFIELD")
#undef ARRAY_SFIELD

#define UNTYPED_SFIELD(sfName, stiSuffix, fieldValue, ...)                     \
    template <>                                                                \
    struct GetFieldType<SFieldNames::field_##sfName>                           \
    {                                                                          \
        constexpr static AggregateFieldTypes Value =                           \
            AggregateFieldTypes::stiSuffix;                                    \
        constexpr static SFieldNames ItemField = SFieldNames::field_sfInvalid; \
    };
#define TYPED_SFIELD(sfName, stiSuffix, fieldValue, ...)
#define ARRAY_SFIELD(sfName, sfItemName, stiSuffix, fieldValue, ...) \
    template <>                                                      \
    struct GetFieldType<SFieldNames::field_##sfName>                 \
    {                                                                \
        constexpr static AggregateFieldTypes Value =                 \
            AggregateFieldTypes::stiSuffix;                          \
        constexpr static SFieldNames ItemField =                     \
            SFieldNames::field_##sfItemName;                         \
    };

#include <xrpl/protocol/detail/sfields.macro>

#undef TYPED_SFIELD
#pragma pop_macro("TYPED_SFIELD")
#undef UNTYPED_SFIELD
#pragma pop_macro("UNTYPED_SFIELD")
#undef ARRAY_SFIELD
#pragma pop_macro("ARRAY_SFIELD")

// We reference those `InnerObjects` in the get function when we define the type
// Because of that, We need to push types defined in inner_objects.macro into an
// array so that we can delay evaluating the types.
// This Pair struct stores a field name and a type
template <SFieldNames FieldName, typename Type>
struct InnerObjectTypePair
{
    static constexpr SFieldNames Field = FieldName;
    using InnerObjectType = Type;
};

template <typename T>
concept IsInnerObjectTypePair = requires {
    std::same_as<decltype(T::Field), SFieldNames>;
    typename T::InnerObjectType;
} || std::same_as<T, void>;

// Those `GetInnerObjectStruct` classes iterate through each item in the array,
// and then return the type that the corresponding field name is equal to the
// name we're looking for. It returns void if there's no corresponding field.
template <SFieldNames FieldName, typename Tuple>
struct GetInnerObjectStruct
{
};

template <
    SFieldNames FieldName,
    IsInnerObjectTypePair Current,
    IsInnerObjectTypePair... Pairs>
struct GetInnerObjectStruct<FieldName, std::tuple<Current, Pairs...>>
{
    using Struct = std::conditional_t<
        Current::Field == FieldName,
        typename Current::InnerObjectType,
        typename GetInnerObjectStruct<FieldName, std::tuple<Pairs...>>::Struct>;
};

template <SFieldNames FieldName, IsInnerObjectTypePair Current>
struct GetInnerObjectStruct<FieldName, std::tuple<Current, void>>
{
    using Struct = std::conditional_t<
        Current::Field == FieldName,
        typename Current::InnerObjectType,
        void>;
};

template <SFieldNames FieldName>
struct GetInnerObjectStruct<FieldName, std::tuple<void>>
{
    using Struct = void;
};

// This type returns the value directly if the field is a typed field.
template <
    SFieldNames FieldName,
    SFieldNames ItemFieldName,
    SOEStyle SOEStyleValue,
    AggregateFieldTypes FieldType,
    typename InnerObjectTypesArray>
struct GetFieldValue
{
    template <typename TFieldType>
    static auto
    get(STObject& object, TFieldType const& field)
    {
        return object[field];
    }
};

// This type returns an optional value when the field is marked as optional
template <
    SFieldNames FieldName,
    SFieldNames ItemFieldName,
    typename InnerObjectTypesArray>
struct GetFieldValue<
    FieldName,
    ItemFieldName,
    soeOPTIONAL,
    AggregateFieldTypes::NONE,
    InnerObjectTypesArray>
{
    template <typename TFieldType>
    static auto
    get(STObject& object, TFieldType const& field)
    {
        return object[~field];
    }
};

// This type wraps the STObject value into the corresponding proxy type to
// provide intellisense
template <
    SFieldNames FieldName,
    SFieldNames ItemFieldName,
    SOEStyle SOEStyle,
    typename InnerObjectTypesArray>
struct GetFieldValue<
    FieldName,
    ItemFieldName,
    SOEStyle,
    AggregateFieldTypes::OBJECT,
    InnerObjectTypesArray>
{
    using InnerObjectType =
        typename GetInnerObjectStruct<FieldName, InnerObjectTypesArray>::Struct;
    // If the type is defined in inner_objects.macro, we return the defined type
    // otherwise, we return the untyped STObject
    template <typename TFieldType>
    static std::conditional_t<
        !std::is_void_v<InnerObjectType>,
        InnerObjectType,
        STObject const&>
    get(STObject& object, TFieldType const& field)
    {
        if constexpr (!std::is_void_v<InnerObjectType>)
        {
            return InnerObjectType::fromObject(
                object.getField(field).template downcast<STObject>());
        }
        else
        {
            return object.getField(field).template downcast<STObject>();
        }
    }
};

// This type returns a std::optional when the field is marked as optional.
template <
    SFieldNames FieldName,
    SFieldNames ItemFieldName,
    typename InnerObjectTypesArray>
struct GetFieldValue<
    FieldName,
    ItemFieldName,
    soeOPTIONAL,
    AggregateFieldTypes::OBJECT,
    InnerObjectTypesArray>
{
    using InnerObjectType =
        typename GetInnerObjectStruct<FieldName, InnerObjectTypesArray>::Struct;
    template <typename TFieldType>
    static std::conditional_t<
        !std::is_void_v<InnerObjectType>,
        std::optional<InnerObjectType>,
        STObject const*>
    get(STObject& object, TFieldType const& field)
    {
        if constexpr (!std::is_void_v<InnerObjectType>)
        {
            if (object.isFieldPresent(field))
            {
                return InnerObjectType::fromObject(
                    object.getField(field).template downcast<STObject>());
            }
            return std::nullopt;
        }
        else
        {
            if (object.isFieldPresent(field))
            {
                return &object.getField(field).template downcast<STObject>();
            }
            return nullptr;
        }
    }
};

// This type wraps the STArray value into the strongly-typed array proxy.
template <
    SFieldNames FieldName,
    SFieldNames ItemFieldName,
    SOEStyle Style,
    typename InnerObjectTypesArray>
struct GetFieldValue<
    FieldName,
    ItemFieldName,
    Style,
    AggregateFieldTypes::ARRAY,
    InnerObjectTypesArray>
{
    using InnerObjectType =
        typename GetInnerObjectStruct<ItemFieldName, InnerObjectTypesArray>::
            Struct;

    template <typename TFieldType>
    static std::conditional_t<
        !std::is_void_v<InnerObjectType>,
        STArrayProxy<InnerObjectType>,
        STArray&>
    get(STObject& object, TFieldType const& field)
    {
        if constexpr (!std::is_void_v<InnerObjectType>)
        {
            return STArrayProxy<InnerObjectType>{&object.peekFieldArray(field)};
        }
        else
        {
            return object.peekFieldArray(field);
        }
    }
};

// This type returns an std::optional when the field is marked as optional.
template <
    SFieldNames FieldName,
    SFieldNames ItemFieldName,
    typename InnerObjectTypesArray>
struct GetFieldValue<
    FieldName,
    ItemFieldName,
    soeOPTIONAL,
    AggregateFieldTypes::ARRAY,
    InnerObjectTypesArray>
{
    using InnerObjectType =
        typename GetInnerObjectStruct<ItemFieldName, InnerObjectTypesArray>::
            Struct;

    // If we know what type the STArray holds (i.e. the field is defined by ARRAY_SFIELD and the item type is not Invalid), we return an STArrayProxy, otherwise,
    // we return an untyped STArray
    template <typename TFieldType>
    static std::conditional_t<
        !std::is_void_v<InnerObjectType>,
        STArrayProxy<InnerObjectType>,
        STArray*>
    get(STObject& object, TFieldType const& field)
    {
        if constexpr (!std::is_void_v<InnerObjectType>)
        {
            if (object.isFieldPresent(field))
            {
                return STArrayProxy<InnerObjectType>{
                    &object.peekFieldArray(field)};
            }
            return STArrayProxy<InnerObjectType>{nullptr};
        }
        else
        {
            if (object.isFieldPresent(field))
            {
                return &object.peekFieldArray(field);
            }
            return nullptr;
        }
    }
};

/*
 * This inner_objects.macro is from InnerObjectFormats.cpp.
 * We're defining the inner object type array so that we can use it
 * in the get function.
 * How this works is, for example, the macro file is:
 *
 * INNER_OBJECT_BEGIN
 *
 * INNER_OBJECT(sfSignerEntry,
 *     FIELDS(
 *         FIELD(sfSignerEntry, sfAccount, soeREQUIRED)
 *         FIELD(sfSignerEntry, sfSignerWeight, soeREQUIRED)
 *         FIELD(sfSignerEntry, sfWalletLocator, soeOPTIONAL)
 *     )
 * )
 *
 * INNER_OBJECT_END
 *
 * 1. Replace INNER_OBJECT_BEGIN with using InnerObjectTypesArray = std::tuple<
 * 2. Replace INNER_OBJECT with InnerObjectTypePair<SFieldNames::field_sfSignerEntry, struct InnerObject_sfSignerEntry>,
 * 3. Replace INNER_OBJECT_END with void>;
 *
 * The resulting code is:
 * using InnerObjectTypesArray = std::tuple<InnerObjectTypePair<SFieldNames::field_sfSignerEntry, struct InnerObject_sfSignerEntry>, void>;
 * which we're defining a tuple with some pairs inside. When we want to look up a type from the field name,
 * we iterate through the tuple and then return the pair that the field name is the same as the one we're looking for.
 * The type struct InnerObject_sfSignerEntry yet to be defined, but we'll define it later.
 */
#pragma push_macro("INNER_OBJECT")
#pragma push_macro("FIELDS")
#pragma push_macro("FIELD")
#pragma push_macro("INNER_OBJECT_BEGIN")
#pragma push_macro("INNER_OBJECT_END")

#define INNER_OBJECT_BEGIN using InnerObjectTypesArray = std::tuple <
#define INNER_OBJECT_END void > ;

#define FIELDS(...) __VA_ARGS__
#define FIELD(name, field, soeStyle)

#define INNER_OBJECT(name, fields) \
    InnerObjectTypePair<SFieldNames::field_##name, struct InnerObject_##name>,

#include <xrpl/protocol/detail/inner_objects.macro>

#pragma pop_macro("INNER_OBJECT")
#pragma pop_macro("FIELDS")
#pragma pop_macro("FIELD")
#pragma pop_macro("INNER_OBJECT_BEGIN")
#pragma pop_macro("INNER_OBJECT_END")

/*
 * We're defining each inner object type here.
 *
 * How this works is, for example, the macro file is like this:
 *
 * INNER_OBJECT_BEGIN
 *
 * INNER_OBJECT(sfSignerEntry,
 *     FIELDS(
 *         FIELD(sfSignerEntry, sfAccount, soeREQUIRED)
 *         FIELD(sfSignerEntry, sfSignerWeight, soeREQUIRED)
 *         FIELD(sfSignerEntry, sfWalletLocator, soeOPTIONAL)
 *     )
 * )
 *
 * INNER_OBJECT_END
 *
 * 1. We leave INNER_OBJECT_BEGIN and INNER_OBJECT_END empty as we don't need them
 * 2. Replace INNER_OBJECT with struct InnerObject_sfSignerEntry with some methods defined
 * 3. Replace FIELD with auto fsfSignerEntry() that returns the value of this field
 *
 * In the end, we'll have a struct like the following:
 * struct InnerObject_sfSignerEntry
 * {
 *      static InnerObject_sfSignerEntry fromObject();
 *      static InnerObject_sfSignerEntry create();
 *
 *      ValueProxy<SignerEntry> fsfSignerEntry();
 * };
 *
 */
#pragma push_macro("INNER_OBJECT")
#pragma push_macro("FIELDS")
#pragma push_macro("FIELD")
#pragma push_macro("INNER_OBJECT_BEGIN")
#pragma push_macro("INNER_OBJECT_END")

#define INNER_OBJECT_BEGIN
#define INNER_OBJECT_END

#define FIELDS(...) __VA_ARGS__
#define FIELD(name, field, soeStyle)                                     \
    auto f##field()                                                      \
    {                                                                    \
        return GetFieldValue<                                            \
            SFieldNames::field_##field,                                  \
            detail::GetFieldType<SFieldNames::field_##field>::ItemField, \
            soeStyle,                                                    \
            detail::GetFieldType<SFieldNames::field_##field>::Value,     \
            InnerObjectTypesArray>::get(*getObject(), field);            \
    }

#define INNER_OBJECT(name, fields)                                          \
    struct InnerObject_##name                                               \
    {                                                                       \
    private:                                                                \
        std::variant<std::shared_ptr<STObject>, STObject*> object_;         \
        InnerObject_##name(STObject* object) : object_(object)              \
        {                                                                   \
        }                                                                   \
        InnerObject_##name(std::shared_ptr<STObject> const& object)         \
            : object_(object)                                               \
        {                                                                   \
        }                                                                   \
                                                                            \
    public:                                                                 \
        static constexpr bool IsValidType = true;                           \
        static InnerObject_##name                                           \
        fromObject(STObject& object)                                        \
        {                                                                   \
            return InnerObject_##name{&object};                             \
        }                                                                   \
        static InnerObject_##name                                           \
        fromObject(std::shared_ptr<STObject> const& object)                 \
        {                                                                   \
            return InnerObject_##name{object};                              \
        }                                                                   \
        static InnerObject_##name                                           \
        create()                                                            \
        {                                                                   \
            return InnerObject_##name{std::make_shared<STObject>(name)};    \
        }                                                                   \
        operator STObject const&() const                                    \
        {                                                                   \
            return *getObject();                                            \
        }                                                                   \
        operator STObject&()                                                \
        {                                                                   \
            return *getObject();                                            \
        }                                                                   \
        STObject*                                                           \
        getObject()                                                         \
        {                                                                   \
            if (std::holds_alternative<std::shared_ptr<STObject>>(object_)) \
            {                                                               \
                return std::get<std::shared_ptr<STObject>>(object_).get();  \
            }                                                               \
            else                                                            \
            {                                                               \
                return std::get<STObject*>(object_);                        \
            }                                                               \
        }                                                                   \
        STObject const*                                                     \
        getObject() const                                                   \
        {                                                                   \
            if (std::holds_alternative<std::shared_ptr<STObject>>(object_)) \
            {                                                               \
                return std::get<std::shared_ptr<STObject>>(object_).get();  \
            }                                                               \
            else                                                            \
            {                                                               \
                return std::get<STObject*>(object_);                        \
            }                                                               \
        }                                                                   \
        bool                                                                \
        isValid() const                                                     \
        {                                                                   \
            return getObject() != nullptr;                                  \
        }                                                                   \
        fields                                                              \
    };

#include <xrpl/protocol/detail/inner_objects.macro>

// We're defining each ledger entry here.

#pragma pop_macro("INNER_OBJECT")
#pragma pop_macro("FIELDS")
#pragma pop_macro("FIELD")
#pragma pop_macro("INNER_OBJECT_BEGIN")
#pragma pop_macro("INNER_OBJECT_END")

template <LedgerEntryType EntryType>
struct LedgerEntry
{
};

/*
 * We're defining ledger entries here.
 * This is very similar to what happens above, and in the end, we'll have something like:
 *
 * struct LedgerEntry<ltCheck>
 * {
 *      static LedgerEntry fromObject();
 *      static LedgerEntry create();
 *
 *      ValueProxy<SignerEntry> fsfSignerEntry();
 * };
 *
 * The only difference is that we don't have to define a type array, and
 * we only need template specialisation for them.
 * The reason is that an inner object may return another inner object, but
 * a ledger entry should never have a field that returns another ledger entry,
 * meaning there's not going to be a circular dependency, and a type array
 * allows us to define each type later and look up in it when they're not
 * fully defined.
 */
#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY
#pragma push_macro("LEDGER_ENTRY_DUPLICATE")
#undef LEDGER_ENTRY_DUPLICATE
#pragma push_macro("LEDGER_ENTRY_FIELD")
#undef LEDGER_ENTRY_FIELD
#pragma push_macro("DEFINE_LEDGER_ENTRY_FIELDS")
#undef DEFINE_LEDGER_ENTRY_FIELDS
#pragma push_macro("LEDGER_ENTRIES_BEGIN")
#undef LEDGER_ENTRIES_BEGIN
#pragma push_macro("LEDGER_ENTRIES_END")
#undef LEDGER_ENTRIES_END

#define LEDGER_ENTRIES_BEGIN
#define LEDGER_ENTRIES_END
#define DEFINE_LEDGER_ENTRY_FIELDS(...) __VA_ARGS__
#define LEDGER_ENTRY_FIELD(field, soeStyle)                                   \
    using Field_##field##Type =                                               \
        decltype(GetFieldValue<                                               \
                 SFieldNames::field_##field,                                  \
                 detail::GetFieldType<SFieldNames::field_##field>::ItemField, \
                 soeStyle,                                                    \
                 detail::GetFieldType<SFieldNames::field_##field>::Value,     \
                 InnerObjectTypesArray>::                                     \
                     get(std::declval<STObject&>(), field));                  \
    Field_##field##Type f##field()                                            \
    {                                                                         \
        return GetFieldValue<                                                 \
            SFieldNames::field_##field,                                       \
            detail::GetFieldType<SFieldNames::field_##field>::ItemField,      \
            soeStyle,                                                         \
            detail::GetFieldType<SFieldNames::field_##field>::Value,          \
            InnerObjectTypesArray>::get(*getObject(), field);                 \
    }

#define LEDGER_ENTRY(tag, value, name, rpcName, fields)                       \
    template <>                                                               \
    struct LedgerEntry<tag>                                                   \
    {                                                                         \
    private:                                                                  \
        using CLS = LedgerEntry<tag>;                                         \
        std::variant<std::shared_ptr<STLedgerEntry>, STLedgerEntry*> object_; \
        LedgerEntry(STLedgerEntry& object) : object_(&object)                 \
        {                                                                     \
        }                                                                     \
        LedgerEntry(std::shared_ptr<STLedgerEntry> const& object)             \
            : object_(object)                                                 \
        {                                                                     \
        }                                                                     \
                                                                              \
    public:                                                                   \
        bool                                                                  \
        isValid() const                                                       \
        {                                                                     \
            return getObject() != nullptr;                                    \
        }                                                                     \
        static void                                                           \
        ensureType(STLedgerEntry const& object)                               \
        {                                                                     \
            if (tag != object.getType())                                      \
            {                                                                 \
                Throw<std::runtime_error>("Object type mismatch!");           \
            }                                                                 \
        }                                                                     \
        STLedgerEntry*                                                        \
        getObject()                                                           \
        {                                                                     \
            if (std::holds_alternative<std::shared_ptr<STLedgerEntry>>(       \
                    object_))                                                 \
            {                                                                 \
                return std::get<std::shared_ptr<STLedgerEntry>>(object_)      \
                    .get();                                                   \
            }                                                                 \
            else                                                              \
            {                                                                 \
                return std::get<STLedgerEntry*>(object_);                     \
            }                                                                 \
        }                                                                     \
        STLedgerEntry const*                                                  \
        getObject() const                                                     \
        {                                                                     \
            if (std::holds_alternative<std::shared_ptr<STLedgerEntry>>(       \
                    object_))                                                 \
            {                                                                 \
                return std::get<std::shared_ptr<STLedgerEntry>>(object_)      \
                    .get();                                                   \
            }                                                                 \
            else                                                              \
            {                                                                 \
                return std::get<STLedgerEntry*>(object_);                     \
            }                                                                 \
        }                                                                     \
        static LedgerEntry                                                    \
        fromObject(STLedgerEntry& object)                                     \
        {                                                                     \
            ensureType(object);                                               \
            return LedgerEntry{object};                                       \
        }                                                                     \
        static LedgerEntry                                                    \
        fromObject(std::shared_ptr<STLedgerEntry> const& object)              \
        {                                                                     \
            ensureType(*object);                                              \
            return LedgerEntry{object};                                       \
        }                                                                     \
        static LedgerEntry                                                    \
        create(uint256 const& key)                                            \
        {                                                                     \
            return LedgerEntry{std::make_shared<STLedgerEntry>(tag, key)};    \
        }                                                                     \
        fields                                                                \
    };

#define LEDGER_ENTRY_DUPLICATE(...)

#include <xrpl/protocol/detail/ledger_entries.macro>

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY_DUPLICATE
#pragma pop_macro("LEDGER_ENTRY_DUPLICATE")
#undef LEDGER_ENTRY_FIELD
#pragma pop_macro("LEDGER_ENTRY_FIELD")
#undef DEFINE_LEDGER_ENTRY_FIELDS
#pragma pop_macro("DEFINE_LEDGER_ENTRY_FIELDS")
#undef LEDGER_ENTRIES_BEGIN
#pragma pop_macro("LEDGER_ENTRIES_BEGIN")
#undef LEDGER_ENTRIES_END
#pragma pop_macro("LEDGER_ENTRIES_END")

}  // namespace detail

// This gives an inner object that the field name corresponds to.
template <SFieldNames FieldName>
using InnerObjectType = typename detail::
    GetInnerObjectStruct<FieldName, detail::InnerObjectTypesArray>::Struct;

// This gives a ledger entry type that the LedgerEntryType corresponds to.
template <LedgerEntryType EntryType>
using LedgerObjectType = detail::LedgerEntry<EntryType>;
}  // namespace ripple
#endif  // TYPED_LEDGER_ENTRIES_H
