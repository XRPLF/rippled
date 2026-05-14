/** @file
 *  Implements `Json::Value`, the discriminated-union type that represents any
 *  JSON datum used throughout rippled for API construction, parsing, and RPC
 *  message handling.  Every inbound request and outbound response passes
 *  through `Value` objects.
 *
 *  The implementation covers the `CZString` map-key helper, string memory
 *  management via `DefaultValueAllocator`, all seven `ValueType` paths for
 *  copy/move/destruction, type-coercion accessors, and the sparse-array
 *  model used by `arrayValue`.
 */
#include <xrpl/json/json_value.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/detail/json_assert.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_writer.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace json {

Value const Value::kNULL;

/** Sole concrete `ValueAllocator` implementation.
 *
 *  Allocates string memory via `malloc`/`free` rather than `new`/`delete` so
 *  that a future custom allocator (pool, arena) could be dropped in by
 *  replacing the global singleton without recompiling callers.
 */
class DefaultValueAllocator : public ValueAllocator
{
public:
    ~DefaultValueAllocator() override = default;

    /** Duplicate a member name string into heap storage.
     *
     *  @param memberName Null-terminated string to copy.
     *  @return Heap-allocated copy; caller is responsible for calling
     *      releaseMemberName() when done.
     */
    char*
    makeMemberName(char const* memberName) override
    {
        return duplicateStringValue(memberName);
    }

    /** Release a member name previously allocated by makeMemberName().
     *
     *  @param memberName Pointer returned by makeMemberName(); may be null.
     */
    void
    releaseMemberName(char* memberName) override
    {
        releaseStringValue(memberName);
    }

    /** Heap-copy a string value, optionally using a pre-computed length.
     *
     *  Passing an explicit `length` avoids a `strlen` scan for callers that
     *  already know the size (e.g., `std::string`).  If `value` is null,
     *  length is forced to zero and the result is a heap-allocated empty
     *  string.
     *
     *  @param value  Source string; may be null (produces an empty string).
     *  @param length Byte count to copy, or `kUNKNOWN` to compute via `strlen`.
     *  @return Null-terminated heap copy; free with releaseStringValue().
     */
    char*
    duplicateStringValue(char const* value, unsigned int length = kUNKNOWN) override
    {
        //@todo investigate this old optimization
        // if ( !value  ||  value[0] == 0 )
        //   return 0;

        if (length == kUNKNOWN)
            length = (value != nullptr) ? (unsigned int)strlen(value) : 0;

        char* newString = static_cast<char*>(malloc(length + 1));
        if (value != nullptr)
            memcpy(newString, value, length);
        newString[length] = 0;
        return newString;
    }

    /** Release a string previously allocated by duplicateStringValue().
     *
     *  @param value Pointer to free; no-op if null.
     */
    void
    releaseStringValue(char* value) override
    {
        if (value != nullptr)
            free(value);
    }
};

/** Return the process-wide `ValueAllocator` singleton by reference.
 *
 *  Constructed on first call as a `DefaultValueAllocator`.  The reference
 *  return allows the pointer to be swapped at runtime, although no production
 *  code currently does so.
 *
 *  @return Mutable reference to the current allocator pointer.
 */
static ValueAllocator*&
valueAllocator()
{
    static ValueAllocator* kVALUE_ALLOCATOR = new DefaultValueAllocator;  // NOLINT TODO
    return kVALUE_ALLOCATOR;
}

/** Forces `valueAllocator()` to be initialised before `main()` starts.
 *
 *  Without this guard, a `Value` object constructed during static
 *  initialisation (before `main`) would call `valueAllocator()` before its
 *  internal static is initialised, invoking undefined behaviour.  The global
 *  instance of this struct calls `valueAllocator()` during its own
 *  construction, which is sequenced before user-code statics.
 */
static struct DummyValueAllocatorInitializer
{
    DummyValueAllocatorInitializer()
    {
        valueAllocator();  // ensure valueAllocator() statics are initialized
                           // before main().
    }
} gDummyValueAllocatorInitializer;

// --- Value::CZString ---

/** Construct a numeric (array-index) key.
 *
 *  Leaves `cstr_` null; `index_` carries the array position.
 *
 *  @param index Zero-based array index.
 */
Value::CZString::CZString(int index) : cstr_(0), index_(index)
{
}

/** Construct a string (object-member) key with an explicit duplication policy.
 *
 *  When `allocate` is `Duplicate`, the string is heap-copied immediately via
 *  the global allocator.  Otherwise `cstr_` stores the raw pointer and the
 *  caller is responsible for keeping the pointed-to string alive.
 *
 *  @param cstr     Null-terminated key string.
 *  @param allocate Ownership policy; stored in `index_` as its integer value.
 */
Value::CZString::CZString(char const* cstr, DuplicationPolicy allocate)
    : cstr_(
          allocate == DuplicationPolicy::Duplicate ? valueAllocator()->makeMemberName(cstr) : cstr)
    , index_(static_cast<int>(allocate))
{
}

/** Copy-construct a `CZString`, respecting the source's ownership policy.
 *
 *  If the source used `NoDuplication` (static string), the copy stores the
 *  same pointer without allocating.  If the source owned a heap copy, a new
 *  heap copy is made so each instance owns its memory independently.
 *  Numeric keys (null `cstr_`) are copied by value.
 */
Value::CZString::CZString(CZString const& other)
    : cstr_(
          other.index_ != static_cast<int>(DuplicationPolicy::NoDuplication) && other.cstr_ != 0
              ? valueAllocator()->makeMemberName(other.cstr_)
              : other.cstr_)
    , index_([&]() -> int {
        if (!other.cstr_)
            return other.index_;
        return other.index_ == static_cast<int>(DuplicationPolicy::NoDuplication)
            ? static_cast<int>(DuplicationPolicy::NoDuplication)
            : static_cast<int>(DuplicationPolicy::Duplicate);
    }())
{
}

