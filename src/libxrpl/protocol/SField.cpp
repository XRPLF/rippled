#include <xrpl/protocol/SField.h>

#include <xrpl/beast/utility/instrumentation.h>

#include <string>
#include <unordered_map>

namespace xrpl {

/** @file
 *  Single authoritative source of all named XRPL protocol fields.
 *
 *  Every field that can appear in a serialized XRPL object (transaction,
 *  ledger entry, validation, metadata) must be registered here before the
 *  process starts.  Registration happens at static-initialization time via
 *  the `SField` and `TypedField<T>` constructors, which insert each field
 *  into two global lookup tables keyed by field code and by name.
 *
 *  The bulk of field definitions come from expanding
 *  `<xrpl/protocol/detail/sfields.macro>` twice: once with `TYPED_SFIELD`
 *  and `UNTYPED_SFIELD` producing `extern` declarations (in `SField.h`)
 *  and once producing `const` definitions (here).  Four historical fields
 *  (`kSF_INVALID`, `kSF_GENERIC`, `kSF_HASH`, `kSF_INDEX`) are defined
 *  directly because they do not follow standard naming or code conventions.
 */

// Storage for static const members.
SField::IsSigning const SField::kNOT_SIGNING;
int SField::num = 0;
std::unordered_map<int, SField const*> SField::knownCodeToField;
std::unordered_map<std::string, SField const*> SField::knownNameToField;

/** Construction access guard for `SField`.
 *
 *  `PrivateAccessTagT` is forward-declared as a public nested type in
 *  `SField`, but its definition appears only here.  Every `SField` and
 *  `TypedField<T>` constructor requires a value of this type as its first
 *  argument.  Because the type is only constructible inside this translation
 *  unit, no external code can create new `SField` instances — they can only
 *  look up existing ones through `getField()`.
 */
struct SField::PrivateAccessTagT
{
    explicit PrivateAccessTagT() = default;
};

/** Sole token that satisfies the `PrivateAccessTagT` constructor parameter.
 *
 *  Passed to every `SField` / `TypedField<T>` constructor call in this file.
 */
static SField::PrivateAccessTagT access;

/** Forwarding constructor for `TypedField<T>`.
 *
 *  Delegates all arguments to the `SField` constructor, registering the
 *  field in the global lookup tables.  The template type parameter `T`
 *  is compile-time only; it is never stored at runtime.
 *
 *  @tparam T   The serialized C++ type associated with this field (e.g.
 *              `STInteger<uint32_t>` for `SF_UINT32`).
 *  @tparam Args  Constructor argument types forwarded to `SField`.
 *  @param pat  Access token restricting construction to `SField.cpp`.
 *  @param args Arguments forwarded verbatim to the `SField` constructor.
 */
template <class T>
template <class... Args>
TypedField<T>::TypedField(PrivateAccessTagT pat, Args&&... args)
    : SField(pat, std::forward<Args>(args)...)
{
}

// --- Field registry population ---
// Construct all compile-time SFields, and register them in the knownCodeToField
// and knownNameToField databases.

// Use macros for most SField construction to enforce naming conventions.
// Each macro derives the human-readable field name by stripping the "sf" prefix
// from the variable name token via std::string_view(#sfName).substr(2).
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

/** Sentinel returned by `getField()` on a lookup miss; `fieldCode == -1`. */
SField const kSF_INVALID(access, -1, "");
/** Catch-all field for untyped contexts; `fieldCode == 0`. */
SField const kSF_GENERIC(access, 0, "Generic");
/** JSON-only uint256 field carrying an object's hash; not binary-serializable.
 *
 *  `fieldValue == 257 > 256` makes `isDiscardable()` return true, so this
 *  field is present in JSON representations of ledger state but excluded from
 *  binary encoding.
 */
SField const kSF_HASH(access, STI_UINT256, 257, "hash");
/** JSON-only uint256 field carrying an object's ledger key; not binary-serializable.
 *
 *  Like `kSF_HASH`, `fieldValue == 258 > 256` marks this field as discardable.
 */
SField const kSF_INDEX(access, STI_UINT256, 258, "index");

#include <xrpl/protocol/detail/sfields.macro>

#undef TYPED_SFIELD
#pragma pop_macro("TYPED_SFIELD")
#undef UNTYPED_SFIELD
#pragma pop_macro("UNTYPED_SFIELD")

/** Construct a typed, named protocol field and register it in both lookup tables.
 *
 *  Computes `fieldCode = (tid << 16) | fv` and inserts a pointer to `this`
 *  into `knownCodeToField` and `knownNameToField`.  Duplicate codes or names
 *  are caught by `XRPL_ASSERT` in debug builds; in release builds with
 *  `NDEBUG` defined the checks compile away and a duplicate would silently
 *  shadow the earlier registration.
 *
 *  @param tid     Serialized type family (e.g. `STI_UINT32`).
 *  @param fv      Per-type field index; must be < 256 for binary serialization.
 *  @param fn      Human-readable field name (the `sf` prefix is stripped by the
 *                 calling macro before this parameter is supplied).
 *  @param meta    Bitmask of `kSMD_*` metadata flags; defaults to `kSMD_DEFAULT`.
 *  @param signing Whether this field is included in signing payloads; defaults
 *                 to `IsSigning::Yes`.
 */
SField::SField(
    PrivateAccessTagT,
    SerializedTypeID tid,
    int fv,
    char const* fn,
    int meta,
    IsSigning signing)
    : fieldCodeMem(fieldCode(tid, fv))
    , fieldType(tid)
    , fieldValue(fv)
    , fieldName(fn)
    , fieldMeta(meta)
    , fieldNum(++num)
    , signingField(signing)
    , jsonName(fieldName.c_str())
{
    XRPL_ASSERT(
        !knownCodeToField.contains(fieldCodeMem),
        "xrpl::SField::SField(tid,fv,fn,meta,signing) : fieldCode is unique");
    XRPL_ASSERT(
        !knownNameToField.contains(fieldName),
        "xrpl::SField::SField(tid,fv,fn,meta,signing) : fieldName is unique");
    knownCodeToField[fieldCodeMem] = this;
    knownNameToField[fieldName] = this;
}

/** Construct a special-purpose field from a raw field code and name.
 *
 *  Used only for the four historical outliers (`kSF_INVALID`, `kSF_GENERIC`,
 *  `kSF_HASH`, `kSF_INDEX`) whose codes cannot be derived from the standard
 *  `(tid << 16) | fv` formula.  Sets `fieldType` to `STI_UNKNOWN` and
 *  `fieldMeta` to `kSMD_NEVER`; these fields carry no metadata and are
 *  never included in standard serialization.
 *
 *  @param fc  Raw field code; -1 for `kSF_INVALID`, 0 for `kSF_GENERIC`.
 *  @param fn  Human-readable field name.
 */
SField::SField(PrivateAccessTagT, int fc, char const* fn)
    : fieldCodeMem(fc)
    , fieldType(STI_UNKNOWN)
    , fieldValue(0)
    , fieldName(fn)
    , fieldMeta(kSMD_NEVER)
    , fieldNum(++num)
    , signingField(IsSigning::Yes)
    , jsonName(fieldName.c_str())
{
    XRPL_ASSERT(
        !knownCodeToField.contains(fieldCodeMem),
        "xrpl::SField::SField(fc,fn) : fieldCode is unique");
    XRPL_ASSERT(
        !knownNameToField.contains(fieldName), "xrpl::SField::SField(fc,fn) : fieldName is unique");
    knownCodeToField[fieldCodeMem] = this;
    knownNameToField[fieldName] = this;
}

/** Look up a field by its packed field code.
 *
 *  @param code  Packed field code `(SerializedTypeID << 16) | fieldValue`.
 *  @return The registered `SField`, or `kSF_INVALID` if no field with that
 *          code exists.
 */
SField const&
SField::getField(int code)
{
    auto it = knownCodeToField.find(code);

    if (it != knownCodeToField.end())
    {
        return *(it->second);
    }
    return kSF_INVALID;
}

/** Compare two fields by canonical binary-serialization order.
 *
 *  Fields are ordered by `fieldCode = (SerializedTypeID << 16) | fieldValue`,
 *  which sorts first by type family and then by per-type index — matching
 *  the canonical order required by the XRPL wire format.
 *
 *  @param f1  First field to compare.
 *  @param f2  Second field to compare.
 *  @return -1 if `f1` precedes `f2`, 1 if `f1` follows `f2`, or 0 if the
 *          comparison is illegal because either field has a non-positive code
 *          (i.e. `kSF_INVALID` or `kSF_GENERIC`).
 */
int
SField::compare(SField const& f1, SField const& f2)
{
    if ((f1.fieldCodeMem <= 0) || (f2.fieldCodeMem <= 0))
        return 0;

    if (f1.fieldCodeMem < f2.fieldCodeMem)
        return -1;

    if (f2.fieldCodeMem < f1.fieldCodeMem)
        return 1;

    return 0;
}

/** Look up a field by its human-readable name.
 *
 *  Names are stored without the `sf` prefix (e.g. the field `sfAmount` is
 *  registered under the key `"Amount"`).
 *
 *  @param fieldName  The field name string to look up (no `sf` prefix).
 *  @return The registered `SField`, or `kSF_INVALID` if no field with that
 *          name exists.
 */
SField const&
SField::getField(std::string const& fieldName)
{
    auto it = knownNameToField.find(fieldName);

    if (it != knownNameToField.end())
    {
        return *(it->second);
    }
    return kSF_INVALID;
}

}  // namespace xrpl
