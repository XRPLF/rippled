/** @file
 *  Type-erased variant storage for all XRPL serialized field types.
 *
 *  Declares `STVar`, the small-object-optimized container that `STObject`
 *  uses to store heterogeneous `STBase`-derived values by value. Also
 *  declares the tag types and the `makeStvar<T>()` factory.
 *
 *  @see xrpl::detail::STVar
 *  @see xrpl::STObject
 */

#pragma once

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <type_traits>

namespace xrpl::detail {

/** Tag type that selects the default-valued construction path in `STVar`.
 *
 *  Pass the global `gDefaultObject` singleton to `STVar(DefaultObjectT,
 *  SField)` to construct a field whose value is the type default. The
 *  explicit constructor prevents accidental implicit use.
 */
struct DefaultObjectT
{
    explicit DefaultObjectT() = default;
};

/** Tag type that selects the absent-field (not-present) construction path in
 *  `STVar`.
 *
 *  Pass the global `gNonPresentObject` singleton to
 *  `STVar(NonPresentObjectT, SField)` to create a bare `STBase` sentinel
 *  representing a schema slot that carries no value in a particular object
 *  instance (`STI_NOTPRESENT`). The explicit constructor prevents accidental
 *  implicit use.
 */
struct NonPresentObjectT
{
    explicit NonPresentObjectT() = default;
};

/** Global sentinel for default-valued object construction.
 *
 *  Passed as the first argument to `STVar(DefaultObjectT, SField)`.
 */
extern DefaultObjectT gDefaultObject;

/** Global sentinel for absent-field object construction.
 *
 *  Passed as the first argument to `STVar(NonPresentObjectT, SField)`.
 */
extern NonPresentObjectT gNonPresentObject;

/** Concept constraining the variadic argument lists accepted by `constructST`.
 *
 *  Permits exactly two calling forms:
 *  - `(SField)` — default-constructs the concrete `ST*` type.
 *  - `(SerialIter, SField)` — deserializes the concrete `ST*` type from a
 *    wire-format stream.
 *
 *  Any other argument combination is a compile-time error, preventing misuse
 *  of the template dispatch switch.
 */
template <typename... Args>
concept ValidConstructSTArgs =
    (std::is_same_v<std::tuple<std::remove_cvref_t<Args>...>, std::tuple<SField>> ||
     std::is_same_v<std::tuple<std::remove_cvref_t<Args>...>, std::tuple<SerialIter, SField>>);

/** Type-erased, small-object-optimized container for any XRPL serialized type.
 *
 *  `STVar` is the storage primitive underlying `STObject`: every field in a
 *  transaction or ledger entry is stored as a `detail::STVar` inside
 *  `STObject`'s field vector. Because fields span roughly two dozen concrete
 *  `STBase` subclasses, `STVar` provides uniform value semantics over the
 *  polymorphic hierarchy.
 *
 *  **Small-object optimization:** objects whose `sizeof` fits within
 *  `kMAX_SIZE` (72 bytes) are placement-new'd directly into the inline
 *  aligned-storage buffer `d_`, avoiding a heap allocation. Larger types
 *  (e.g., `STPathSet`, `STObject`, `STArray`) fall back to `new`. The
 *  `onHeap()` predicate distinguishes the two cases by comparing `p_`
 *  against `&d_`.
 *
 *  **Move semantics:** moving a heap-resident object is a zero-copy pointer
 *  transfer. Moving an inline object requires calling the virtual
 *  `STBase::move()` to relocate it into the new buffer, because the source
 *  buffer address becomes invalid once the source `STVar` is destroyed.
 *
 *  **Depth guard:** the deserialization constructor enforces a maximum nesting
 *  depth of 10 for `STObject`/`STArray` containers to prevent stack
 *  exhaustion from malformed or malicious wire data.
 *
 *  @note `STVar` is an internal implementation detail (`xrpl::detail`). Public
 *      callers interact with fields through `STObject` and its proxy accessors.
 *  @see STBase
 *  @see STObject
 */
class STVar
{
private:
    /** Inline buffer size in bytes; objects at or below this threshold are
     *  stored in-place rather than on the heap. */
    static std::size_t constexpr kMAX_SIZE = 72;

    std::aligned_storage<kMAX_SIZE>::type d_ = {};
    STBase* p_ = nullptr;

public:
    ~STVar();
    STVar(STVar const& other);
    STVar(STVar&& other);
    STVar&
    operator=(STVar const& rhs);
    STVar&
    operator=(STVar&& rhs);

    /** Move-construct from a bare `STBase` rvalue.
     *
     *  Delegates to `STBase::move()`, which places the object into `d_` if
     *  it fits or heap-allocates it otherwise. The source object is in a
     *  valid-but-unspecified state after this call.
     *
     *  @param t The `STBase`-derived rvalue to move into this container.
     */
    STVar(STBase&& t)  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
    {
        p_ = t.move(kMAX_SIZE, &d_);
    }

    /** Copy-construct from a bare `STBase` lvalue.
     *
     *  Delegates to `STBase::copy()`, which places a copy of @p t into `d_`
     *  if it fits or heap-allocates it otherwise.
     *
     *  @param t The `STBase`-derived object to copy into this container.
     */
    STVar(STBase const& t)
    {
        p_ = t.copy(kMAX_SIZE, &d_);
    }

    /** Construct a default-valued field.
     *
     *  Creates an instance of the concrete `ST*` type identified by
     *  `name.fieldType`, initialized to that type's default value. Pass
     *  `gDefaultObject` as the tag argument.
     *
     *  @param name The field descriptor; `name.fieldType` selects the
     *      concrete subtype to construct.
     */
    STVar(DefaultObjectT, SField const& name);