/** Destroy the `CZString`, freeing heap-owned string memory if applicable. */
Value::CZString::~CZString()
{
    if ((cstr_ != nullptr) && index_ == static_cast<int>(DuplicationPolicy::Duplicate))
        valueAllocator()->releaseMemberName(const_cast<char*>(cstr_));
}

/** Total order over `CZString` values for use as `std::map` keys.
 *
 *  String keys compare lexicographically; numeric keys compare by index.
 *  Mixing string and numeric keys in the same map is not expected and would
 *  fall into the numeric branch (one `cstr_` is null).
 */
bool
Value::CZString::operator<(CZString const& other) const
{
    if ((cstr_ != nullptr) && (other.cstr_ != nullptr))
        return strcmp(cstr_, other.cstr_) < 0;

    return index_ < other.index_;
}

/** Equality comparison matching the ordering defined by `operator<`. */
bool
Value::CZString::operator==(CZString const& other) const
{
    if ((cstr_ != nullptr) && (other.cstr_ != nullptr))
        return strcmp(cstr_, other.cstr_) == 0;

    return index_ == other.index_;
}

/** Return the numeric array index, or the encoded `DuplicationPolicy` for string keys. */
int
Value::CZString::index() const
{
    return index_;
}

/** Return the raw string pointer, or null for numeric (array-index) keys. */
char const*
Value::CZString::cStr() const
{
    return cstr_;
}

/** Return true if this key points at a static (non-owned) string.
 *
 *  A static key's underlying C-string must outlive the `CZString`.  This is
 *  guaranteed when the key was constructed via `StaticString` or with
 *  `DuplicationPolicy::NoDuplication`.
 */
bool
Value::CZString::isStaticString() const
{
    return index_ == static_cast<int>(DuplicationPolicy::NoDuplication);
}

// --- Value constructors and lifecycle ---

/** Construct a default `Value` of the given type.
 *
 *  Scalars are zero-initialised; strings are null; arrays and objects receive
 *  a freshly heap-allocated `ObjectValues` map.  The zero-initialisation
 *  contract is equivalent to `memset(this, 0, sizeof(Value))` and must be
 *  preserved if the storage layout ever changes.
 *
 *  @param type The `ValueType` tag; defaults to `ValueType::Null`.
 */
Value::Value(ValueType type) : type_(type)
{
    switch (type)
    {
        case ValueType::Null:
            break;

        case ValueType::Int:
        case ValueType::UInt:
            value_.intVal = 0;
            break;

        case ValueType::Real:
            value_.realVal = 0.0;
            break;

        case ValueType::String:
            value_.stringVal = 0;
            break;

        case ValueType::Array:
        case ValueType::Object:
            value_.mapVal = new ObjectValues();
            break;

        case ValueType::Boolean:
            value_.boolVal = false;
            break;

        // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::Value(ValueType) : invalid type");
            // LCOV_EXCL_STOP
    }
}

/** Construct a signed-integer `Value`. */
Value::Value(Int value) : type_(ValueType::Int)
{
    value_.intVal = value;
}

/** Construct an unsigned-integer `Value`. */
Value::Value(UInt value) : type_(ValueType::UInt)
{
    value_.uintVal = value;
}

/** Construct a double-precision floating-point `Value`. */
Value::Value(double value) : type_(ValueType::Real)
{
    value_.realVal = value;
}

/** Construct a string `Value` by heap-copying a null-terminated C-string.
 *
 *  Sets `allocated_` so the destructor frees the copy.
 */
Value::Value(char const* value) : type_(ValueType::String), allocated_(true)
{
    value_.stringVal = valueAllocator()->duplicateStringValue(value);
}

/** Construct a string `Value` from an `xrpl::Number`, serialised via `to_string`.
 *
 *  The resulting string is heap-allocated with `allocated_` set.
 */
Value::Value(xrpl::Number const& value) : type_(ValueType::String), allocated_(true)
{
    auto const tmp = to_string(value);
    value_.stringVal = valueAllocator()->duplicateStringValue(tmp.c_str(), tmp.length());
}

/** Construct a string `Value` by heap-copying a `std::string`.
 *
 *  Uses the known length to skip an extra `strlen` scan.
 */
Value::Value(std::string const& value) : type_(ValueType::String), allocated_(true)
{
    value_.stringVal =
        valueAllocator()->duplicateStringValue(value.c_str(), (unsigned int)value.length());
}

/** Construct a string `Value` that references a static string without copying.
 *
 *  `allocated_` remains 0, so the destructor will not free the pointer.  The
 *  pointed-to string must have program-lifetime storage (e.g., a string
 *  literal or a `static const char[]`).
 */
Value::Value(StaticString const& value) : type_(ValueType::String)
{
    value_.stringVal = const_cast<char*>(value.cStr());
}

/** Construct a boolean `Value`. */
Value::Value(bool value) : type_(ValueType::Boolean)
{
    value_.boolVal = value;
}

/** Deep-copy constructor; heap-allocated strings and maps are duplicated. */
Value::Value(Value const& other) : type_(other.type_)
{
    switch (type_)
    {
        case ValueType::Null:
        case ValueType::Int:
        case ValueType::UInt:
        case ValueType::Real:
        case ValueType::Boolean:
            value_ = other.value_;
            break;

        case ValueType::String:
            if (other.value_.stringVal != nullptr)
            {
                value_.stringVal = valueAllocator()->duplicateStringValue(other.value_.stringVal);
                allocated_ = true;
            }
            else
            {
                value_.stringVal = 0;
            }

            break;

        case ValueType::Array:
        case ValueType::Object:
            value_.mapVal = new ObjectValues(*other.value_.mapVal);
            break;

        // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::Value(Value const&) : invalid type");
            // LCOV_EXCL_STOP
    }
}

