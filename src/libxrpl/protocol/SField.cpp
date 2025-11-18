#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/SField.h>

#include <map>
#include <string>

namespace ripple {

// Storage for static const members.
SField::IsSigning const SField::notSigning;
int SField::num = 0;
std::unordered_map<int, SField const*> SField::knownCodeToField;
std::unordered_map<std::string, SField const*> SField::knownNameToField;

// Give only this translation unit permission to construct SFields
struct SField::private_access_tag_t
{
    explicit private_access_tag_t() = default;
};

static SField::private_access_tag_t access;

template <class T>
template <class... Args>
TypedField<T>::TypedField(private_access_tag_t pat, Args&&... args)
    : SField(pat, std::forward<Args>(args)...)
{
}

// Construct all compile-time SFields, and register them in the knownCodeToField
// and knownNameToField databases:

// Use macros for most SField construction to enforce naming conventions.
#pragma push_macro("UNTYPED_SFIELD")
#undef UNTYPED_SFIELD
#pragma push_macro("TYPED_SFIELD")
#undef TYPED_SFIELD

#define UNTYPED_SFIELD(sfName, stiSuffix, fieldValue, ...) \
    SField const sfName(                                   \
        access,                                            \
        STI_##stiSuffix,                                   \
        fieldValue,                                        \
        std::string_view(#sfName).substr(2).data(),        \
        ##__VA_ARGS__);
#define TYPED_SFIELD(sfName, stiSuffix, fieldValue, ...) \
    SF_##stiSuffix const sfName(                         \
        access,                                          \
        STI_##stiSuffix,                                 \
        fieldValue,                                      \
        std::string_view(#sfName).substr(2).data(),      \
        ##__VA_ARGS__);

// SFields which, for historical reasons, do not follow naming conventions.
SField const sfInvalid(access, -1, "");
SField const sfGeneric(access, 0, "Generic");
// The following two fields aren't used anywhere, but they break tests/have
// downstream effects.
SField const sfHash(access, STI_UINT256, 257, "hash");
SField const sfIndex(access, STI_UINT256, 258, "index");

#include <xrpl/protocol/detail/sfields.macro>

#undef TYPED_SFIELD
#pragma pop_macro("TYPED_SFIELD")
#undef UNTYPED_SFIELD
#pragma pop_macro("UNTYPED_SFIELD")

SField::SField(
    private_access_tag_t,
    SerializedTypeID tid,
    int fv,
    char const* fn,
    int meta,
    IsSigning signing)
    : fieldCode(field_code(tid, fv))
    , fieldType(tid)
    , fieldValue(fv)
    , fieldName(fn)
    , fieldMeta(meta)
    , fieldNum(++num)
    , signingField(signing)
    , jsonName(fieldName.c_str())
{
    XRPL_ASSERT(
        !knownCodeToField.contains(fieldCode),
        "ripple::SField::SField(tid,fv,fn,meta,signing) : fieldCode is unique");
    XRPL_ASSERT(
        !knownNameToField.contains(fieldName),
        "ripple::SField::SField(tid,fv,fn,meta,signing) : fieldName is unique");
    knownCodeToField[fieldCode] = this;
    knownNameToField[fieldName] = this;
}

SField::SField(private_access_tag_t, int fc, char const* fn)
    : fieldCode(fc)
    , fieldType(STI_UNKNOWN)
    , fieldValue(0)
    , fieldName(fn)
    , fieldMeta(sMD_Never)
    , fieldNum(++num)
    , signingField(IsSigning::yes)
    , jsonName(fieldName.c_str())
{
    XRPL_ASSERT(
        !knownCodeToField.contains(fieldCode),
        "ripple::SField::SField(fc,fn) : fieldCode is unique");
    XRPL_ASSERT(
        !knownNameToField.contains(fieldName),
        "ripple::SField::SField(fc,fn) : fieldName is unique");
    knownCodeToField[fieldCode] = this;
    knownNameToField[fieldName] = this;
}

SField const&
SField::getField(int code)
{
    auto it = knownCodeToField.find(code);

    if (it != knownCodeToField.end())
    {
        return *(it->second);
    }
    return sfInvalid;
}

int
SField::compare(SField const& f1, SField const& f2)
{
    // -1 = f1 comes before f2, 0 = illegal combination, 1 = f1 comes after f2
    if ((f1.fieldCode <= 0) || (f2.fieldCode <= 0))
        return 0;

    if (f1.fieldCode < f2.fieldCode)
        return -1;

    if (f2.fieldCode < f1.fieldCode)
        return 1;

    return 0;
}

SField const&
SField::getField(std::string const& fieldName)
{
    auto it = knownNameToField.find(fieldName);

    if (it != knownNameToField.end())
    {
        return *(it->second);
    }
    return sfInvalid;
}

}  // namespace ripple
