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

/** Bitmask controlling how an ST type renders to JSON.
 *
 *  Combines named flag bits defined in `Values`. Supports `|`, `&`, and `~`
 *  for composing and masking option sets. The complement operator `~` is
 *  bounded by `Values::All` so it never sets reserved future bits.
 *
 *  @note Treat instances as flag sets — bitwise operators are the intended
 *      interface; do not compare or store the raw `value` field directly.
 */
struct JsonOptions
{
    using underlying_t = unsigned int;
    underlying_t value;

    /** Named option bits for JSON rendering. */
    enum class Values : underlying_t {
        None = 0b0000'0000,
        IncludeDate = 0b0000'0001,       /**< Include a date field in the output. */
        DisableApiPriorV2 = 0b0000'0010, /**< Suppress legacy pre-API-v2 formatting. */

        // IMPORTANT `All` must be union of all of the above; see also operator~
        All = IncludeDate | DisableApiPriorV2  // 0b0000'0011
    };

    /** Construct from a raw bitmask value. */
    constexpr JsonOptions(underlying_t v) noexcept : value(v)
    {
    }

    /** Construct from a named `Values` enumerator. */
    constexpr JsonOptions(Values v) noexcept : value(static_cast<JsonOptions::underlying_t>(v))
    {
    }

    /** Convert to the underlying unsigned integer. */
    [[nodiscard]] constexpr explicit
    operator underlying_t() const noexcept
    {
        return value;
    }

    /** Return `true` if any option bit is set. */
    [[nodiscard]] constexpr explicit
    operator bool() const noexcept
    {
        return value != 0u;
    }
    [[nodiscard]] constexpr auto friend
    operator==(JsonOptions lh, JsonOptions rh) noexcept -> bool = default;
    [[nodiscard]] constexpr auto friend
    operator!=(JsonOptions lh, JsonOptions rh) noexcept -> bool = default;

    /** Return the union (bitwise OR) of two option sets. */
    [[nodiscard]] constexpr JsonOptions friend
    operator|(JsonOptions lh, JsonOptions rh) noexcept
    {
        return {lh.value | rh.value};
    }

    /** Return the intersection (bitwise AND) of two option sets. */
    [[nodiscard]] constexpr JsonOptions friend
    operator&(JsonOptions lh, JsonOptions rh) noexcept
    {
        return {lh.value & rh.value};
    }

    /** Return the complement, bounded to the known `Values::All` mask.
     *
     *  Use with `&` for set-difference, e.g.
     *  `options & ~JsonOptions(JsonOptions::Values::IncludeDate)`.
     *  Bits beyond `Values::All` are never set in the result.
     */
    [[nodiscard]] constexpr JsonOptions friend
    operator~(JsonOptions v) noexcept
    {
        return {~v.value & static_cast<underlying_t>(Values::All)};
    }
};

/** ADL-accessible JSON conversion for any type that exposes `getJson(JsonOptions)`.
 *
 *  Calls `t.getJson(JsonOptions::Values::None)`. Provides a uniform,
 *  options-free entry point for generic code that needs to render an ST value
 *  without caring about per-call rendering flags.
 *
 *  @tparam T A type whose `getJson` method returns a value convertible to
 *      `json::Value`.
 *  @param t The object to convert.
 *  @return A `json::Value` representation of @p t.
 */
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

/** Abstract base class for every serialized field type in the XRPL protocol.
 *
 *  "ST" stands for "Serialized Type." Every field that appears in a
 *  transaction, ledger entry, or validation — integers, amounts, account IDs,
 *  blobs, arrays, nested objects — is represented as a class derived from
 *  `STBase`. Each instance carries a field identity (an `SField` pointer) that
 *  binds a human-readable name and a numeric type+field code used in the binary
 *  wire format.
 *
 *  The virtual interface (`getSType`, `add`, `isEquivalent`, `isDefault`,
 *  `getJson`, `getText`, `getFullText`) must be overridden by every concrete
 *  subclass.
 *
 *  @note `operator=` deliberately does **not** copy the field name when the
 *      destination already holds a meaningful name (see implementation). This
 *      design supports element slide-down in `STObject`/`STArray` without
 *      corrupting field identities. As a consequence, do **not** store
 *      `STBase`-derived objects directly in `std::vector` or similar owning
 *      containers — use `boost::ptr_*` containers or `detail::STVar` instead.
 */
class STBase
{
    SField const* fName_;

public:
    virtual ~STBase() = default;

    /** Construct with the generic (placeholder) field name. */
    STBase();

    STBase(STBase const&) = default;

    /** Copy-assign the value; conditionally copies the field name.
     *
     *  The field name (`fName_`) is updated from @p t only when the current
     *  name is not useful (e.g., `sfGeneric`). This allows slot initialisation
     *  to pick up the source's protocol identity while preventing element
     *  slide-down operations inside `STObject` from overwriting already-valid
     *  field names.
     *
     *  @param t The source object whose value (and optionally name) to copy.
     *  @return `*this`
     */
    STBase&
    operator=(STBase const& t);

    /** Construct with a specific field identity.
     *
     *  @param n The `SField` descriptor that identifies this field on the wire.
     */
    explicit STBase(SField const& n);

    /** Value equality: same concrete type and `isEquivalent` holds.
     *
     *  Field names are ignored; only values are compared.
     */
    bool
    operator==(STBase const& t) const;

    /** Value inequality: opposite of `operator==`. */
    bool
    operator!=(STBase const& t) const;