/** Destroy the `Value`, releasing heap-owned strings and maps.
 *
 *  Static strings (`allocated_ == 0`) are not freed — their lifetime is
 *  managed externally by the `StaticString` contract.
 */
Value::~Value()
{
    switch (type_)
    {
        case ValueType::Null:
        case ValueType::Int:
        case ValueType::UInt:
        case ValueType::Real:
        case ValueType::Boolean:
            break;

        case ValueType::String:
            if (allocated_)
                valueAllocator()->releaseStringValue(value_.stringVal);

            break;

        case ValueType::Array:
        case ValueType::Object:
            if (value_.mapVal != nullptr)
                delete value_.mapVal;
            break;

        // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::~Value : invalid type");
            // LCOV_EXCL_STOP
    }
}

/** Copy-assign using copy-and-swap; provides strong exception safety.
 *
 *  A copy-constructed temporary is swapped into `*this`; the temporary's
 *  destructor then cleans up the old state.
 */
Value&
Value::operator=(Value const& other)
{
    Value tmp(other);
    swap(tmp);
    return *this;
}

/** Move-construct by stealing the source's union members.
 *
 *  The source is left as `nullValue` with `allocated_ = 0` so its destructor
 *  is a no-op.
 */
Value::Value(Value&& other) noexcept
    : value_(other.value_), type_(other.type_), allocated_(other.allocated_)
{
    other.type_ = ValueType::Null;
    other.allocated_ = 0;
}

/** Move-assign using move-and-swap.
 *
 *  The old contents of `*this` are destroyed when the temporary falls out of
 *  scope after the swap.
 */
Value&
Value::operator=(Value&& other)
{
    Value tmp(std::move(other));
    swap(tmp);
    return *this;
}

/** Swap all data members with `other` in O(1) without allocation. */
void
Value::swap(Value& other) noexcept
{
    std::swap(value_, other.value_);

    ValueType const temp = type_;
    type_ = other.type_;
    other.type_ = temp;

    int const temp2 = allocated_;
    allocated_ = other.allocated_;
    other.allocated_ = temp2;
}

/** Return the `ValueType` tag indicating which union branch is active. */
ValueType
Value::type() const
{
    return type_;
}

/** Compare a signed integer against an unsigned integer without UB.
 *
 *  Direct comparison would promote `i` to `unsigned`, giving the wrong result
 *  for negative values.  This helper handles the sign mismatch explicitly:
 *  any negative `i` is always less than any `UInt`.
 *
 *  @param i  Signed comparand.
 *  @param ui Unsigned comparand.
 *  @return -1, 0, or 1 in the usual three-way convention.
 */
static int
integerCmp(Int i, UInt ui)
{
    if (i < 0)
        return -1;

    if (i < ui)
        return -1;
    return (i == ui) ? 0 : 1;
}

/** Total order over `Value` objects.
 *
 *  For equal types, comparison delegates to the underlying scalar or
 *  container comparison.  For the `Int`/`UInt` cross-type pair,
 *  `integerCmp()` is used to avoid signed/unsigned promotion UB.  All other
 *  type mismatches fall back to ordering by `ValueType` enum value, giving
 *  `Value` a deterministic total order suitable for use as a `std::map` key.
 */
bool
operator<(Value const& x, Value const& y)
{
    if (auto signum = static_cast<int>(x.type_) - static_cast<int>(y.type_))
    {
        if (x.type_ == ValueType::Int && y.type_ == ValueType::UInt)
        {
            signum = integerCmp(x.value_.intVal, y.value_.uintVal);
        }
        else if (x.type_ == ValueType::UInt && y.type_ == ValueType::Int)
        {
            signum = -integerCmp(y.value_.intVal, x.value_.uintVal);
        }
        return signum < 0;
    }

    switch (x.type_)
    {
        case ValueType::Null:
            return false;

        case ValueType::Int:
            return x.value_.intVal < y.value_.intVal;

        case ValueType::UInt:
            return x.value_.uintVal < y.value_.uintVal;

        case ValueType::Real:
            return x.value_.realVal < y.value_.realVal;

        case ValueType::Boolean:
            return static_cast<int>(x.value_.boolVal) < static_cast<int>(y.value_.boolVal);

        case ValueType::String:
            return (x.value_.stringVal == 0 && (y.value_.stringVal != nullptr)) ||
                ((y.value_.stringVal != nullptr) && (x.value_.stringVal != nullptr) &&
                 strcmp(x.value_.stringVal, y.value_.stringVal) < 0);

        case ValueType::Array:
        case ValueType::Object: {
            if (int const signum = int(x.value_.mapVal->size()) - y.value_.mapVal->size())
                return signum < 0;

            return *x.value_.mapVal < *y.value_.mapVal;
        }

            // LCOV_EXCL_START
        default:
            UNREACHABLE("json::operator<(Value, Value) : invalid type");
            // LCOV_EXCL_STOP
    }

    return false;  // unreachable
}

/** Equality comparison consistent with `operator<`.
 *
 *  The `Int`/`UInt` cross-type case uses `integerCmp()` so that
 *  `Value(-1) != Value(0u)` and `Value(5) == Value(5u)` hold correctly.
 *  All other type mismatches return false without inspecting the payloads.
 */
