/** @file
 *  Defines `STObject`, the heterogeneous field container that underlies every
 *  XRPL transaction, ledger entry, and inner object.
 *
 *  `STObject` supports two operating modes: *free mode* (no schema, insertion
 *  order preserved) and *template mode* (schema enforced via `SOTemplate`,
 *  O(1) field lookup).  Both `STTx` and `STLedgerEntry` are `final` subclasses.
 *
 *  The proxy system (`ValueProxy`, `OptionalProxy`) provides type-safe,
 *  compile-time-checked field access via `operator[]` and `at()`, replacing
 *  the older `getFieldU32()`/`setFieldU32()` family for new code.
 */
#pragma once

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

namespace xrpl {

class STArray;

/** Throw a `std::runtime_error` indicating a missing field.
 *
 *  Used by the legacy typed-accessor family (`getFieldByValue`,
 *  `getFieldByConstRef`, `setFieldUsingSetValue`) when `peekAtPField` returns
 *  null.  Not intended for direct use by callers outside `STObject`.
 *
 *  @param field  The field that could not be found.
 *  @throws std::runtime_error always.
 */
inline void
throwFieldNotFound(SField const& field)
{
    Throw<std::runtime_error>("Field not found: " + field.getName());
}

/** Heterogeneous, field-keyed container for XRPL protocol objects.
 *
 *  Stores an ordered sequence of `STBase`-derived fields, keyed by `SField`.
 *  Operates in one of two modes:
 *  - *Free mode* (`isFree() == true`): no schema; fields are stored in
 *    insertion order and any field may be added.
 *  - *Template mode* (`isFree() == false`): an `SOTemplate` constrains
 *    which fields are present, enforces `soeREQUIRED`/`soeOPTIONAL`/
 *    `soeDEFAULT` semantics, and enables O(1) field lookup.
 *
 *  `STTx` and `STLedgerEntry` are the primary `final` subclasses.
 *  Field access is available via the modern proxy API (`operator[]`, `at()`)
 *  or the legacy typed-accessor family (`getFieldU32()`, etc.).
 *
 *  @note `operator==` compares only wire-representable (`isBinary()`) fields.
 */
class STObject : public STBase, public CountedObject<STObject>
{
    template <class T>
    class Proxy;
    template <class T>
    class ValueProxy;
    template <class T>
    class OptionalProxy;

    /** Functor used by `boost::transform_iterator` to project `STVar→STBase`. */
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
    SOTemplate const* type_{};

public:
    /** Forward iterator over the fields of this object as `STBase const&`. */
    using iterator = boost::transform_iterator<Transform, STObject::list_type::const_iterator>;

    ~STObject() override = default;
    STObject(STObject const&) = default;

    /** Construct a templated `STObject` from a schema, field name, and
     *  an initializer callable.
     *
     *  Delegates to `STObject(type, name)` then calls `f(*this)`, allowing
     *  fields to be populated inline at construction time.
     *
     *  @param type  The SOTemplate that defines the object's layout.
     *  @param name  The SField identifying this object within its parent.
     *  @param f     Callable `void(STObject&)` invoked after template init.
     */
    template <typename F>
    STObject(SOTemplate const& type, SField const& name, F&& f) : STObject(type, name)
    {
        f(*this);
    }

    STObject&
    operator=(STObject const&) = default;
    /** Move-construct, transferring field storage and template pointer. */
    STObject(STObject&&);
    /** Move-assign, transferring field storage and template pointer. */
    STObject&
    operator=(STObject&& other);

    /** Construct a templated `STObject` pre-populated from a schema.
     *
     *  Every slot in `type` is initialized: `soeREQUIRED` fields receive their
     *  type-default value; `soeOPTIONAL` and `soeDEFAULT` fields receive the
     *  `STI_NOTPRESENT` sentinel.
     *
     *  @param type  The SOTemplate that defines the object's layout.
     *  @param name  The SField identifying this object within its parent.
     */
    STObject(SOTemplate const& type, SField const& name);

    /** Construct a templated `STObject` by deserializing from a byte stream.
     *
     *  Reads fields in free mode from `sit`, then calls `applyTemplate(type)`
     *  to reorder and validate them against the schema.
     *
     *  @param type  The SOTemplate to enforce after deserialization.
     *  @param sit   The byte stream to deserialize from.
     *  @param name  The SField identifying this object within its parent.
     *  @throws FieldErr if a required field is missing or an unknown
     *      non-discardable field is present.
     */
    STObject(SOTemplate const& type, SerialIter& sit, SField const& name);

