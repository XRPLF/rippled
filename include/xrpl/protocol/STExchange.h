/** @file
 *  Type-safe bridge between serialized (`STBase`-derived) types and native C++
 *  types for reading and writing `STObject` fields.
 *
 *  Application code works with plain C++ types (integers, `Slice`, `Buffer`),
 *  while the wire protocol stores everything in serialized form (`STInteger<U>`,
 *  `STBlob`, etc.). The `STExchange` traits struct centralizes the conversion
 *  mappings so callers never need to perform manual `dynamic_cast` or construct
 *  heap-allocated serialized objects directly. The free functions `get`, `set`,
 *  and `erase` are the primary interface for field access on `STObject`.
 */

#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/contract.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STObject.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace xrpl {

/** Traits adapter that maps a serialized type @p U to a native C++ type @p T.
 *
 *  Each specialization provides:
 *  - `value_type` — the canonical C++ representation for the serialized type.
 *  - `get(optional<T>&, U const&)` — extracts a native value from the
 *      serialized object.
 *  - `set(field, T const&)` — constructs a heap-allocated serialized object
 *      ready for insertion into an `STObject`.
 *
 *  All conversions are resolved at compile time; there is no runtime
 *  polymorphism in the type mapping itself. Adding support for a new C++ view
 *  of an existing wire type requires only a new specialization here — the
 *  serialization infrastructure is not touched.
 *
 *  @tparam U The serialized type (e.g. `STInteger<uint32_t>`, `STBlob`).
 *  @tparam T The desired native C++ type (e.g. `uint32_t`, `Slice`, `Buffer`).
 */
template <class U, class T>
struct STExchange;

/** `STExchange` specialization covering the full family of integer types.
 *
 *  A single partial specialization handles `STUInt8`, `STUInt16`, `STUInt32`,
 *  `STUInt64`, and `STInt32` uniformly. `get` extracts the integer via
 *  `STInteger::value()` and `set` constructs a new `STInteger<U>` on the heap.
 *
 *  @tparam U The underlying integer type (e.g. `uint32_t`, `uint64_t`).
 *  @tparam T The desired C++ integer type to convert to/from.
 */
template <class U, class T>
struct STExchange<STInteger<U>, T>
{
    explicit STExchange() = default;

    /** The canonical C++ integer type for this serialized field. */
    using value_type = U;

    /** Populate @p t with the integer value stored in @p u.
     *
     *  @param t Output optional to receive the extracted value.
     *  @param u The serialized integer object to read from.
     */
    static void
    get(std::optional<T>& t, STInteger<U> const& u)
    {
        t = u.value();
    }

    /** Construct a heap-allocated `STInteger<U>` initialized to @p t.
     *
     *  @param f The field descriptor for the new serialized object.
     *  @param t The integer value to store.
     *  @return Owning pointer to the newly constructed serialized integer.
     */
    static std::unique_ptr<STInteger<U>>
    set(SField const& f, T const& t)
    {
        return std::make_unique<STInteger<U>>(f, t);
    }
};

/** `STExchange` specialization for reading an `STBlob` field as a `Slice`.
 *
 *  `Slice` is a non-owning view, so both `get` and `set` always copy the
 *  underlying bytes — `get` via `emplace(data, size)` and `set` via the
 *  `STBlob(field, data, size)` constructor.
 */
template <>
struct STExchange<STBlob, Slice>
{
    explicit STExchange() = default;

    /** Non-owning byte view. */
    using value_type = Slice;

    /** Populate @p t with a `Slice` pointing into a copy of @p u's bytes.
     *
     *  @param t Output optional to receive the `Slice`.
     *  @param u The serialized blob object to read from.
     */
    static void
    get(std::optional<value_type>& t, STBlob const& u)
    {
        t.emplace(u.data(), u.size());
    }

    /** Construct a heap-allocated `STBlob` by copying the bytes of @p t.
     *
     *  @param f The field descriptor for the new serialized object.
     *  @param t The source byte view to copy into the blob.
     *  @return Owning pointer to the newly constructed `STBlob`.
     */
    static std::unique_ptr<STBlob>
    set(TypedField<STBlob> const& f, Slice const& t)
    {
        return std::make_unique<STBlob>(f, t.data(), t.size());
    }
};

/** `STExchange` specialization for reading an `STBlob` field as a `Buffer`.
 *
 *  `Buffer` owns its memory. The lvalue `set` overload copies bytes into the
 *  new `STBlob`; the rvalue `set` overload moves the `Buffer` directly into
 *  the `STBlob`, avoiding an extra heap allocation on hot paths that build
 *  transaction objects.
 */
template <>
struct STExchange<STBlob, Buffer>
{
    explicit STExchange() = default;

    /** Owning byte container. */
    using value_type = Buffer;

    /** Populate @p t with a `Buffer` containing a copy of @p u's bytes.
     *
     *  @param t Output optional to receive the `Buffer`.
     *  @param u The serialized blob object to read from.
     */
    static void
    get(std::optional<Buffer>& t, STBlob const& u)
    {
        t.emplace(u.data(), u.size());
    }

    /** Construct a heap-allocated `STBlob` by copying the bytes of @p t.
     *
     *  @param f The field descriptor for the new serialized object.
     *  @param t The source buffer to copy into the blob.
     *  @return Owning pointer to the newly constructed `STBlob`.
     */
    static std::unique_ptr<STBlob>
    set(TypedField<STBlob> const& f, Buffer const& t)
    {
        return std::make_unique<STBlob>(f, t.data(), t.size());
    }