bool
operator==(Value const& x, Value const& y)
{
    if (x.type_ != y.type_)
    {
        if (x.type_ == ValueType::Int && y.type_ == ValueType::UInt)
            return integerCmp(x.value_.intVal, y.value_.uintVal) == 0;
        if (x.type_ == ValueType::UInt && y.type_ == ValueType::Int)
            return integerCmp(y.value_.intVal, x.value_.uintVal) == 0;
        return false;
    }

    switch (x.type_)
    {
        case ValueType::Null:
            return true;

        case ValueType::Int:
            return x.value_.intVal == y.value_.intVal;

        case ValueType::UInt:
            return x.value_.uintVal == y.value_.uintVal;

        case ValueType::Real:
            return x.value_.realVal == y.value_.realVal;

        case ValueType::Boolean:
            return x.value_.boolVal == y.value_.boolVal;

        case ValueType::String:
            return x.value_.stringVal == y.value_.stringVal ||
                ((y.value_.stringVal != nullptr) && (x.value_.stringVal != nullptr) &&
                 (strcmp(x.value_.stringVal, y.value_.stringVal) == 0));

        case ValueType::Array:
        case ValueType::Object:
            return x.value_.mapVal->size() == y.value_.mapVal->size() &&
                *x.value_.mapVal == *y.value_.mapVal;

        // LCOV_EXCL_START
        default:
            UNREACHABLE("json::operator==(Value, Value) : invalid type");
            // LCOV_EXCL_STOP
    }

    return false;  // unreachable
}

/** Return the raw C-string pointer for a `stringValue`.
 *
 *  @pre `type() == ValueType::String` (asserted in debug builds).
 *  @return Pointer into the internal string buffer; valid until the `Value`
 *      is modified or destroyed.
 */
char const*
Value::asCString() const
{
    XRPL_ASSERT(type_ == ValueType::String, "json::Value::asCString : valid type");
    return value_.stringVal;
}

/** Convert the `Value` to a `std::string` with permissive coercion.
 *
 *  - `nullValue` → `""`
 *  - `stringValue` → the stored string (empty string if the pointer is null)
 *  - `boolValue` → `"true"` or `"false"`
 *  - `intValue` / `uintValue` / `realValue` → decimal via `std::to_string`
 *  - `arrayValue` / `objectValue` → asserts and aborts in debug builds
 *
 *  @return String representation of the value.
 *  @throw Does not throw; containers abort via `JSON_ASSERT_MESSAGE`.
 */
std::string
Value::asString() const
{
    switch (type_)
    {
        case ValueType::Null:
            return "";

        case ValueType::String:
            return (value_.stringVal != nullptr) ? value_.stringVal : "";

        case ValueType::Boolean:
            return value_.boolVal ? "true" : "false";

        case ValueType::Int:
            return std::to_string(value_.intVal);

        case ValueType::UInt:
            return std::to_string(value_.uintVal);

        case ValueType::Real:
            return std::to_string(value_.realVal);

        case ValueType::Array:
        case ValueType::Object:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to string");

            // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::asString : invalid type");
            // LCOV_EXCL_STOP
    }

    return "";  // unreachable
}

/** Convert the `Value` to a signed integer with range-checked coercion.
 *
 *  - `nullValue` → 0
 *  - `intValue` → identity
 *  - `uintValue` → asserts `value < kMAX_INT`
 *  - `realValue` → asserts within `[kMIN_INT, kMAX_INT]`, truncates
 *  - `boolValue` → 0 or 1
 *  - `stringValue` → parsed via `beast::lexicalCastThrow<int>`
 *  - `arrayValue` / `objectValue` → asserts and aborts
 *
 *  @return The value as `Int`.
 *  @throws std::exception (or similar) on unparseable string input.
 */
Value::Int
Value::asInt() const
{
    switch (type_)
    {
        case ValueType::Null:
            return 0;

        case ValueType::Int:
            return value_.intVal;

        case ValueType::UInt:
            JSON_ASSERT_MESSAGE(
                value_.uintVal < (unsigned)kMAX_INT, "integer out of signed integer range");
            return value_.uintVal;

        case ValueType::Real:
            JSON_ASSERT_MESSAGE(
                (value_.realVal >= kMIN_INT && value_.realVal <= kMAX_INT),
                "Real out of signed integer range");
            return Int(value_.realVal);

        case ValueType::Boolean:
            return value_.boolVal ? 1 : 0;

        case ValueType::String: {
            char const* const str{(value_.stringVal != nullptr) ? value_.stringVal : ""};
            return beast::lexicalCastThrow<int>(str);
        }

        case ValueType::Array:
        case ValueType::Object:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to int");

            // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::asInt : invalid type");
            // LCOV_EXCL_STOP
    }

    return 0;  // unreachable;
}

/** Return the absolute value of any numeric type as an unsigned integer.
 *
 *  XRPL-specific addition. Negative `intValue` is negated through `int64_t`
 *  before the cast to avoid undefined overflow for `INT_MIN` (which cannot
 *  be represented as a positive `int`).  Negative `realValue` is similarly
 *  negated after a range check.  Strings are parsed as `int64_t` first, then
 *  the absolute value is returned.
 *
 *  @return Absolute value of the numeric payload as `UInt`.
 *  @note Asserts (debug-aborts) if the value is out of `UInt` range or if
 *      the type is `arrayValue`/`objectValue`.
 */
UInt
Value::asAbsUInt() const
{
    switch (type_)
    {
        case ValueType::Null:
            return 0;

        case ValueType::Int: {
            // Doing this conversion through int64 avoids overflow error for
            // value_.intVal = -1 * 2^31 i.e. numeric_limits<int>::min().
            if (value_.intVal < 0)
                return static_cast<std::int64_t>(value_.intVal) * -1;
            return value_.intVal;
        }

        case ValueType::UInt:
            return value_.uintVal;

        case ValueType::Real: {
            if (value_.realVal < 0)
            {
                JSON_ASSERT_MESSAGE(
                    -1 * value_.realVal <= kMAX_UINT, "Real out of unsigned integer range");
                return UInt(-1 * value_.realVal);
            }
            JSON_ASSERT_MESSAGE(value_.realVal <= kMAX_UINT, "Real out of unsigned integer range");
            return UInt(value_.realVal);
        }

        case ValueType::Boolean:
            return value_.boolVal ? 1 : 0;

        case ValueType::String: {
            char const* const str{(value_.stringVal != nullptr) ? value_.stringVal : ""};
            auto const temp = beast::lexicalCastThrow<std::int64_t>(str);
            if (temp < 0)
            {
                JSON_ASSERT_MESSAGE(-1 * temp <= kMAX_UINT, "String out of unsigned integer range");
                return -1 * temp;
            }
            JSON_ASSERT_MESSAGE(temp <= kMAX_UINT, "String out of unsigned integer range");
            return temp;
        }

        case ValueType::Array:
        case ValueType::Object:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to int");

            // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::asAbsInt : invalid type");
            // LCOV_EXCL_STOP
    }

    return 0;  // unreachable;
}