    /** Construct a free-mode `STObject` by deserializing from a byte stream.
     *
     *  The `depth` parameter guards against stack exhaustion when parsing
     *  deeply nested structures from untrusted input; nesting beyond 10
     *  throws `std::runtime_error`.
     *
     *  @param sit    The byte stream to deserialize from.
     *  @param name   The SField identifying this object within its parent.
     *  @param depth  Current nesting depth (default 0); capped at 10.
     *  @throws std::runtime_error if depth exceeds 10 or data is malformed.
     */
    STObject(SerialIter& sit, SField const& name, int depth = 0);

    /** Construct from an rvalue `SerialIter`; delegates to the lvalue overload. */
    STObject(SerialIter&& sit, SField const& name);

    /** Construct a free-mode (schema-less) `STObject` with the given field name. */
    explicit STObject(SField const& name);

    /** Create a free-mode inner object, conditionally binding a schema template.
     *
     *  Checks the ambient `getCurrentTransactionRules()` to determine whether
     *  the `fixInnerObjTemplate` or `fixInnerObjTemplate2` amendments are
     *  active, and applies the corresponding `SOTemplate` from
     *  `InnerObjectFormats` when they are.  This amendment-gated behaviour
     *  preserves replay compatibility with historical ledger data serialized
     *  before schemas existed.
     *
     *  @param name  The SField identifying the type of inner object to create.
     *  @return A new `STObject`, bound to its schema when the active rules permit.
     */
    static STObject
    makeInnerObject(SField const& name);

    /** Return an iterator to the first field in this object. */
    [[nodiscard]] iterator
    begin() const;

    /** Return a past-the-end iterator for this object's fields. */
    [[nodiscard]] iterator
    end() const;

    /** Return `true` when this object contains no fields. */
    [[nodiscard]] bool
    empty() const;

    /** Reserve storage for at least `n` fields in the underlying vector. */
    void
    reserve(std::size_t n);

    /** Validate and reorder fields against a schema after free-mode deserialization.
     *
     *  Rebuilds the internal storage in template order.  `soeREQUIRED` fields
     *  that are missing throw; `soeDEFAULT` fields whose serialized value equals
     *  the type's zero value are rejected (explicit defaults are forbidden);
     *  unknown non-discardable fields throw.
     *
     *  @param type  The SOTemplate to enforce.
     *  @throws FieldErr on required-field missing, explicit default value, or
     *      unknown non-discardable field.
     */
    void
    applyTemplate(SOTemplate const& type);

    /** Look up and apply the schema registered for `sField` in `InnerObjectFormats`.
     *
     *  No-op when no template is registered for the given field.
     *
     *  @param sField  The SField whose registered SOTemplate should be applied.
     *  @throws FieldErr (from `applyTemplate`) if the object does not conform.
     */
    void
    applyTemplateFromSField(SField const&);

    /** Return `true` when no schema template is associated with this object. */
    [[nodiscard]] bool
    isFree() const;

    /** Initialize this object from a template, pre-populating every slot.
     *
     *  Clears existing fields and rebuilds in template order.  `soeREQUIRED`
     *  fields receive their type-default value; all other fields receive the
     *  `STI_NOTPRESENT` sentinel.
     *
     *  @param type  The SOTemplate that defines the layout of this object.
     */
    void
    set(SOTemplate const&);

    /** Deserialize fields from a byte stream into this object (free mode).
     *
     *  Reads `(type, field)` ID pairs from `u`, constructs each child `STVar`
     *  at `depth+1`, and calls `applyTemplateFromSField()` on nested
     *  `STObject` children.  Stops at an inner-object terminator byte.
     *
     *  @param u      The byte stream to read from.
     *  @param depth  Current nesting depth; guards against deeply nested input.
     *  @return `true` if an inner-object terminator was consumed; `false`
     *      at top-level end-of-stream.
     *  @throws std::runtime_error on malformed data or duplicate fields.
     */
    bool
    set(SerialIter& u, int depth = 0);

    /** Return `STI_OBJECT`. */
    [[nodiscard]] SerializedTypeID
    getSType() const override;

    /** Return `true` when the objects have the same wire-representable fields.
     *
     *  Only fields where `SField::isBinary()` is true participate in the
     *  comparison.  Non-binary (JSON-only) fields are ignored.
     *
     *  @param t  The other serialized type to compare against.
     */
    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    /** Return `true` when this object holds no fields (empty storage). */
    [[nodiscard]] bool
    isDefault() const override;

    /** Serialize all fields (including signing fields) into `s`. */
    void
    add(Serializer& s) const override;

    /** Return a human-readable, field-by-field description of this object. */
    [[nodiscard]] std::string
    getFullText() const override;

    /** Return a brief textual description of this object. */
    [[nodiscard]] std::string
    getText() const override;

