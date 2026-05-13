/** @file
 *  Iterator implementation for Json::Value traversal.
 *
 *  This file is not compiled as an independent translation unit; it is
 *  `#include`-d directly by `json_value.cpp`. The split is a readability
 *  convention inherited from the upstream JsonCpp library: the iterator
 *  implementation lives in its own file while still sharing internal types
 *  that are private to `json_value.cpp`.
 */
// included by json_value.cpp

#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>

namespace json {

// --- ValueIteratorBase ---

/** Construct a null-state iterator.
 *
 *  Sets `isNull_` to true so that comparisons between two default-constructed
 *  iterators short-circuit before touching `current_`, which is left as a
 *  default-constructed (and therefore non-comparable) `std::map` iterator.
 */
ValueIteratorBase::ValueIteratorBase() : isNull_(true)
{
}

/** Construct an iterator wrapping a live map iterator.
 *
 *  @param current  A valid iterator into the `Value::ObjectValues` map.
 *      The caller is responsible for ensuring `current` is not past-the-end
 *      before any dereference operation.
 */
ValueIteratorBase::ValueIteratorBase(Value::ObjectValues::iterator const& current)
    : current_(current), isNull_(false)
{
}

Value&
ValueIteratorBase::deref() const
{
    return current_->second;
}

void
ValueIteratorBase::increment()
{
    ++current_;
}

void
ValueIteratorBase::decrement()
{
    --current_;
}

/** Compute the number of steps from this iterator to @p other.
 *
 *  Walks forward from `current_` to `other.current_`, counting steps.
 *  The result is undefined if `other` precedes `*this` in the underlying map.
 *
 *  @param other  The target iterator; must refer to the same container.
 *  @return       The non-negative distance, or 0 if both iterators are null.
 *
 *  @note When both iterators are null (i.e., iterators over a JSON null
 *      value), their underlying `current_` members are default-constructed
 *      `std::map` iterators that cannot be compared portably — two separate
 *      default-constructed `std::map::iterator` instances are not guaranteed
 *      to compare equal even when logically equivalent. The null check
 *      short-circuits this undefined behavior and returns 0 immediately.
 *
 *  @note `std::distance()` is intentionally avoided: the Sun Studio 12
 *      RogueWave STL (the default on Solaris at the time) failed to compile
 *      `std::distance` for non-random-access iterators. The hand-rolled loop
 *      is an O(n) portability tradeoff.
 */
ValueIteratorBase::difference_type
ValueIteratorBase::computeDistance(SelfType const& other) const
{
    if (isNull_ && other.isNull_)
    {
        return 0;
    }

    difference_type myDistance = 0;

    for (Value::ObjectValues::iterator it = current_; it != other.current_; ++it)
    {
        ++myDistance;
    }

    return myDistance;
}

/** Test iterator equality, handling the null-iterator edge case.
 *
 *  Two null iterators compare equal regardless of their `current_` state,
 *  avoiding undefined behavior from comparing default-constructed map
 *  iterators.
 *
 *  @param other  The iterator to compare against.
 *  @return       True if both iterators refer to the same position or are
 *      both null.
 */
bool
ValueIteratorBase::isEqual(SelfType const& other) const
{
    if (isNull_)
    {
        return other.isNull_;
    }

    return current_ == other.current_;
}

void
ValueIteratorBase::copy(SelfType const& other)
{
    current_ = other.current_;
}

/** Return the current key as a typed Value.
 *
 *  For object members, returns a string `Value`. If the underlying
 *  `CZString` key was created from a compile-time-constant (static string),
 *  a `StaticString` wrapper is used to avoid heap allocation. For array
 *  elements, returns an integer `Value` holding the element's index.
 *
 *  @return  A `Value` of type string or integer depending on the container
 *      being iterated.
 */
Value
ValueIteratorBase::key() const
{
    Value::CZString const czString = (*current_).first;

    if (czString.cStr() != nullptr)
    {
        if (czString.isStaticString())
            return Value(StaticString(czString.cStr()));

        return Value(czString.cStr());
    }

    return Value(czString.index());
}

/** Return the array index of the current element.
 *
 *  @return  The zero-based array index when iterating an array, or
 *      `Value::UInt(-1)` (the maximum unsigned value, used as "not
 *      applicable") when iterating an object.
 */
UInt
ValueIteratorBase::index() const
{
    Value::CZString const czString = (*current_).first;

    if (czString.cStr() == nullptr)
        return czString.index();

    return Value::UInt(-1);
}

/** Return the object member name of the current element.
 *
 *  @return  A non-null, NUL-terminated C string holding the member name
 *      when iterating a JSON object, or `""` when iterating an array
 *      (where keys are integer indices rather than strings).
 *
 *  @note The empty-string fallback ensures callers never receive a null
 *      pointer, preventing null-dereference crashes when the iterator is
 *      used on a non-object container.
 */
char const*
ValueIteratorBase::memberName() const
{
    char const* name = (*current_).first.cStr();
    return (name != nullptr) ? name : "";
}

// --- ValueConstIterator ---

/** Construct a const iterator wrapping a live map iterator.
 *
 *  Only `Value` (a friend) calls this constructor directly; external code
 *  obtains const iterators via `Value::begin()` / `Value::end()`.
 *
 *  @param current  A valid iterator into the `Value::ObjectValues` map.
 */
ValueConstIterator::ValueConstIterator(Value::ObjectValues::iterator const& current)
    : ValueIteratorBase(current)
{
}

/** Assign from any `ValueIteratorBase`, including a mutable `ValueIterator`.
 *
 *  Accepting `ValueIteratorBase const&` rather than `SelfType const&` allows
 *  a mutable `ValueIterator` to be implicitly converted to a const iterator
 *  via assignment, mirroring the standard-library pattern of
 *  `const_iterator = iterator`.
 *
 *  @param other  The source iterator; may be a `ValueIterator` or another
 *      `ValueConstIterator`.
 *  @return       Reference to `*this`.
 */
ValueConstIterator&
ValueConstIterator::operator=(ValueIteratorBase const& other)
{
    copy(other);
    return *this;
}

// --- ValueIterator ---

/** Construct a mutable iterator wrapping a live map iterator.
 *
 *  Only `Value` (a friend) calls this constructor directly; external code
 *  obtains mutable iterators via `Value::begin()` / `Value::end()`.
 *
 *  @param current  A valid iterator into the `Value::ObjectValues` map.
 */
ValueIterator::ValueIterator(Value::ObjectValues::iterator const& current)
    : ValueIteratorBase(current)
{
}

/** Construct a mutable iterator from a const iterator.
 *
 *  Allows code that holds a `ValueConstIterator` to seed a mutable
 *  `ValueIterator` at construction time.
 *
 *  @param other  The const iterator to copy position from.
 */
ValueIterator::ValueIterator(ValueConstIterator const& other) : ValueIteratorBase(other)
{
}

ValueIterator::ValueIterator(ValueIterator const& other) = default;

/** Copy-assign from another mutable iterator.
 *
 *  @param other  The source iterator.
 *  @return       Reference to `*this`.
 */
ValueIterator&
ValueIterator::operator=(SelfType const& other)
{
    copy(other);
    return *this;
}

}  // namespace json
