/** @file
 *  Schema definitions for XRPL serialized objects.
 *
 *  Provides `SOElement` (a single field's schema entry) and `SOTemplate` (the
 *  complete ordered schema for one transaction, ledger entry, or inner object
 *  type).  Templates are constructed once at startup by the `KnownFormats`
 *  singletons and are thereafter read-only, enabling lock-free O(1) field
 *  lookup during every serialization and deserialization call.
 */

#pragma once

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/SField.h>

#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <vector>

namespace xrpl {

/** Field-presence semantics for a single entry in an `SOTemplate`.
 *
 *  Controls how `STObject` treats a field during deserialization, validation,
 *  and serialization:
 *
 *  - `SoeRequired`  — the field must be present; absence is a fatal error.
 *  - `SoeOptional`  — the field may be absent; if present it may carry the
 *    type's default value (presence with default has distinct protocol meaning).
 *  - `SoeDefault`   — the field may be absent; if present it must NOT carry the
 *    type's default value.  Inner objects that contain `SoeDefault` fields must
 *    be created via `STObject::makeInnerObject()` to preserve this invariant.
 *  - `SoeInvalid`   — sentinel returned by `STObject::getFieldStyle()` when the
 *    object has no associated template; never used in a live schema.
 *
 *  @note `SoeOptional` and `SoeDefault` are subtly different: for some fields
 *      (e.g., `QualityIn` on a trust line) having the field present with its
 *      default value and having it absent carry different protocol semantics.
 *      Use `SoeDefault` when the field must not encode redundant default state.
 */
// 2026 usages, 129 files
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum SOEStyle {
    SoeInvalid = -1,
    SoeRequired = 0,  ///< Field must be present.
    SoeOptional = 1,  ///< Field may be absent; if present, may hold default value.
    SoeDefault = 2,   ///< Field may be absent; if present, must not hold default value.
};

/** Multi-Purpose Token (MPT) awareness annotation for amount and issue fields.
 *
 *  Applied only to `STAmount` and `STIssue` typed fields (enforced by the
 *  constrained `SOElement` constructor).  Allows the validation layer in
 *  `STObject` and `STTx` to check MPT compatibility at the schema level rather
 *  than in scattered per-transaction code.
 *
 *  - `SoeMptNone`         — field does not carry an amount or issue; MPT check
 *    is never performed.  Default for all non-amount fields.
 *  - `SoeMptSupported`    — the transaction format allows MPT in this field.
 *  - `SoeMptNotSupported` — the transaction format explicitly forbids MPT in
 *    this field; validation rejects any MPT value.
 *
 *  @note Bare enumerator names (without a class scope) are required because
 *      these values are parsed by the Python DSL that processes
 *      `transactions.macro`.
 */
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum SOETxMPTIssue { SoeMptNone, SoeMptSupported, SoeMptNotSupported };

//------------------------------------------------------------------------------

/** One field's schema entry inside an `SOTemplate`.
 *
 *  Pairs an `SField` reference with its `SOEStyle` presence semantics and,
 *  for amount/issue fields, an `SOETxMPTIssue` MPT-awareness tag.
 *
 *  `SField` instances are immovable, non-copyable process-lifetime singletons.
 *  Storing a `std::reference_wrapper` rather than a raw pointer communicates
 *  the non-owning relationship clearly and allows `SOElement` to be held in a
 *  `std::vector` (which requires copyable/movable elements).
 *
 *  @note Both constructors call the private `init()` helper, which throws if
 *      the field is not "useful" (i.e., `fieldCode <= 0`, as for `sfInvalid`
 *      or `sfGeneric`).  This catches schema bugs at application startup.
 */
class SOElement
{
    // Use std::reference_wrapper so SOElement can be stored in a std::vector.
    std::reference_wrapper<SField const> sField_;
    SOEStyle style_;
    SOETxMPTIssue supportMpt_ = SoeMptNone;

private:
    /** Validate that the wrapped field is a known, named, serializable field.
     *
     *  @param fieldName The field to validate.
     *  @throws std::runtime_error if `fieldName.isUseful()` returns false
     *      (i.e., `fieldCode <= 0`), indicating a sentinel or placeholder field.
     */
    void
    init(SField const& fieldName) const
    {
        if (!sField_.get().isUseful())
        {
            auto nm = std::to_string(fieldName.getCode());
            if (fieldName.hasName())
                nm += ": '" + fieldName.getName() + "'";
            Throw<std::runtime_error>("SField (" + nm + ") in SOElement must be useful.");
        }
    }

public:
    /** Construct a schema entry for any serializable field.
     *
     *  @param fieldName The field this entry describes; must satisfy
     *      `isUseful()` (positive field code).
     *  @param style Presence semantics: required, optional, or default.
     *  @throws std::runtime_error if @p fieldName is not a useful field.
     */
    SOElement(SField const& fieldName, SOEStyle style) : sField_(fieldName), style_(style)
    {
        init(fieldName);
    }

    /** Construct a schema entry for an `STAmount` or `STIssue` field with MPT annotation.
     *
     *  The `requires` constraint restricts this overload to `STAmount` and
     *  `STIssue` typed fields, enforcing that MPT support annotations can only
     *  appear on fields that actually carry amounts or asset specifiers.
     *
     *  @tparam T Must be `STAmount` or `STIssue`.
     *  @param fieldName The typed amount or issue field this entry describes.
     *  @param style Presence semantics: required, optional, or default.
     *  @param supportMpt Whether this field accepts MPT values.  Defaults to
     *      `SoeMptNotSupported` so new amount fields must explicitly opt in.
     *  @throws std::runtime_error if @p fieldName is not a useful field.
     */
    template <typename T>
        requires(std::is_same_v<T, STAmount> || std::is_same_v<T, STIssue>)
    SOElement(
        TypedField<T> const& fieldName,
        SOEStyle style,
        SOETxMPTIssue supportMpt = SoeMptNotSupported)
        : sField_(fieldName), style_(style), supportMpt_(supportMpt)
    {
        init(fieldName);
    }