    // TODO(tom): options should be an enum.
    /** Convert this object to a JSON value.
     *
     *  @param options  Controls formatting options (e.g., binary vs. human-readable).
     */
    [[nodiscard]] json::Value getJson(JsonOptions = JsonOptions::Values::None) const override;

    /** Serialize signing-eligible fields only into `s`.
     *
     *  Excludes fields whose `SField::shouldInclude(false)` returns `false`
     *  (e.g., `sfTxnSignature`, `sfSigners`).  Used to produce the payload
     *  that is hashed and signed or verified.
     *
     *  @param s  The serializer to append to.
     */
    void
    addWithoutSigningFields(Serializer& s) const;

    /** Return a `Serializer` containing all fields (including signing fields).
     *
     *  @note Produces a full copy of the serialized form; prefer
     *      `add(Serializer&)` when appending to an existing buffer.
     */
    [[nodiscard]] Serializer
    getSerializer() const;

    /** Append a new field directly to the internal storage vector.
     *
     *  Used during deserialization and by the proxy system when adding new
     *  fields to a free-mode object.  Arguments are forwarded to
     *  `detail::STVar`'s constructor.
     *
     *  @return The zero-based index of the newly added field.
     */
    template <class... Args>
    std::size_t
    emplaceBack(Args&&... args);

    /** Return the number of fields currently stored in this object. */
    [[nodiscard]] int
    getCount() const;

    /** Set a flag bit in `sfFlags`, creating the field if absent.
     *
     *  @param flag  The flag bit(s) to set (OR-ed into existing flags).
     *  @return `true` if the flags field was changed.
     */
    bool
    setFlag(std::uint32_t);

    /** Clear a flag bit in `sfFlags`.
     *
     *  @param flag  The flag bit(s) to clear.
     *  @return `true` if the flags field was changed.
     */
    bool
    clearFlag(std::uint32_t);

    /** Return `true` when all bits in `flag` are set in `sfFlags`. */
    [[nodiscard]] bool
    isFlag(std::uint32_t) const;

    /** Return the value of `sfFlags`, or 0 if the field is absent. */
    [[nodiscard]] std::uint32_t
    getFlags() const;

    /** Compute a domain-separated hash of all fields (including signing fields).
     *
     *  Prepends `prefix` before serializing all fields, then returns the
     *  `sha512Half` of the result.
     *
     *  @param prefix  The `HashPrefix` discriminator for this hash domain.
     *  @return The 256-bit hash.
     */
    [[nodiscard]] uint256
    getHash(HashPrefix prefix) const;

    /** Compute a domain-separated hash of signing-eligible fields only.
     *
     *  Equivalent to `getHash` but uses `addWithoutSigningFields` to exclude
     *  signature-carrying fields.  Used by single-sig and multi-sig verification.
     *
     *  @param prefix  The `HashPrefix` discriminator for this hash domain.
     *  @return The 256-bit hash.
     */
    [[nodiscard]] uint256
    getSigningHash(HashPrefix prefix) const;

    /** Return the field at `offset` by const reference; no bounds check. */
    [[nodiscard]] STBase const&
    peekAtIndex(int offset) const;

    /** Return the field at `offset` by mutable reference; no bounds check. */
    STBase&
    getIndex(int offset);

    /** Return a const pointer to the field at `offset`; no bounds check. */
    [[nodiscard]] STBase const*
    peekAtPIndex(int offset) const;

    /** Return a mutable pointer to the field at `offset`; no bounds check. */
    STBase*
    getPIndex(int offset);

    /** Return the storage index of `field`, or -1 if not present. */
    [[nodiscard]] int
    getFieldIndex(SField const& field) const;

    /** Return the `SField` descriptor for the field at storage index `index`. */
    [[nodiscard]] SField const&
    getFieldSType(int index) const;

    /** Return the field identified by `field` by const reference.
     *
     *  @throws std::runtime_error if the field is not present.
     */
    [[nodiscard]] STBase const&
    peekAtField(SField const& field) const;

    /** Return the field identified by `field` by mutable reference.
     *
     *  @throws std::runtime_error if the field is not present.
     */
    STBase&
    getField(SField const& field);

    /** Return a const pointer to the field identified by `field`, or `nullptr`.
     *
     *  Does not create the field.  Returns `nullptr` for absent optional fields
     *  in free mode; returns a pointer to the `STI_NOTPRESENT` sentinel in
     *  template mode.
     */
    [[nodiscard]] STBase const*
    peekAtPField(SField const& field) const;