    /** Construct a heap-allocated `STBlob` by moving @p t's storage.
     *
     *  Preferred over the lvalue overload when the caller no longer needs
     *  the `Buffer`, as it avoids an extra heap allocation.
     *
     *  @param f The field descriptor for the new serialized object.
     *  @param t The source buffer to move into the blob.
     *  @return Owning pointer to the newly constructed `STBlob`.
     */
    static std::unique_ptr<STBlob>
    set(TypedField<STBlob> const& f, Buffer&& t)
    {
        return std::make_unique<STBlob>(f, std::move(t));
    }
};

//------------------------------------------------------------------------------

/** Read a field from an `STObject` as native C++ type @p T.
 *
 *  Uses `STObject::peekAtPField` (non-mutating — does not insert a default for
 *  absent fields) and checks two distinct absence conditions: a null pointer
 *  (the field was never registered in the object's schema) and
 *  `STI_NOTPRESENT` (the field exists in the schema but has been explicitly
 *  marked absent). A `dynamic_cast` failure on the non-null, present field
 *  indicates a programming error and throws rather than returning empty.
 *
 *  @tparam T The desired native C++ type to extract (e.g. `Slice` or `Buffer`
 *      for an `STBlob` field).
 *  @tparam U The serialized field type, inferred from @p f.
 *  @param st The object to read from.
 *  @param f  The typed field descriptor identifying the field.
 *  @return The field value, or `std::nullopt` if the field is absent.
 *  @throws std::runtime_error If the field is present but its dynamic type
 *      does not match @p U — this indicates a programming error.
 *  @see get(STObject const&, TypedField<U> const&) for the type-inferring
 *      overload that avoids spelling out @p T explicitly.
 */
/** @{ */
template <class T, class U>
std::optional<T>
get(STObject const& st, TypedField<U> const& f)
{
    std::optional<T> t;
    STBase const* const b = st.peekAtPField(f);
    if (!b)
        return t;
    auto const id = b->getSType();
    if (id == STI_NOTPRESENT)
        return t;
    auto const u = dynamic_cast<U const*>(b);
    // This should never happen
    if (!u)
        Throw<std::runtime_error>("Wrong field type");
    STExchange<U, T>::get(t, *u);
    return t;
}

/** Read a field from an `STObject`, inferring the native type from the field
 *  descriptor's `value_type`.
 *
 *  This is the ergonomic default: callers write `get(st, sfSequence)` rather
 *  than `get<uint32_t>(st, sfSequence)`. Use the explicit-`T` overload when a
 *  different C++ view of the same wire type is needed (e.g. reading an `STBlob`
 *  as `Slice` for temporary inspection vs. `Buffer` for ownership).
 *
 *  @tparam U The serialized field type, inferred from @p f.
 *  @param st The object to read from.
 *  @param f  The typed field descriptor identifying the field.
 *  @return The field value as `U::value_type`, or `std::nullopt` if absent.
 */
template <class U>
std::optional<typename STExchange<U, typename U::value_type>::value_type>
get(STObject const& st, TypedField<U> const& f)
{
    return get<typename U::value_type>(st, f);
}
/** @} */

/** Write a value into a field of an `STObject`.
 *
 *  Uses `std::decay` to strip cv-qualifiers and references before selecting the
 *  `STExchange` specialization, and `std::forward` to preserve value category
 *  so the move-semantic `Buffer&&` overload fires when an rvalue is passed.
 *
 *  @tparam U The serialized field type, inferred from @p f.
 *  @tparam T The native C++ value type, inferred from @p t. May be an rvalue
 *      reference to trigger move-optimized specializations (e.g.
 *      `STExchange<STBlob, Buffer>::set(f, Buffer&&)`).
 *  @param st The object to write into.
 *  @param f  The typed field descriptor identifying the field.
 *  @param t  The value to store; forwarded to the appropriate `STExchange`
 *      specialization.
 */
template <class U, class T>
void
set(STObject& st, TypedField<U> const& f, T&& t)
{
    st.set(STExchange<U, std::decay_t<T>>::set(f, std::forward<T>(t)));
}

/** Write a blob field whose contents are populated by a callable.
 *
 *  Constructs an `STBlob` of @p size bytes and invokes @p init to fill it
 *  in-place, avoiding an intermediate copy for large blobs.
 *
 *  @tparam Init A callable with signature compatible with the `STBlob`
 *      in-place initialization constructor.
 *  @param st   The object to write into.
 *  @param f    The field descriptor for the blob field.
 *  @param size The desired byte length of the blob.
 *  @param init Callable invoked to populate the blob's storage.
 */
template <class Init>
void
set(STObject& st, TypedField<STBlob> const& f, std::size_t size, Init&& init)
{
    st.set(std::make_unique<STBlob>(f, size, init));
}

/** Write a blob field from a raw pointer and length.
 *
 *  Convenience overload for C-style interop. Copies @p size bytes from
 *  @p data into a newly constructed `STBlob`.
 *
 *  @param st   The object to write into.
 *  @param f    The field descriptor for the blob field.
 *  @param data Pointer to the source bytes.
 *  @param size Number of bytes to copy.
 */
template <class = void>
void
set(STObject& st, TypedField<STBlob> const& f, void const* data, std::size_t size)
{
    st.set(std::make_unique<STBlob>(f, data, size));
}

/** Mark a field as absent in an `STObject` without removing it from the schema.
 *
 *  Delegates to `STObject::makeFieldAbsent`, which sets the field's type to
 *  `STI_NOTPRESENT`. The field slot is retained in the object's declared
 *  schema but contributes nothing to the wire encoding or canonical
 *  serialization.
 *
 *  @tparam U The serialized field type, inferred from @p f.
 *  @param st The object to modify.
 *  @param f  The typed field descriptor identifying the field to erase.
 */
template <class U>
void
erase(STObject& st, TypedField<U> const& f)
{
    st.makeFieldAbsent(f);
}

}  // namespace xrpl