/** Convert the `Value` to an unsigned integer with range-checked coercion.
 *
 *  - `nullValue` → 0
 *  - `intValue` → asserts non-negative
 *  - `uintValue` → identity
 *  - `realValue` → asserts within `[0, kMAX_UINT]`, truncates
 *  - `boolValue` → 0 or 1
 *  - `stringValue` → parsed via `beast::lexicalCastThrow<unsigned int>`
 *  - `arrayValue` / `objectValue` → asserts and aborts
 *
 *  @return The value as `UInt`.
 *  @throws std::exception (or similar) on unparseable string input.
 */
Value::UInt
Value::asUInt() const
{
    switch (type_)
    {
        case ValueType::Null:
            return 0;

        case ValueType::Int:
            JSON_ASSERT_MESSAGE(
                value_.intVal >= 0, "Negative integer can not be converted to unsigned integer");
            return value_.intVal;

        case ValueType::UInt:
            return value_.uintVal;

        case ValueType::Real:
            JSON_ASSERT_MESSAGE(
                (value_.realVal >= 0 && value_.realVal <= kMAX_UINT),
                "Real out of unsigned integer range");
            return UInt(value_.realVal);

        case ValueType::Boolean:
            return value_.boolVal ? 1 : 0;

        case ValueType::String: {
            char const* const str{(value_.stringVal != nullptr) ? value_.stringVal : ""};
            return beast::lexicalCastThrow<unsigned int>(str);
        }

        case ValueType::Array:
        case ValueType::Object:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to uint");

            // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::asUInt : invalid type");
            // LCOV_EXCL_STOP
    }

    return 0;  // unreachable;
}

/** Convert numeric or boolean `Value` to `double`.
 *
 *  - `nullValue` → 0.0
 *  - `intValue` / `uintValue` / `realValue` → widening conversion
 *  - `boolValue` → 0.0 or 1.0
 *  - `stringValue` / `arrayValue` / `objectValue` → asserts and aborts
 *
 *  @return Double representation.
 */
double
Value::asDouble() const
{
    switch (type_)
    {
        case ValueType::Null:
            return 0.0;

        case ValueType::Int:
            return value_.intVal;

        case ValueType::UInt:
            return value_.uintVal;

        case ValueType::Real:
            return value_.realVal;

        case ValueType::Boolean:
            return value_.boolVal ? 1.0 : 0.0;

        case ValueType::String:
        case ValueType::Array:
        case ValueType::Object:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to double");

            // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::asDouble : invalid type");
            // LCOV_EXCL_STOP
    }

    return 0;  // unreachable;
}

/** Convert any `Value` to a boolean with Python-style truthiness rules.
 *
 *  - `nullValue` → false
 *  - `intValue` / `uintValue` → true iff non-zero (shares `intVal` branch)
 *  - `realValue` → true iff non-zero
 *  - `boolValue` → identity
 *  - `stringValue` → true iff pointer is non-null and first char is non-NUL
 *  - `arrayValue` / `objectValue` → true iff the map is non-empty
 *
 *  @return Boolean interpretation of the value.
 */
bool
Value::asBool() const
{
    switch (type_)
    {
        case ValueType::Null:
            return false;

        case ValueType::Int:
        case ValueType::UInt:
            return value_.intVal != 0;

        case ValueType::Real:
            return value_.realVal != 0.0;

        case ValueType::Boolean:
            return value_.boolVal;

        case ValueType::String:
            return (value_.stringVal != nullptr) && value_.stringVal[0] != 0;

        case ValueType::Array:
        case ValueType::Object:
            return !value_.mapVal->empty();

            // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::asBool : invalid type");
            // LCOV_EXCL_STOP
    }

    return false;  // unreachable;
}

/** Test whether this `Value` can be coerced to `other` without data loss.
 *
 *  Encodes the same rules as the `asXxx()` accessors in predicate form so
 *  callers can check feasibility cheaply before committing to a conversion.
 *  Notable rules:
 *  - Any type can be coerced to `Null` iff its payload is the zero/empty
 *    value for that type.
 *  - `realValue` → `UInt` additionally requires that the double has no
 *    fractional component (checked via `fabs(round(x) - x) < epsilon`).
 *  - `stringValue` is only convertible to `String` or (if empty) `Null`.
 *  - `arrayValue` / `objectValue` are only convertible to themselves or
 *    (if empty) `Null`.
 *
 *  @param other Target `ValueType` to test conversion to.
 *  @return True if the conversion would succeed without assertion failure.
 */