    /** Core field-lookup primitive, optionally creating the field.
     *
     *  In template mode with `createOkay == false`, returns the stored slot
     *  pointer (which may be an `STI_NOTPRESENT` sentinel).  With
     *  `createOkay == true`, promotes `STI_NOTPRESENT` sentinels and
     *  appends missing free-mode fields.  Returns `nullptr` only when the
     *  field is absent in free mode and `createOkay` is false.
     *
     *  @param field       The field to locate.
     *  @param createOkay  When `true`, create the field if absent.
     *  @return Pointer to the stored `STBase`, or `nullptr`.
     */
    STBase*
    getPField(SField const& field, bool createOkay = false);

    /** @name Legacy typed field accessors
     *
     *  These methods return field values by their native C++ type.  They throw
     *  `std::runtime_error` if the field type does not match the expected type,
     *  and return a default-constructed value when an optional field is absent.
     *  Prefer the proxy API (`operator[]`, `at()`) for new code.
     *  @{
     */
    [[nodiscard]] unsigned char
    getFieldU8(SField const& field) const;
    [[nodiscard]] std::uint16_t
    getFieldU16(SField const& field) const;
    [[nodiscard]] std::uint32_t
    getFieldU32(SField const& field) const;
    [[nodiscard]] std::uint64_t
    getFieldU64(SField const& field) const;
    [[nodiscard]] uint128
    getFieldH128(SField const& field) const;
    [[nodiscard]] uint160
    getFieldH160(SField const& field) const;
    [[nodiscard]] uint192
    getFieldH192(SField const& field) const;
    [[nodiscard]] uint256
    getFieldH256(SField const& field) const;
    [[nodiscard]] std::int32_t
    getFieldI32(SField const& field) const;
    [[nodiscard]] AccountID
    getAccountID(SField const& field) const;
    [[nodiscard]] Blob
    getFieldVL(SField const& field) const;
    [[nodiscard]] STAmount const&
    getFieldAmount(SField const& field) const;
    [[nodiscard]] STPathSet const&
    getFieldPathSet(SField const& field) const;
    [[nodiscard]] STVector256 const&
    getFieldV256(SField const& field) const;
    /** Return the nested `STObject` for `field` by value.
     *
     *  If the field is absent, returns a default-constructed `STObject`
     *  initialized with `field` as its name.  Modifications to the returned
     *  copy do not propagate back; use `peekFieldObject()` for in-place access.
     */
    [[nodiscard]] STObject
    getFieldObject(SField const& field) const;
    [[nodiscard]] STArray const&
    getFieldArray(SField const& field) const;
    [[nodiscard]] STCurrency const&
    getFieldCurrency(SField const& field) const;
    [[nodiscard]] STNumber const&
    getFieldNumber(SField const& field) const;
    /** @} */

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
    [[nodiscard]] typename T::value_type
    at(TypedField<T> const& f) const;

    /** Get the value of a field as std::optional

        @param An OptionaledField built from an SField value representing the
           desired object field. In typical use, the OptionaledField will be
           constructed by using the ~ operator on an SField.
        @return std::nullopt if the field is not present, else the value of
           the specified field.
    */
    template <class T>
    [[nodiscard]] std::optional<std::decay_t<typename T::value_type>>
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

    /** Replace or insert a field from a heap-allocated `STBase`.
     *
     *  If a field with the same `SField` already exists, it is replaced.
     *
     *  @param v  The field to store; ownership is transferred.
     */
    void
    set(std::unique_ptr<STBase> v);

    /** Replace or insert a field by move.
     *
     *  If a field with the same `SField` already exists, it is replaced.
     *
     *  @param v  The field to move into this object.
     */
    void
    set(STBase&& v);

    /** @name Legacy typed field mutators
     *
     *  Set a named field to the given value.  Throws `std::runtime_error` if
     *  the stored field has a different type than the value being set.
     *  Prefer the proxy API (`operator[]`, `at()`) for new code.
     *  @{
     */
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
    setFieldH192(SField const& field, uint192 const&);
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

    /** Set a 160-bit hash field from any `BaseUInt<160, Tag>` value.
     *
     *  @tparam Tag  The phantom tag type of the `BaseUInt` specialization.
     *  @param field  The field to set.
     *  @param v      The 160-bit value to store.
     *  @throws std::runtime_error if the stored field has a different type.
     */
    template <class Tag>
    void
    setFieldH160(SField const& field, BaseUInt<160, Tag> const& v);
    /** @} */

    /** Return a mutable reference to the nested `STObject` for `field`.
     *
     *  Unlike `getFieldObject()`, the returned reference is into internal
     *  storage; mutations propagate back to this object.
     *
     *  @throws std::runtime_error if the field is absent or has the wrong type.
     */
    STObject&
    peekFieldObject(SField const& field);

