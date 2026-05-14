/** @file
 *  Type-erased variant storage for XRPL serialized types.
 *
 *  Provides the out-of-line definitions for `STVar`: constructors, assignment
 *  operators, destructor, and the central `constructST` type-dispatch function.
 *  `STVar` uses a small-object optimization — objects up to 72 bytes are stored
 *  in an aligned stack buffer (`d_`) rather than the heap, eliminating the
 *  majority of allocations during transaction processing and ledger deserialization.
 *
 *  @see xrpl::detail::STVar
 *  @see xrpl::STObject
 *  @see xrpl::STArray
 */

#include <xrpl/protocol/detail/STVar.h>

#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/Serializer.h>

#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace xrpl::detail {

/** Global sentinel for default-valued object construction.
 *
 *  Passed to `STVar(DefaultObjectT, SField)` to construct a field with its
 *  type's default value. The tag disambiguates from other construction paths.
 */
DefaultObjectT gDefaultObject;

/** Global sentinel for absent-field object construction.
 *
 *  Passed to `STVar(NonPresentObjectT, SField)` to construct a bare `STBase`
 *  with `STI_NOTPRESENT`, representing a schema field that is absent from a
 *  specific object instance.
 */
NonPresentObjectT gNonPresentObject;

//------------------------------------------------------------------------------

/** Destroy the contained object, releasing any heap allocation. */
STVar::~STVar()
{
    destroy();
}

/** Copy-construct from another STVar.
 *
 *  Delegates to the virtual `STBase::copy()` on the source object, which
 *  re-applies the small-object size check independently for the destination
 *  buffer. The source is left unchanged.
 *
 *  @param other The STVar to copy from; may be empty (`p_ == nullptr`).
 */
STVar::STVar(STVar const& other)
{
    if (other.p_ != nullptr)
        p_ = other.p_->copy(kMAX_SIZE, &d_);
}

/** Move-construct from another STVar.
 *
 *  If the source object lives on the heap, ownership is transferred by pointer
 *  swap — an O(1) zero-copy operation. If it lives in the source's stack buffer
 *  the object is move-constructed into this object's buffer via the virtual
 *  `STBase::move()`, since stack buffer addresses are non-transferable.
 *
 *  @param other The STVar to move from; left in an empty (`p_ == nullptr`) state.
 */
STVar::STVar(STVar&& other)
{
    if (other.onHeap())
    {
        p_ = other.p_;
        other.p_ = nullptr;
    }
    else
    {
        p_ = other.p_->move(kMAX_SIZE, &d_);
    }
}

/** Copy-assign from another STVar.
 *
 *  Destroys the current contents before copying the source. Self-assignment is
 *  a no-op. Delegates to `STBase::copy()` for the actual object duplication.
 *
 *  @param rhs The STVar to copy from.
 *  @return `*this`
 */
STVar&
STVar::operator=(STVar const& rhs)
{
    if (&rhs != this)
    {
        destroy();
        if (rhs.p_ != nullptr)
        {
            p_ = rhs.p_->copy(kMAX_SIZE, &d_);
        }
        else
        {
            p_ = nullptr;
        }
    }

    return *this;
}

/** Move-assign from another STVar.
 *
 *  Destroys the current contents before taking ownership of `rhs`. Applies the
 *  same heap-steal vs. buffer-move logic as the move constructor. Self-assignment
 *  is a no-op.
 *
 *  @param rhs The STVar to move from; left in an empty state on success.
 *  @return `*this`
 */
STVar&
STVar::operator=(STVar&& rhs)
{
    if (&rhs != this)
    {
        destroy();
        if (rhs.onHeap())
        {
            p_ = rhs.p_;
            rhs.p_ = nullptr;
        }
        else
        {
            p_ = rhs.p_->move(kMAX_SIZE, &d_);
        }
    }

    return *this;
}

/** Construct a default-valued object for the given field.
 *
 *  Tag-dispatch wrapper that delegates to `STVar(SerializedTypeID, SField)`
 *  using `name.fieldType` as the type selector.
 *
 *  @param name The field whose type determines which `ST*` subtype is created.
 */
STVar::STVar(DefaultObjectT, SField const& name) : STVar(name.fieldType, name)
{
}

/** Construct an absent-field sentinel for the given field.
 *
 *  Creates a bare `STBase` with type `STI_NOTPRESENT`, representing a schema
 *  slot that exists in the object template but carries no value in this
 *  particular instance.
 *
 *  @param name The field that is absent.
 */
STVar::STVar(NonPresentObjectT, SField const& name) : STVar(STI_NOTPRESENT, name)
{
}

/** Deserialize an `STVar` from a wire-format byte stream.
 *
 *  Dispatches to the appropriate `ST*` deserialization constructor based on
 *  `name.fieldType`. Enforces a hard nesting-depth limit to prevent stack
 *  exhaustion from maliciously crafted or corrupt data containing deeply nested
 *  `STObject` or `STArray` fields.
 *
 *  @param sit   Iterator positioned at the start of the serialized field value.
 *  @param name  The field descriptor; its `fieldType` selects the concrete type.
 *  @param depth Current nesting depth, incremented by each recursive container.
 *  @throws std::runtime_error if `depth` exceeds 10.
 */
STVar::STVar(SerialIter& sit, SField const& name, int depth)
{
    if (depth > 10)
        Throw<std::runtime_error>("Maximum nesting depth of STVar exceeded");
    constructST(name.fieldType, depth, sit, name);
}