bool
Value::isConvertibleTo(ValueType other) const
{
    switch (type_)
    {
        case ValueType::Null:
            return true;

        case ValueType::Int:
            return (other == ValueType::Null && value_.intVal == 0) || other == ValueType::Int ||
                (other == ValueType::UInt && value_.intVal >= 0) || other == ValueType::Real ||
                other == ValueType::String || other == ValueType::Boolean;

        case ValueType::UInt:
            return (other == ValueType::Null && value_.uintVal == 0) ||
                (other == ValueType::Int && value_.uintVal <= (unsigned)kMAX_INT) ||
                other == ValueType::UInt || other == ValueType::Real ||
                other == ValueType::String || other == ValueType::Boolean;

        case ValueType::Real:
            return (other == ValueType::Null && value_.realVal == 0.0) ||
                (other == ValueType::Int && value_.realVal >= kMIN_INT &&
                 value_.realVal <= kMAX_INT) ||
                (other == ValueType::UInt && value_.realVal >= 0 && value_.realVal <= kMAX_UINT &&
                 std::fabs(round(value_.realVal) - value_.realVal) <
                     std::numeric_limits<double>::epsilon()) ||
                other == ValueType::Real || other == ValueType::String ||
                other == ValueType::Boolean;

        case ValueType::Boolean:
            return (other == ValueType::Null && !value_.boolVal) || other == ValueType::Int ||
                other == ValueType::UInt || other == ValueType::Real ||
                other == ValueType::String || other == ValueType::Boolean;

        case ValueType::String:
            return other == ValueType::String ||
                (other == ValueType::Null &&
                 ((value_.stringVal == nullptr) || value_.stringVal[0] == 0));

        case ValueType::Array:
            return other == ValueType::Array ||
                (other == ValueType::Null && value_.mapVal->empty());

        case ValueType::Object:
            return other == ValueType::Object ||
                (other == ValueType::Null && value_.mapVal->empty());

        // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::isConvertible : invalid type");
            // LCOV_EXCL_STOP
    }

    return false;  // unreachable;
}

/** Return the number of elements in an array or object, or 0 for scalars.
 *
 *  For `arrayValue`, size is the index of the highest-indexed entry plus one
 *  (sparse-array semantics).  Writing `arr[5] = x` on an empty array makes
 *  `size()` return 6, even though only one slot is occupied.  For
 *  `objectValue`, size is the actual entry count.  All scalar types return 0.
 *
 *  @return Element count (sparse for arrays).
 */
Value::UInt
Value::size() const
{
    switch (type_)
    {
        case ValueType::Null:
        case ValueType::Int:
        case ValueType::UInt:
        case ValueType::Real:
        case ValueType::Boolean:
        case ValueType::String:
            return 0;

        case ValueType::Array:
            if (!value_.mapVal->empty())
            {
                ObjectValues::const_iterator itLast = value_.mapVal->end();
                --itLast;
                return (*itLast).first.index() + 1;
            }

            return 0;

        case ValueType::Object:
            return Int(value_.mapVal->size());

            // LCOV_EXCL_START
        default:
            UNREACHABLE("json::Value::size : invalid type");
            // LCOV_EXCL_STOP
    }

    return 0;  // unreachable;
}

/** Return false for null, empty string, empty array, or empty object; true otherwise.
 *
 *  Provides Python-style truthiness: a non-null, non-empty `Value` is truthy.
 *  Unlike `asBool()`, this operator is valid for all types including
 *  containers.
 */
Value::
operator bool() const
{
    if (isNull())
        return false;

    if (isString())
    {
        auto s = asCString();
        return (s != nullptr) && (s[0] != 0);
    }

    return !(isArray() || isObject()) || (size() != 0u);
}

/** Remove all elements from an array or object; no-op for null.
 *
 *  @pre `type()` is `Array`, `Object`, or `Null` (asserted).
 *  @post `type()` is unchanged and `size() == 0`.
 */
void
Value::clear()
{
    XRPL_ASSERT(
        type_ == ValueType::Null || type_ == ValueType::Array || type_ == ValueType::Object,
        "json::Value::clear : valid type");

    switch (type_)
    {
        case ValueType::Array:
        case ValueType::Object:
            value_.mapVal->clear();
            break;

        default:
            break;
    }
}

/** Access or auto-create an array element at `index` (non-const).
 *
 *  If the `Value` is `nullValue`, it is promoted to `arrayValue`.  If no
 *  entry exists at `index`, a `nullValue` is inserted and returned — the
 *  sparse-array representation means all intermediate indices remain absent
 *  until explicitly written.
 *
 *  @param index Zero-based array index.
 *  @return Reference to the element, default-inserting null if absent.
 *  @pre `type()` is `Null` or `Array` (asserted).
 */
Value&
Value::operator[](UInt index)
{
    XRPL_ASSERT(
        type_ == ValueType::Null || type_ == ValueType::Array,
        "json::Value::operator[](UInt) : valid type");

    if (type_ == ValueType::Null)
        *this = Value(ValueType::Array);

    CZString const key(index);
    ObjectValues::iterator it = value_.mapVal->lower_bound(key);

    if (it != value_.mapVal->end() && (*it).first == key)
        return (*it).second;

    ObjectValues::value_type const defaultValue(key, kNULL);
    it = value_.mapVal->insert(it, defaultValue);
    return (*it).second;
}

/** Access an array element at `index` (const); returns `kNULL` if absent.
 *
 *  Never inserts elements.  Returns `Value::kNULL` for out-of-range indices
 *  or when the `Value` is `nullValue`.
 *
 *  @param index Zero-based array index.
 *  @return Const reference to the element, or `kNULL` if not found.
 *  @pre `type()` is `Null` or `Array` (asserted).
 */
Value const&
Value::operator[](UInt index) const
{
    XRPL_ASSERT(
        type_ == ValueType::Null || type_ == ValueType::Array,
        "json::Value::operator[](UInt) const : valid type");

    if (type_ == ValueType::Null)
        return kNULL;

    CZString const key(index);
    ObjectValues::const_iterator const it = value_.mapVal->find(key);

    if (it == value_.mapVal->end())
        return kNULL;

    return (*it).second;
}

/** Access or create an object member by string key (non-const).
 *
 *  The key is treated as a transient string and will be duplicated into the
 *  map.  For hot-path code that reuses the same key, prefer `operator[](StaticString)`
 *  to avoid repeated allocation.
 *
 *  @param key Null-terminated member name.
 *  @return Reference to the member `Value`, default-inserting null if absent.
 */