    /** Return a mutable reference to the nested `STArray` for `field`.
     *
     *  The returned reference is into internal storage; mutations propagate
     *  back to this object.
     *
     *  @throws std::runtime_error if the field is absent or has the wrong type.
     */
    STArray&
    peekFieldArray(SField const& field);

    /** Return `true` when `field` is present and not an `STI_NOTPRESENT` sentinel. */
    [[nodiscard]] bool
    isFieldPresent(SField const& field) const;

    /** Promote an `STI_NOTPRESENT` sentinel field to a live default value.
     *
     *  In template mode, converts an optional/default slot from the sentinel
     *  state to a type-correct default value, making the field "present".
     *  In free mode, appends a new default-constructed field.
     *
     *  @param field  The field to make present.
     *  @return Pointer to the now-live field, or `nullptr` if not found.
     */
    STBase*
    makeFieldPresent(SField const& field);

    /** Demote a live optional field back to the `STI_NOTPRESENT` sentinel.
     *
     *  In template mode, replaces the field's value with the sentinel.
     *  In free mode, removes the field entry entirely.
     *  Only valid for `soeOPTIONAL` fields; called by the proxy system
     *  when assigning a `soeDEFAULT` field its zero value.
     *
     *  @param field  The field to make absent.
     */
    void
    makeFieldAbsent(SField const& field);

    /** Remove the field identified by `field` unconditionally.
     *
     *  @param field  The field to remove.
     *  @return `true` if the field was found and removed.
     */
    bool
    delField(SField const& field);

    /** Remove the field at storage index `index` unconditionally. */
    void
    delField(int index);

    /** Return the `SOEStyle` (`soeREQUIRED`, `soeOPTIONAL`, `soeDEFAULT`)
     *  for `field` according to the associated template.
     *
     *  @param field  The field to query.
     *  @return The style, or `SoeInvalid` if the object is in free mode.
     */
    [[nodiscard]] SOEStyle
    getStyle(SField const& field) const;

    /** Return `true` when any stored field compares equal to `entry`.
     *
     *  Used to test membership in collections such as `STArray`.
     */
    [[nodiscard]] bool
    hasMatchingEntry(STBase const&) const;

    /** Compare two `STObject` instances for equality.
     *
     *  Only wire-representable (`isBinary()`) fields participate.
     *  This comparison is O(n²) by design.
     *
     *  @note For fast same-template comparison, use `isEquivalent()` which
     *      short-circuits when both objects share the same `mType` pointer.
     */
    bool
    operator==(STObject const& o) const;
    bool
    operator!=(STObject const& o) const;

    /** Exception thrown by the proxy and `at()` accessors on field errors.
     *
     *  Raised when a required field is absent, a template constraint is
     *  violated, or an invalid field access is attempted on a free-mode object.
     */
    class FieldErr;

private:
    /** Selects which fields are included during serialization.
     *
     *  The underlying `bool` values alias directly to `SField::shouldInclude(bool)`
     *  so they can be passed without translation:
     *  - `OmitSigningFields` (false) — exclude fields not intended for signing.
     *  - `WithAllFields` (true) — include every field.
     */
    enum class WhichFields : bool {
        // These values are carefully chosen to do the right thing if passed
        // to SField::shouldInclude (bool)
        OmitSigningFields = false,
        WithAllFields = true
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
        typename V = std::remove_cv_t<std::remove_reference_t<decltype(std::declval<T>().value())>>>
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

/** Common base for `ValueProxy<T>` and `OptionalProxy<T>`.
 *
 *  Stores a back-pointer to the owning `STObject`, the `SOEStyle` of the
 *  field, and a typed descriptor pointer.  Provides the read path
 *  (`value()`, `operator*`, `operator->`) and the write primitive `assign()`.
 *
 *  `assign()` enforces `soeDEFAULT` canonicalization: assigning the zero
 *  value calls `makeFieldAbsent` rather than storing an explicit default,
 *  preserving canonical wire format.
 *
 *  @tparam T  The concrete `STBase`-derived type carrying the field's value.
 */
template <class T>
class STObject::Proxy
{
public:
    using value_type = typename T::value_type;

    /** Return the field's current value.
     *
     *  For `soeDEFAULT` fields that are absent, returns a default-constructed
     *  `value_type`.  Throws `FieldErr` for absent `soeOPTIONAL` or required
     *  fields, and when called on a free-mode object with no template.
     */
    [[nodiscard]] value_type
    value() const;

    /** Dereference operator; equivalent to `value()`. */
    value_type
    operator*() const;