    /** Return the `SField` this entry describes. */
    [[nodiscard]] SField const&
    sField() const
    {
        return sField_.get();
    }

    /** Return the field's presence semantics within its containing object type. */
    [[nodiscard]] SOEStyle
    style() const
    {
        return style_;
    }

    /** Return the MPT-awareness annotation for this amount or issue field.
     *
     *  @note Returns `SoeMptNone` for all non-amount, non-issue fields; callers
     *      should only interpret the result when the field type is `STAmount`
     *      or `STIssue`.
     */
    [[nodiscard]] SOETxMPTIssue
    supportMPT() const
    {
        return supportMpt_;
    }
};

//------------------------------------------------------------------------------

/** Immutable field schema for one serialized object type in the XRP Ledger.
 *
 *  Holds the ordered list of `SOElement` entries for a single transaction,
 *  ledger entry, or inner object type, together with a dense reverse-lookup
 *  table that maps `SField::getNum()` to the element's position in O(1).
 *
 *  Templates are constructed once at process startup by `KnownFormats`
 *  subclasses (`TxFormats`, `LedgerFormats`, `InnerObjectFormats`) and are
 *  thereafter immutable.  All consumers hold a `const*` or `const&`; no
 *  copying is ever required.  Consequently the copy constructor and copy
 *  assignment operator are deleted — the type is move-only.
 *
 *  @note The constructor snapshots `SField::getNumFields()` to size the index
 *      table.  Fields registered after the template is constructed cannot be
 *      looked up and will cause `getIndex()` to throw.  In practice this is
 *      never an issue because all `SField` singletons are registered before
 *      `main()` runs, ahead of the `KnownFormats` singletons.
 *
 *  @see SOElement, SOEStyle, STObject::applyTemplate(), STObject::set()
 */
class SOTemplate
{
public:
    SOTemplate(SOTemplate const&) = delete;
    SOTemplate&
    operator=(SOTemplate const&) = delete;

    // Copying vectors is expensive.  Make this a move-only type until
    // there is motivation to change that.
    SOTemplate(SOTemplate&& other) = default;
    SOTemplate&
    operator=(SOTemplate&& other) = default;

    /** Build the schema from a type-specific and a shared field list.
     *
     *  Concatenates @p uniqueFields followed by @p commonFields into a single
     *  ordered element sequence, then constructs the O(1) index table.
     *
     *  @param uniqueFields Fields specific to this object type; placed first in
     *      the element sequence.
     *  @param commonFields Fields shared across all object types of this kind
     *      (e.g., `Fee`, `Sequence`, `SigningPubKey` for transactions); appended
     *      after unique fields.
     *  @throws std::runtime_error if any field has an out-of-range field number
     *      or appears more than once across both lists.
     */
    SOTemplate(std::vector<SOElement> uniqueFields, std::vector<SOElement> commonFields = {});

    /** Convenience overload accepting initializer lists; delegates to the vector constructor.
     *
     *  @param uniqueFields Fields specific to this object type.
     *  @param commonFields Fields shared across all object types of this kind.
     *  @throws std::runtime_error forwarded from the vector constructor.
     */
    SOTemplate(
        std::initializer_list<SOElement> uniqueFields,
        std::initializer_list<SOElement> commonFields = {});

    /** Return an iterator to the first `SOElement` in the schema. */
    [[nodiscard]] std::vector<SOElement>::const_iterator
    begin() const
    {
        return elements_.cbegin();
    }

    /** Return an iterator to the first `SOElement` in the schema. */
    [[nodiscard]] std::vector<SOElement>::const_iterator
    cbegin() const
    {
        return begin();
    }

    /** Return a past-the-end iterator for the element sequence. */
    [[nodiscard]] std::vector<SOElement>::const_iterator
    end() const
    {
        return elements_.cend();
    }

    /** Return a past-the-end iterator for the element sequence. */
    [[nodiscard]] std::vector<SOElement>::const_iterator
    cend() const
    {
        return end();
    }

    /** Return the number of field entries in this schema. */
    [[nodiscard]] std::size_t
    size() const
    {
        return elements_.size();
    }

    /** Return the position of @p sField in the element sequence, or -1 if absent.
     *
     *  Uses a direct array subscript into the internal index table for O(1)
     *  cost.  This is the hot path called on every field access during
     *  serialization and deserialization.
     *
     *  @param sField The field to look up.
     *  @return Index into the element sequence, or -1 if the field is not part
     *      of this schema.
     *  @throws std::runtime_error if @p sField has a non-positive or
     *      out-of-range field number (i.e., a sentinel field or one registered
     *      after this template was constructed).
     */
    [[nodiscard]] int
    getIndex(SField const&) const;

    /** Return the presence-style of @p sf within this schema.
     *
     *  @param sf The field whose style to retrieve; must be present in this
     *      template (i.e., `getIndex(sf) != -1`).
     *  @return The `SOEStyle` declared for this field in the schema.
     *  @note Calling this with a field that is not in the template results in
     *      undefined behavior (out-of-bounds array access via the `-1` sentinel
     *      returned by `getIndex()`).  Use `getIndex()` to check presence first
     *      when the field may be absent.
     */
    [[nodiscard]] SOEStyle
    style(SField const& sf) const
    {
        return elements_[indices_[sf.getNum()]].style();
    }

private:
    std::vector<SOElement> elements_;
    std::vector<int> indices_;  ///< Dense lookup table: field num -> index into elements_.
};

}  // namespace xrpl