Value&
Value::operator[](char const* key)
{
    return resolveReference(key, false);
}

/** Insert or locate a member key, respecting its static/dynamic ownership.
 *
 *  When `isStatic` is true, the key is stored with `NoDuplication` policy
 *  (no heap copy); when false, `DuplicateOnCopy` is used so the key is
 *  copied the first time it is inserted into the map.  Promotes `nullValue`
 *  to `objectValue` if necessary.
 *
 *  @param key      Null-terminated member name.
 *  @param isStatic True if `key` has static/program-lifetime storage.
 *  @return Reference to the (possibly newly inserted) member `Value`.
 *  @pre `type()` is `Null` or `Object` (asserted).
 */
Value&
Value::resolveReference(char const* key, bool isStatic)
{
    XRPL_ASSERT(
        type_ == ValueType::Null || type_ == ValueType::Object,
        "json::Value::resolveReference : valid type");

    if (type_ == ValueType::Null)
        *this = Value(ValueType::Object);

    CZString const actualKey(
        key,
        isStatic ? CZString::DuplicationPolicy::NoDuplication
                 : CZString::DuplicationPolicy::DuplicateOnCopy);
    ObjectValues::iterator it = value_.mapVal->lower_bound(actualKey);

    if (it != value_.mapVal->end() && (*it).first == actualKey)
        return (*it).second;

    ObjectValues::value_type const defaultValue(actualKey, kNULL);
    it = value_.mapVal->insert(it, defaultValue);
    Value& value = (*it).second;
    return value;
}

/** Return the array element at `index`, or `defaultValue` if absent.
 *
 *  @param index        Zero-based array index.
 *  @param defaultValue Value to return when the index is out of range.
 *  @return Copy of the element or `defaultValue`.
 */
Value
Value::get(UInt index, Value const& defaultValue) const
{
    Value const* value = &((*this)[index]);
    return value == &kNULL ? defaultValue : *value;
}

/** Return true if `index` is within the current array bounds.
 *
 *  @param index Zero-based array index to test.
 *  @return True iff `index < size()`.
 */
bool
Value::isValidIndex(UInt index) const
{
    return index < size();
}

/** Look up a const object member; returns `kNULL` if the key is absent.
 *
 *  The key is compared without duplication (`NoDuplication` policy), so no
 *  heap allocation occurs.  Returns `Value::kNULL` for missing keys or when
 *  the `Value` is `nullValue`.
 *
 *  @param key Null-terminated member name.
 *  @return Const reference to the member or `kNULL`.
 *  @pre `type()` is `Null` or `Object` (asserted).
 */
Value const&
Value::operator[](char const* key) const
{
    XRPL_ASSERT(
        type_ == ValueType::Null || type_ == ValueType::Object,
        "json::Value::operator[](const char*) const : valid type");

    if (type_ == ValueType::Null)
        return kNULL;

    CZString const actualKey(key, CZString::DuplicationPolicy::NoDuplication);
    ObjectValues::const_iterator const it = value_.mapVal->find(actualKey);

    if (it == value_.mapVal->end())
        return kNULL;

    return (*it).second;
}

/** Access or create an object member by `std::string` key (non-const). */
Value&
Value::operator[](std::string const& key)
{
    return (*this)[key.c_str()];
}

/** Look up a const object member by `std::string` key; returns `kNULL` if absent. */
Value const&
Value::operator[](std::string const& key) const
{
    return (*this)[key.c_str()];
}

/** Access or create an object member using a `StaticString` key (non-const).
 *
 *  The key pointer is stored without duplication, avoiding heap allocation.
 *  Use this overload for frequently-accessed keys held in `static const
 *  StaticString` constants.
 */
Value&
Value::operator[](StaticString const& key)
{
    return resolveReference(key, true);
}

/** Look up a const object member by `StaticString` key; returns `kNULL` if absent. */
Value const&
Value::operator[](StaticString const& key) const
{
    return (*this)[key.cStr()];
}

/** Append a copy of `value` to the end of this array.
 *
 *  Equivalent to `(*this)[size()] = value`.
 *
 *  @param value Element to append.
 *  @return Reference to the newly inserted element.
 */
Value&
Value::append(Value const& value)
{
    return (*this)[size()] = value;
}

/** Append a moved `value` to the end of this array.
 *
 *  Equivalent to `(*this)[size()] = std::move(value)`.
 *
 *  @param value Element to move-append.
 *  @return Reference to the newly inserted element.
 */
Value&
Value::append(Value&& value)
{
    return (*this)[size()] = std::move(value);
}

/** Return the named object member, or `defaultValue` if the key is absent.
 *
 *  @param key          Null-terminated member name.
 *  @param defaultValue Fallback value when the key does not exist.
 *  @return Copy of the member or `defaultValue`.
 */
Value
Value::get(char const* key, Value const& defaultValue) const
{
    Value const* value = &((*this)[key]);
    return value == &kNULL ? defaultValue : *value;
}

/** Return the named object member, or `defaultValue` if the key is absent.
 *
 *  @param key          Member name.
 *  @param defaultValue Fallback value when the key does not exist.
 *  @return Copy of the member or `defaultValue`.
 */
Value
Value::get(std::string const& key, Value const& defaultValue) const
{
    return get(key.c_str(), defaultValue);
}

/** Remove and return the named member from this object.
 *
 *  @param key Null-terminated member name to remove.
 *  @return The removed value, or `kNULL` if the key was absent or the
 *      `Value` is `nullValue`.
 *  @pre `type()` is `Object` or `Null` (asserted).
 *  @post `type()` is unchanged; the member is no longer present.
 */