    /// Do not use operator->() unless the field is required, or you've checked
    /// that it's set.
    T const*
    operator->() const;

protected:
    STObject* st_;
    SOEStyle style_;
    TypedField<T> const* f_;

    Proxy(Proxy const&) = default;

    Proxy(STObject* st, TypedField<T> const* f);

    /** Locate the field via `dynamic_cast`; returns `nullptr` when absent. */
    [[nodiscard]] T const*
    find() const;

    /** Write `u` into the field, applying `soeDEFAULT` canonicalization. */
    template <class U>
    void
    assign(U&& u);
};

/** Satisfied by scalar arithmetic types, `Number`, and `STAmount`.
 *
 *  Used to gate `ValueProxy::operator+=` and `operator-=` so they are only
 *  available for field types that support arithmetic operations.
 */
template <typename U>
concept IsArithmeticNumber =
    std::is_arithmetic_v<U> || std::is_same_v<U, Number> || std::is_same_v<U, STAmount>;

/** Satisfied by phantom-typed `unit::ValueUnit<Unit, Value>` wrappers
 *  whose `Value` satisfies `IsArithmeticNumber`.
 */
template <
    typename U,
    typename Value = typename U::value_type,
    typename Unit = typename U::unit_type>
concept IsArithmeticValueUnit = std::is_same_v<U, unit::ValueUnit<Unit, Value>> &&
    IsArithmeticNumber<Value> && std::is_class_v<Unit>;

/** Satisfied by ST wrapper types (e.g., `STAmount`) that are not
 *  `ValueUnit` but whose `value_type` satisfies `IsArithmeticNumber`.
 */
template <typename U, typename Value = typename U::value_type>
concept IsArithmeticST = !IsArithmeticValueUnit<U> && IsArithmeticNumber<Value>;

/** Union of `IsArithmeticNumber`, `IsArithmeticST`, and `IsArithmeticValueUnit`. */
template <typename U>
concept IsArithmetic = IsArithmeticNumber<U> || IsArithmeticST<U> || IsArithmeticValueUnit<U>;

/** Satisfied when `T + U` compiles and the result is assignable back to `T`. */
template <class T, class U>
concept Addable = requires(T t, U u) { t = t + u; };

/** Satisfied when `T`'s `value_type` is arithmetic and supports addition with `U`. */
template <typename T, typename U>
concept IsArithmeticCompatible =
    IsArithmetic<typename T::value_type> && Addable<typename T::value_type, U>;

/** Mutable proxy for a non-optional (`soeREQUIRED` or `soeDEFAULT`) field.
 *
 *  Returned by the mutable overloads of `STObject::operator[]` and
 *  `STObject::at()`.  Supports assignment, arithmetic `+=`/`-=` for
 *  compatible value types, and implicit conversion to `value_type` for
 *  transparent read-through.
 *
 *  Copy-constructible but not copy-assignable; constructed only by `STObject`.
 *
 *  @tparam T  The concrete `STBase`-derived type carrying the field's value.
 */
template <class T>
class STObject::ValueProxy : public Proxy<T>
{
private:
    using value_type = typename T::value_type;

public:
    ValueProxy(ValueProxy const&) = default;
    ValueProxy&
    operator=(ValueProxy const&) = delete;

    /** Assign `u` to the field, delegating to `Proxy::assign()`. */
    template <class U>
    std::enable_if_t<std::is_assignable_v<T, U>, ValueProxy&>
    operator=(U&& u);

    /** Add `u` to the current field value and write back. */
    template <IsArithmetic U>
        requires IsArithmeticCompatible<T, U>
    ValueProxy&
    operator+=(U const& u);

    /** Subtract `u` from the current field value and write back. */
    template <IsArithmetic U>
        requires IsArithmeticCompatible<T, U>
    ValueProxy&
    operator-=(U const& u);

    /** Implicit conversion to `value_type` for transparent read-through. */
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

/** Mutable proxy for an optional (`soeOPTIONAL`) field.
 *
 *  Returned by the mutable overloads of `STObject::operator[]` and
 *  `STObject::at()` when called with an `OptionaledField<T>`.  Supports
 *  assignment from a value, `std::optional`, or `std::nullopt` (to remove
 *  the field).  Implicit conversion to `optional_type` enables use in
 *  standard optional contexts.
 *
 *  Assigning `std::nullopt` to a `soeREQUIRED` or `soeDEFAULT` field throws
 *  `FieldErr`; required fields cannot be removed and default-value fields are
 *  semantically always present.
 *
 *  Copy-constructible but not copy-assignable; constructed only by `STObject`.
 *
 *  @tparam T  The concrete `STBase`-derived type carrying the field's value.
 */
template <class T>
class STObject::OptionalProxy : public Proxy<T>
{
private:
    using value_type = typename T::value_type;