/** Construct a default-valued object of a specific serialized type.
 *
 *  Used internally when constructing absent-field sentinels (`STI_NOTPRESENT`)
 *  or default-valued fields. The `id` must equal `name.fieldType` unless
 *  `id == STI_NOTPRESENT`.
 *
 *  @param id    The serialized type to instantiate; must match `name.fieldType`
 *      or be `STI_NOTPRESENT`.
 *  @param name  The associated field descriptor.
 */
STVar::STVar(SerializedTypeID id, SField const& name)
{
    XRPL_ASSERT(
        (id == STI_NOTPRESENT) || (id == name.fieldType),
        "xrpl::detail::STVar::STVar(SerializedTypeID) : valid type input");
    constructST(id, 0, name);
}

/** Destroy the contained object and reset to the empty state.
 *
 *  Uses `delete` for heap-allocated objects and an explicit destructor call for
 *  objects that were placement-new'd into the stack buffer. Calling `delete` on
 *  a pointer into `d_` would attempt to free stack memory and is undefined
 *  behaviour — this asymmetry is intentional. Resets `p_` to `nullptr` in both
 *  cases.
 */
void
STVar::destroy()
{
    if (onHeap())
    {
        delete p_;
    }
    else
    {
        p_->~STBase();
    }

    p_ = nullptr;
}

/** Dispatch construction of the concrete `ST*` type identified by `id`.
 *
 *  The `ValidConstructSTArgs` concept restricts `args` to exactly one of two
 *  forms: `(SField)` for default construction, or `(SerialIter, SField)` for
 *  deserialization. The `constructWithDepth` local lambda uses `if constexpr`
 *  to select the appropriate calling convention at compile time for container
 *  types (`STObject`, `STArray`) that require `depth` to propagate the nesting
 *  limit into their own recursive `STVar` construction.
 *
 *  @note `STI_NOTPRESENT` always constructs a bare `STBase` regardless of
 *      `args`, since the field is absent and carries no value.
 *
 *  @tparam Args Constrained to `(SField)` or `(SerialIter, SField)`.
 *  @param id    The serialized type ID selecting the concrete `ST*` subtype.
 *  @param depth Current nesting depth; forwarded to `STObject` and `STArray`
 *      constructors only — all other types ignore it.
 *  @param args  Construction arguments forwarded to the selected `ST*` type.
 *  @throws std::runtime_error if `id` is not a recognised `SerializedTypeID`.
 */
template <typename... Args>
    requires ValidConstructSTArgs<Args...>
void
STVar::constructST(SerializedTypeID id, int depth, Args&&... args)
{
    auto constructWithDepth = [&]<typename T>() {
        if constexpr (std::is_same_v<std::tuple<std::remove_cvref_t<Args>...>, std::tuple<SField>>)
        {
            construct<T>(std::forward<Args>(args)...);
        }
        else if constexpr (
            std::
                is_same_v<std::tuple<std::remove_cvref_t<Args>...>, std::tuple<SerialIter, SField>>)
        {
            construct<T>(std::forward<Args>(args)..., depth);
        }
        else
        {
            constexpr bool kALWAYS_FALSE =
                !std::is_same_v<std::tuple<Args...>, std::tuple<Args...>>;
            static_assert(kALWAYS_FALSE, "Invalid STVar constructor arguments");
        }
    };

    switch (id)
    {
        case STI_NOTPRESENT: {
            // Last argument is always SField
            SField const& field = std::get<sizeof...(args) - 1>(std::forward_as_tuple(args...));
            construct<STBase>(field);
            return;
        }
        case STI_UINT8:
            construct<STUInt8>(std::forward<Args>(args)...);
            return;
        case STI_UINT16:
            construct<STUInt16>(std::forward<Args>(args)...);
            return;
        case STI_UINT32:
            construct<STUInt32>(std::forward<Args>(args)...);
            return;
        case STI_UINT64:
            construct<STUInt64>(std::forward<Args>(args)...);
            return;
        case STI_AMOUNT:
            construct<STAmount>(std::forward<Args>(args)...);
            return;
        case STI_NUMBER:
            construct<STNumber>(std::forward<Args>(args)...);
            return;
        case STI_UINT128:
            construct<STUInt128>(std::forward<Args>(args)...);
            return;
        case STI_UINT160:
            construct<STUInt160>(std::forward<Args>(args)...);
            return;
        case STI_UINT192:
            construct<STUInt192>(std::forward<Args>(args)...);
            return;
        case STI_UINT256:
            construct<STUInt256>(std::forward<Args>(args)...);
            return;
        case STI_INT32:
            construct<STInt32>(std::forward<Args>(args)...);
            return;
        case STI_VECTOR256:
            construct<STVector256>(std::forward<Args>(args)...);
            return;
        case STI_VL:
            construct<STBlob>(std::forward<Args>(args)...);
            return;
        case STI_ACCOUNT:
            construct<STAccount>(std::forward<Args>(args)...);
            return;
        case STI_PATHSET:
            construct<STPathSet>(std::forward<Args>(args)...);
            return;
        case STI_OBJECT:
            constructWithDepth.template operator()<STObject>();
            return;
        case STI_ARRAY:
            constructWithDepth.template operator()<STArray>();
            return;
        case STI_ISSUE:
            construct<STIssue>(std::forward<Args>(args)...);
            return;
        case STI_XCHAIN_BRIDGE:
            construct<STXChainBridge>(std::forward<Args>(args)...);
            return;
        case STI_CURRENCY:
            construct<STCurrency>(std::forward<Args>(args)...);
            return;
        default:
            Throw<std::runtime_error>("Unknown object type");
    }
}

}  // namespace xrpl::detail