    /** Construct an absent-field sentinel.
     *
     *  Creates a bare `STBase` with `getSType() == STI_NOTPRESENT`,
     *  representing a schema slot that has no value in this particular
     *  object instance. Pass `gNonPresentObject` as the tag argument.
     *
     *  @param name The field that is absent.
     */
    STVar(NonPresentObjectT, SField const& name);

    /** Deserialize a field from a wire-format byte stream.
     *
     *  Dispatches to the appropriate `ST*` deserialization constructor based
     *  on `name.fieldType`. For `STObject` and `STArray`, the current
     *  `depth` is incremented before recursing into nested `STVar`
     *  construction to enforce the nesting limit.
     *
     *  @param sit   Iterator positioned at the start of the serialized value.
     *  @param name  The field descriptor; `name.fieldType` selects the type.
     *  @param depth Current nesting depth; must not exceed 10.
     *  @throws std::runtime_error if `depth` exceeds 10.
     */
    STVar(SerialIter& sit, SField const& name, int depth = 0);

    /** Return a mutable reference to the contained `STBase` object. */
    STBase&
    get()
    {
        return *p_;
    }

    /** Dereference to a mutable `STBase` reference. */
    STBase&
    operator*()
    {
        return get();
    }

    /** Arrow operator for mutable member access on the contained object. */
    STBase*
    operator->()
    {
        return &get();
    }

    /** Return a const reference to the contained `STBase` object. */
    [[nodiscard]] STBase const&
    get() const
    {
        return *p_;
    }

    /** Dereference to a const `STBase` reference. */
    STBase const&
    operator*() const
    {
        return get();
    }

    /** Arrow operator for const member access on the contained object. */
    STBase const*
    operator->() const
    {
        return &get();
    }

    template <class T, class... Args>
    friend STVar
    makeStvar(Args&&... args);

private:
    STVar() = default;

    STVar(SerializedTypeID id, SField const& name);

    void
    destroy();

    /** Placement-new a `T` into the inline buffer or the heap.
     *
     *  If `sizeof(T) > kMAX_SIZE`, allocates via `new`. Otherwise,
     *  placement-news directly into `d_`. Sets `p_` to point at the result.
     *
     *  @tparam T    The concrete `STBase` subtype to construct.
     *  @tparam Args Constructor argument types forwarded to `T`.
     *  @param args  Arguments forwarded to the `T` constructor.
     */
    template <class T, class... Args>
    void
    construct(Args&&... args)
    {
        if constexpr (sizeof(T) > kMAX_SIZE)
        {
            p_ = new T(std::forward<Args>(args)...);
        }
        else
        {
            p_ = new (&d_) T(std::forward<Args>(args)...);
        }
    }

    /** Dispatch construction of the concrete `ST*` type identified by `id`.
     *
     *  Maps every `SerializedTypeID` to the appropriate `construct<T>()` call.
     *  For `STObject` and `STArray`, forwards `depth` so the nesting limit
     *  propagates into their own recursive `STVar` construction.
     *  `STI_NOTPRESENT` always constructs a bare `STBase` regardless of
     *  `args`.
     *
     *  @tparam Args Constrained by `ValidConstructSTArgs` to `(SField)` or
     *      `(SerialIter, SField)`.
     *  @param id    Selects the concrete `ST*` subtype.
     *  @param depth Current nesting depth; forwarded only to `STObject` and
     *      `STArray` constructors.
     *  @param args  Construction arguments forwarded to the selected type.
     *  @throws std::runtime_error if `id` is not a recognised
     *      `SerializedTypeID`.
     */
    template <typename... Args>
        requires ValidConstructSTArgs<Args...>
    void
    constructST(SerializedTypeID id, int depth, Args&&... arg);

    /** Return `true` if the contained object was allocated on the heap.
     *
     *  Compares `p_` against the address of the inline buffer `d_`. Used by
     *  the move constructor and destructor to select between pointer-steal
     *  and virtual-move / explicit-destruct paths respectively.
     */
    [[nodiscard]] bool
    onHeap() const
    {
        return static_cast<void const*>(p_) != static_cast<void const*>(&d_);
    }
};

/** Construct an `STVar` holding a concrete `T` directly, bypassing type-ID
 *  dispatch.
 *
 *  Use this factory in well-typed contexts where the exact `STBase` subclass
 *  is already known, avoiding the overhead of the `constructST` switch. The
 *  small-object optimization applies: `T` is placement-new'd into the inline
 *  buffer when `sizeof(T) <= 72`, otherwise heap-allocated.
 *
 *  @tparam T    The concrete `STBase` subtype to store.
 *  @tparam Args Constructor argument types forwarded to `T`.
 *  @param args  Arguments forwarded to the `T` constructor.
 *  @return An `STVar` owning the newly constructed `T` instance.
 */
template <class T, class... Args>
inline STVar
makeStvar(Args&&... args)
{
    STVar st;
    st.construct<T>(std::forward<Args>(args)...);
    return st;
}

/** Value equality: delegates to `STBase::isEquivalent()`.
 *
 *  Compares field values but not field names, consistent with `STBase`
 *  semantics — two fields of equal value but different names are considered
 *  equivalent at the `STVar` level.
 *
 *  @param lhs Left-hand operand.
 *  @param rhs Right-hand operand.
 *  @return `true` if the contained values are equivalent.
 */
inline bool
operator==(STVar const& lhs, STVar const& rhs)
{
    return lhs.get().isEquivalent(rhs.get());
}

/** Value inequality: opposite of `operator==`.
 *
 *  @param lhs Left-hand operand.
 *  @param rhs Right-hand operand.
 *  @return `true` if the contained values are not equivalent.
 */
inline bool
operator!=(STVar const& lhs, STVar const& rhs)
{
    return !(lhs == rhs);
}

}  // namespace xrpl::detail