    using optional_type = std::optional<std::decay_t<value_type>>;

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

    /** Implicit conversion to `std::optional<value_type>`. */
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

    /** Return the field's value if present, otherwise `val`. */
    [[nodiscard]] value_type
    valueOr(value_type val) const;

    /** Remove the field (make it absent).
     *
     *  @throws FieldErr if the field is `soeREQUIRED` or `soeDEFAULT`.
     */
    OptionalProxy&
    operator=(std::nullopt_t const&);

    /** Assign from an rvalue `std::optional`; removes the field if `nullopt`. */
    OptionalProxy&
    operator=(optional_type&& v);  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)

    /** Assign from a const `std::optional`; removes the field if `nullopt`. */
    OptionalProxy&
    operator=(optional_type const& v);

    /** Assign a value directly to the field. */
    template <class U>
    std::enable_if_t<std::is_assignable_v<T, U>, OptionalProxy&>
    operator=(U&& u);

private:
    friend class STObject;

    OptionalProxy(STObject* st, TypedField<T> const* f);

    /** Return `true` when the field is present (not the `STI_NOTPRESENT` sentinel). */
    [[nodiscard]] bool
    engaged() const noexcept;

    /** Remove the field, enforcing `soeREQUIRED`/`soeDEFAULT` constraints. */
    void
    disengage();