Value
Value::removeMember(char const* key)
{
    XRPL_ASSERT(
        type_ == ValueType::Null || type_ == ValueType::Object,
        "json::Value::removeMember : valid type");

    if (type_ == ValueType::Null)
        return kNULL;

    CZString const actualKey(key, CZString::DuplicationPolicy::NoDuplication);
    ObjectValues::iterator const it = value_.mapVal->find(actualKey);

    if (it == value_.mapVal->end())
        return kNULL;

    Value old(it->second);
    value_.mapVal->erase(it);
    return old;
}

/** Remove and return the named member from this object.
 *
 *  Delegates to `removeMember(char const*)`.
 *
 *  @param key Member name to remove.
 *  @return The removed value, or `kNULL` if absent.
 */
Value
Value::removeMember(std::string const& key)
{
    return removeMember(key.c_str());
}

/** Return true if this object contains a member with the given key.
 *
 *  Returns false for non-object types without asserting.
 *
 *  @param key Null-terminated member name to look up.
 *  @return True iff the key exists in this object.
 */
bool
Value::isMember(char const* key) const
{
    if (type_ != ValueType::Object)
        return false;

    Value const* value = &((*this)[key]);
    return value != &kNULL;
}

/** Return true if this object contains a member with the given key. */
bool
Value::isMember(std::string const& key) const
{
    return isMember(key.c_str());
}

/** Return true if this object contains a member with the given key. */
bool
Value::isMember(StaticString const& key) const
{
    return isMember(key.cStr());
}

/** Return a vector of all member names in this object.
 *
 *  @return `Members` (vector of strings) in map iteration order.  Returns an
 *      empty vector for `nullValue`.
 *  @pre `type()` is `Object` or `Null` (asserted).
 *  @post `type()` is unchanged.
 */
Value::Members
Value::getMemberNames() const
{
    XRPL_ASSERT(
        type_ == ValueType::Null || type_ == ValueType::Object,
        "json::Value::getMemberNames : valid type");

    if (type_ == ValueType::Null)
        return Value::Members();

    Members members;
    members.reserve(value_.mapVal->size());
    ObjectValues::const_iterator it = value_.mapVal->begin();
    ObjectValues::const_iterator const itEnd = value_.mapVal->end();

    for (; it != itEnd; ++it)
        members.emplace_back((*it).first.cStr());

    return members;
}

/** Return true if the value is `null`. */
bool
Value::isNull() const
{
    return type_ == ValueType::Null;
}

/** Return true if the value is a boolean. */
bool
Value::isBool() const
{
    return type_ == ValueType::Boolean;
}

/** Return true if the value is a signed integer. */
bool
Value::isInt() const
{
    return type_ == ValueType::Int;
}

/** Return true if the value is an unsigned integer. */
bool
Value::isUInt() const
{
    return type_ == ValueType::UInt;
}

/** Return true if the value is an integer type or boolean.
 *
 *  Booleans are included because they coerce losslessly to 0/1.
 */
bool
Value::isIntegral() const
{
    return type_ == ValueType::Int || type_ == ValueType::UInt || type_ == ValueType::Boolean;
}

/** Return true if the value is a double (`realValue`). */
bool
Value::isDouble() const
{
    return type_ == ValueType::Real;
}

/** Return true if the value is any numeric type (integral or double). */
bool
Value::isNumeric() const
{
    return isIntegral() || isDouble();
}

/** Return true if the value is a string. */
bool
Value::isString() const
{
    return type_ == ValueType::String;
}

/** Return true if the value is an array. */
bool
Value::isArray() const
{
    return type_ == ValueType::Array;
}

/** Return true if the value is an array or null. */
bool
Value::isArrayOrNull() const
{
    return type_ == ValueType::Null || type_ == ValueType::Array;
}

/** Return true if the value is an object. */
bool
Value::isObject() const
{
    return type_ == ValueType::Object;
}

/** Return true if the value is an object or null. */
bool
Value::isObjectOrNull() const
{
    return type_ == ValueType::Null || type_ == ValueType::Object;
}

/** Serialise this `Value` to a human-readable, indented JSON string.
 *
 *  Delegates to `StyledWriter::write()`.  Suitable for logging and debugging;
 *  not intended for wire-format serialisation where compact output is preferred.
 *
 *  @return Pretty-printed JSON string.
 */
std::string
Value::toStyledString() const
{
    StyledWriter writer;
    return writer.write(*this);
}

/** Return a const iterator to the first element of an array or object.
 *
 *  Returns a default-constructed (null) iterator for any other value type,
 *  which compares equal to `end()` so range-based loops are safe.
 */
Value::const_iterator
Value::begin() const
{
    switch (type_)
    {
        case ValueType::Array:
        case ValueType::Object:
            if (value_.mapVal != nullptr)
                return const_iterator(value_.mapVal->begin());

            break;
        default:
            break;
    }

    return const_iterator();
}

/** Return a const past-the-end iterator for an array or object.
 *
 *  Returns a default-constructed (null) iterator for other value types.
 */
Value::const_iterator
Value::end() const
{
    switch (type_)
    {
        case ValueType::Array:
        case ValueType::Object:
            if (value_.mapVal != nullptr)
                return const_iterator(value_.mapVal->end());

            break;
        default:
            break;
    }

    return const_iterator();
}

/** Return a mutable iterator to the first element of an array or object.
 *
 *  Returns a default-constructed (null) iterator for other value types.
 */
Value::iterator
Value::begin()
{
    switch (type_)
    {
        case ValueType::Array:
        case ValueType::Object:
            if (value_.mapVal != nullptr)
                return iterator(value_.mapVal->begin());
            break;
        default:
            break;
    }

    return iterator();
}

/** Return a mutable past-the-end iterator for an array or object.
 *
 *  Returns a default-constructed (null) iterator for other value types.
 */
Value::iterator
Value::end()
{
    switch (type_)
    {
        case ValueType::Array:
        case ValueType::Object:
            if (value_.mapVal != nullptr)
                return iterator(value_.mapVal->end());
            break;
        default:
            break;
    }

    return iterator();
}

}  // namespace json