    /** Narrow the static type to `D`, throwing on failure.
     *
     *  Performs `dynamic_cast<D*>(this)`. Prefer this over a raw
     *  `dynamic_cast` at call sites — it guarantees that a failed cast throws
     *  `std::bad_cast` rather than yielding a null pointer that may be
     *  silently dereferenced.
     *
     *  @tparam D The target derived type.
     *  @return A reference to `*this` as `D`.
     *  @throws std::bad_cast if `*this` is not an instance of `D`.
     */
    template <class D>
    D&
    downcast();

    /** Const overload of `downcast()`.
     *
     *  @tparam D The target derived type.
     *  @return A const reference to `*this` as `D`.
     *  @throws std::bad_cast if `*this` is not an instance of `D`.
     */
    template <class D>
    D const&
    downcast() const;

    /** Return the `SerializedTypeID` enum value for this concrete type.
     *
     *  The base implementation returns `STI_NOTPRESENT`. Every concrete
     *  subclass overrides this to return its own type tag.
     */
    [[nodiscard]] virtual SerializedTypeID
    getSType() const;

    /** Return a human-readable string that includes the field name.
     *
     *  Typically formatted as `"<fieldName> = <value>"`. Returns an empty
     *  string when `getSType() == STI_NOTPRESENT`.
     */
    [[nodiscard]] virtual std::string
    getFullText() const;

    /** Return a human-readable string representation of the value only.
     *
     *  Unlike `getFullText()`, the field name is not included. The base
     *  implementation returns an empty string.
     */
    [[nodiscard]] virtual std::string
    getText() const;

    /** Render to a JSON value, respecting the given rendering options.
     *
     *  @param options Bitmask controlling date inclusion and API version
     *      formatting. Defaults to `JsonOptions::Values::None`.
     *  @return A `json::Value` representation.
     */
    [[nodiscard]] virtual json::Value getJson(JsonOptions = JsonOptions::Values::None) const;

    /** Serialize the field's binary payload into @p s.
     *
     *  Writes only the value bytes; the field-ID header must be written
     *  separately via `addFieldID()`. The base implementation is an
     *  unreachable stub — every concrete subclass must override this.
     *
     *  @param s The `Serializer` accumulator to write into.
     */
    virtual void
    add(Serializer& s) const;

    /** Value equivalence check, ignoring field names.
     *
     *  Used by `operator==` and by `detail::STVar::operator==`. The base
     *  implementation asserts that this instance has type `STI_NOTPRESENT`
     *  and returns `true` only if @p t does as well. All concrete subclasses
     *  must override this.
     *
     *  @param t The object to compare against.
     *  @return `true` if the two objects hold equivalent values.
     */
    [[nodiscard]] virtual bool
    isEquivalent(STBase const& t) const;

    /** Return `true` if the field holds its default (zero-equivalent) value.
     *
     *  Used during serialization to omit optional fields whose value is
     *  the type default. The base implementation always returns `true`.
     */
    [[nodiscard]] virtual bool
    isDefault() const;

    /** Set the field identity for this instance.
     *
     *  @param n The `SField` descriptor to associate with this object.
     */
    void
    setFName(SField const& n);

    /** Return the `SField` descriptor that identifies this field. */
    [[nodiscard]] SField const&
    getFName() const;

    /** Write the type+field ID prefix bytes to @p s.
     *
     *  Encodes the combined type code and field code as 1–3 bytes per the
     *  XRPL binary format, forming the header that precedes the value bytes
     *  written by `add()`. Asserts that the current field is a binary
     *  (wire-representable) field.
     *
     *  @param s The `Serializer` to write the field ID into.
     */
    void
    addFieldID(Serializer& s) const;

protected:
    /** Placement helper for the small-object optimization used by `detail::STVar`.
     *
     *  If `sizeof(U) <= n`, constructs a `U` in @p buf via placement-new and
     *  returns a pointer to it. Otherwise heap-allocates a `U` with `new`.
     *  Concrete subclasses delegate to this from their `copy()` and `move()`
     *  overrides so that `detail::STVar` can store small types inline without
     *  a separate heap allocation.
     *
     *  @tparam T The value type to construct (deduced); `U = std::decay_t<T>`.
     *  @param n  Size of the inline buffer in bytes.
     *  @param buf Pointer to inline storage of at least @p n bytes.
     *  @param val The value to forward-construct into the buffer or heap.
     *  @return Pointer to the newly constructed `U` object.
     */
    template <class T>
    static STBase*
    emplace(std::size_t n, void* buf, T&& val);

private:
    /** Copy this object into @p buf (or heap) and return a pointer to it.
     *
     *  Called exclusively by `detail::STVar` to implement copy construction.
     *  Delegates to `emplace(n, buf, *this)`.
     */
    virtual STBase*
    copy(std::size_t n, void* buf) const;

    /** Move this object into @p buf (or heap) and return a pointer to it.
     *
     *  Called exclusively by `detail::STVar` to implement move construction.
     *  Delegates to `emplace(n, buf, std::move(*this))`.
     */
    virtual STBase*
    move(std::size_t n, void* buf);

    friend class detail::STVar;
};

//------------------------------------------------------------------------------

/** Stream an `STBase` as its full-text representation (field name and value).
 *
 *  Equivalent to `out << t.getFullText()`.
 *
 *  @param out The output stream to write to.
 *  @param t The serialized-type object to render.
 *  @return @p out, to allow chaining.
 */
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