    /** Return the current value as `optional_type`, or `nullopt` if absent. */
    [[nodiscard]] optional_type
    optionalValue() const;
};

class STObject::FieldErr : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

template <class T>
STObject::Proxy<T>::Proxy(STObject* st, TypedField<T> const* f) : st_(st), f_(f)
{
    if (st_->type_ != nullptr)
    {
        // STObject has associated template
        if (!st_->peekAtPField(*f_))
            Throw<STObject::FieldErr>("Template field error '" + this->f_->getName() + "'");
        style_ = st_->type_->style(*f_);
    }
    else
    {
        style_ = SoeInvalid;
    }
}

template <class T>
auto
STObject::Proxy<T>::value() const -> value_type
{
    auto const t = find();
    if (t)
        return t->value();
    if (style_ == SoeInvalid)
    {
        Throw<STObject::FieldErr>("Value requested from invalid STObject.");
    }
    if (style_ != SoeDefault)
    {
        Throw<STObject::FieldErr>("Missing field '" + this->f_->getName() + "'");
    }
    return value_type{};
}

template <class T>
auto
STObject::Proxy<T>::operator*() const -> value_type
{
    return this->value();
}

/// Do not use operator->() unless the field is required, or you've checked that
/// it's set.
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
    if (style_ == SoeDefault && u == value_type{})
    {
        st_->makeFieldAbsent(*f_);
        return;
    }
    T* t = nullptr;
    if (style_ == SoeInvalid)
    {
        t = dynamic_cast<T*>(st_->getPField(*f_, true));
    }
    else
    {
        t = dynamic_cast<T*>(st_->makeFieldPresent(*f_));
    }
    XRPL_ASSERT(t, "xrpl::STObject::Proxy::assign : type cast succeeded");
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
    requires IsArithmeticCompatible<T, U>
STObject::ValueProxy<T>&
STObject::ValueProxy<T>::operator+=(U const& u)
{
    this->assign(this->value() + u);
    return *this;
}

template <class T>
template <IsArithmetic U>
    requires IsArithmeticCompatible<T, U>
STObject::ValueProxy<T>&
STObject::ValueProxy<T>::operator-=(U const& u)
{
    this->assign(this->value() - u);
    return *this;
}

template <class T>
STObject::ValueProxy<T>::
operator value_type() const
{
    return this->value();
}

template <class T>
STObject::ValueProxy<T>::ValueProxy(STObject* st, TypedField<T> const* f) : Proxy<T>(st, f)
{
}

//------------------------------------------------------------------------------

template <class T>
STObject::OptionalProxy<T>::
operator bool() const noexcept
{
    return engaged();
}

template <class T>
STObject::OptionalProxy<T>::
operator typename STObject::OptionalProxy<T>::optional_type() const
{
    return optionalValue();
}

template <class T>
typename STObject::OptionalProxy<T>::optional_type
STObject::OptionalProxy<T>::operator~() const
{
    return optionalValue();
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
STObject::OptionalProxy<T>::operator=(
    optional_type&& v)  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
    -> OptionalProxy&
{
    if (v)
    {
        this->assign(std::move(*v));
    }
    else
    {
        disengage();
    }
    return *this;
}

template <class T>
auto
STObject::OptionalProxy<T>::operator=(optional_type const& v) -> OptionalProxy&
{
    if (v)
    {
        this->assign(*v);
    }
    else
    {
        disengage();
    }
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
STObject::OptionalProxy<T>::OptionalProxy(STObject* st, TypedField<T> const* f) : Proxy<T>(st, f)
{
}

template <class T>
bool
STObject::OptionalProxy<T>::engaged() const noexcept
{
    return this->style_ == SoeDefault || this->find() != nullptr;
}

template <class T>
void
STObject::OptionalProxy<T>::disengage()
{
    if (this->style_ == SoeRequired || this->style_ == SoeDefault)
        Throw<STObject::FieldErr>("Template field error '" + this->f_->getName() + "'");
    if (this->style_ == SoeInvalid)
    {
        this->st_->delField(*this->f_);
    }
    else
    {
        this->st_->makeFieldAbsent(*this->f_);
    }
}

template <class T>
auto
STObject::OptionalProxy<T>::optionalValue() const -> optional_type
{
    if (!engaged())
        return std::nullopt;
    return this->value();
}

template <class T>
typename STObject::OptionalProxy<T>::value_type
STObject::OptionalProxy<T>::valueOr(value_type val) const
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

// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
inline STObject::STObject(SerialIter&& sit, SField const& name) : STObject(sit, name)
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
    return type_ == nullptr;
}

inline void
STObject::addWithoutSigningFields(Serializer& s) const
{
    add(s, WhichFields::OmitSigningFields);
}

// VFALCO NOTE does this return an expensive copy of an object with a
//             dynamic buffer?
// VFALCO TODO Remove this function and fix the few callers.
inline Serializer
STObject::getSerializer() const
{
    Serializer s;
    add(s, WhichFields::WithAllFields);
    return s;
}

template <class... Args>
inline std::size_t
STObject::emplaceBack(Args&&... args)
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
[[nodiscard]] typename T::value_type
STObject::at(TypedField<T> const& f) const
{
    auto const b = peekAtPField(f);
    if (!b)
    {
        // This is a free object (no constraints)
        // with no template
        Throw<STObject::FieldErr>("Missing field: " + f.getName());
    }

    if (auto const u = dynamic_cast<T const*>(b))
        return u->value();

    XRPL_ASSERT(type_, "xrpl::STObject::at(TypedField auto) : field template non-null");
    XRPL_ASSERT(
        b->getSType() == STI_NOTPRESENT, "xrpl::STObject::at(TypedField auto) : type not present");

    if (type_->style(f) == SoeOptional)
        Throw<STObject::FieldErr>("Missing optional field: " + f.getName());

    XRPL_ASSERT(
        type_->style(f) == SoeDefault,
        "xrpl::STObject::at(TypedField auto) : template style is default");

    // Used to help handle the case where value_type is a const reference,
    // otherwise we would return the address of a temporary.
    static std::decay_t<typename T::value_type> const kDV{};
    return kDV;
}

template <class T>
[[nodiscard]] std::optional<std::decay_t<typename T::value_type>>
STObject::at(OptionaledField<T> const& of) const
{
    auto const b = peekAtPField(*of.f);
    if (!b)
        return std::nullopt;
    auto const u = dynamic_cast<T const*>(b);
    if (!u)
    {
        XRPL_ASSERT(
            type_,
            "xrpl::STObject::at(OptionaledField auto) : field template "
            "non-null");
        XRPL_ASSERT(
            b->getSType() == STI_NOTPRESENT,
            "xrpl::STObject::at(OptionaledField auto) : type not present");
        if (type_->style(*of.f) == SoeOptional)
            return std::nullopt;
        XRPL_ASSERT(
            type_->style(*of.f) == SoeDefault,
            "xrpl::STObject::at(OptionaledField auto) : template style is "
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
STObject::setFieldH160(SField const& field, BaseUInt<160, Tag> const& v)
{
    STBase* rf = getPField(field, true);

    if (!rf)
        throwFieldNotFound(field);

    if (rf->getSType() == STI_NOTPRESENT)
        rf = makeFieldPresent(field);

    using Bits = STBitString<160>;
    if (auto cf = dynamic_cast<Bits*>(rf))
    {
        cf->setValue(v);
    }
    else
    {
        Throw<std::runtime_error>("Wrong field type");
    }
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

    SerializedTypeID const id = rf->getSType();

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

    SerializedTypeID const id = rf->getSType();

    if (id == STI_NOTPRESENT)
    {
        // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
        return empty;  // optional field not present
    }

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
    static_assert(!std::is_lvalue_reference_v<V>, "");

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

}  // namespace xrpl
