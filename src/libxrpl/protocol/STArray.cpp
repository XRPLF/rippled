/** @file
 *  Implements STArray — the protocol's typed container for sequences of STObject
 *  instances, used for fields like sfMemos, sfSigners, and sfNFTokens.
 *
 *  Non-trivial method bodies live here; inline accessors and iterator plumbing
 *  are defined in STArray.h.
 */

#include <xrpl/protocol/STArray.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

/** Move constructor.
 *
 *  Explicitly copies the field name from @p other before moving the element
 *  vector, because STBase stores the field-name pointer separately from the
 *  data. Without the explicit `setFName` call the compiler-generated move
 *  would leave the field name association in an unspecified state, causing
 *  field-ID mismatches during serialization.
 *
 *  @param other The STArray to move from; left in a valid but unspecified state.
 */
STArray::STArray(STArray&& other) : STBase(other.getFName()), v_(std::move(other.v_))
{
}

/** Move assignment operator.
 *
 *  Same field-name transfer requirement as the move constructor: the field
 *  name must be copied explicitly because STBase does not participate in the
 *  compiler-generated move-assignment.
 *
 *  @param other The STArray to move from; left in a valid but unspecified state.
 *  @return *this
 */
STArray&
STArray::operator=(STArray&& other)
{
    setFName(other.getFName());
    v_ = std::move(other.v_);
    return *this;
}

/** Construct an anonymous STArray with pre-allocated capacity.
 *
 *  Creates an array with no associated SField but with storage reserved for
 *  @p n elements, avoiding early reallocations when the size is known up front.
 *
 *  @param n Number of elements to reserve space for.
 */
STArray::STArray(int n)
{
    v_.reserve(n);
}

/** Construct an empty STArray bound to the given field.
 *
 *  @param f The SField that names this array in its parent object.
 */
STArray::STArray(SField const& f) : STBase(f)
{
}

/** Construct an STArray bound to a field with pre-allocated capacity.
 *
 *  @param f The SField that names this array in its parent object.
 *  @param n Number of elements to reserve space for.
 */
STArray::STArray(SField const& f, std::size_t n) : STBase(f)
{
    v_.reserve(n);
}

/** Deserializing constructor — decodes a sentinel-terminated sequence of
 *  inner objects from a binary stream.
 *
 *  The XRPL binary format does not prefix arrays with a length; instead the
 *  decoder loops over field-ID tokens until it sees the canonical end-of-array
 *  marker (`STI_ARRAY, field == 1`). Each element is validated and constructed
 *  in place before the next token is read.
 *
 *  Four checks guard each loop iteration:
 *  - `(STI_ARRAY, 1)` — clean end of array; break.
 *  - `(STI_OBJECT, 1)` — misplaced end-of-object marker; the stream is
 *      structurally corrupt. Throws `std::runtime_error("Illegal terminator
 *      in array")`.
 *  - `fn.isInvalid()` — unrecognized `(type, field)` pair. Throws
 *      `std::runtime_error("Unknown field")`.
 *  - `fn.fieldType != STI_OBJECT` — every STArray element must be an
 *      STObject. Throws `std::runtime_error("Non-object in array")`.
 *
 *  After successful construction, `applyTemplateFromSField(fn)` validates the
 *  new element against the schema registered for that inner-object field type
 *  (e.g. sfMemo, sfSigner). This call may also throw; any exception unwinds
 *  the partially built array entirely — there is no partial-recovery path.
 *
 *  @param sit   Forward cursor over the binary payload. Advanced in place; the
 *      caller retains ownership.
 *  @param f     The SField naming this array in its parent object.
 *  @param depth Current nesting depth, threaded from the parent STObject. Each
 *      child STObject is constructed with `depth + 1`; STObject's own
 *      constructor enforces a maximum depth of 10 to prevent stack exhaustion
 *      from crafted payloads.
 *  @throws std::runtime_error on any structural violation in the stream.
 */
STArray::STArray(SerialIter& sit, SField const& f, int depth) : STBase(f)
{
    while (!sit.empty())
    {
        int type = 0, field = 0;
        sit.getFieldID(type, field);

        if ((type == STI_ARRAY) && (field == 1))
            break;

        if ((type == STI_OBJECT) && (field == 1))
        {
            JLOG(debugLog().error()) << "Encountered array with end of object marker";
            Throw<std::runtime_error>("Illegal terminator in array");
        }

        auto const& fn = SField::getField(type, field);

        if (fn.isInvalid())
        {
            JLOG(debugLog().error()) << "Unknown field: " << type << "/" << field;
            Throw<std::runtime_error>("Unknown field");
        }

        if (fn.fieldType != STI_OBJECT)
        {
            JLOG(debugLog().error()) << "Array contains non-object";
            Throw<std::runtime_error>("Non-object in array");
        }

        v_.emplace_back(sit, fn, depth + 1);

        v_.back().applyTemplateFromSField(fn);  // May throw
    }
}

/** Copy this object into a caller-supplied buffer using placement new.
 *
 *  Delegates to `STBase::emplace()`, which places the copy into @p buf when
 *  it fits within @p n bytes, or heap-allocates otherwise. This supports the
 *  small-object optimization in `detail::STVar`.
 *
 *  @param n   Size of @p buf in bytes.
 *  @param buf Target buffer for placement new; must be suitably aligned.
 *  @return Pointer to the newly constructed copy (either @p buf or heap).
 */
STBase*
STArray::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

/** Move this object into a caller-supplied buffer using placement new.
 *
 *  Delegates to `STBase::emplace()`. If this array fits within @p n bytes it
 *  is move-constructed into @p buf; otherwise it is move-constructed on the
 *  heap.
 *
 *  @param n   Size of @p buf in bytes.
 *  @param buf Target buffer for placement new; must be suitably aligned.
 *  @return Pointer to the newly constructed object (either @p buf or heap).
 */
STBase*
STArray::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

/** Return a bracket-delimited, comma-separated string including field names.
 *
 *  Each element is rendered via `STObject::getFullText()`, which includes
 *  field-name prefixes. Intended for debugging and logging.
 *
 *  @return Human-readable representation of the array, e.g. `[fieldA = ...,
 *      fieldB = ...]`.
 */
std::string
STArray::getFullText() const
{
    std::string r = "[";

    bool first = true;
    for (auto const& obj : v_)
    {
        if (!first)
            r += ",";

        r += obj.getFullText();
        first = false;
    }

    r += "]";
    return r;
}

/** Return a bracket-delimited, comma-separated string of element values only.
 *
 *  Each element is rendered via `STObject::getText()`, which omits field-name
 *  prefixes. Intended for debugging.
 *
 *  @return Human-readable value-only representation of the array.
 */
std::string
STArray::getText() const
{
    std::string r = "[";

    bool first = true;
    for (STObject const& o : v_)
    {
        if (!first)
            r += ",";

        r += o.getText();
        first = false;
    }

    r += "]";
    return r;
}

/** Serialize this array to a JSON array value.
 *
 *  Each element that is not `STI_NOTPRESENT` is appended as a JSON object with
 *  a single key — the element's field name — mapping to the element's own JSON
 *  representation:
 *
 *  @code
 *  [ { "Memo": { "MemoData": "..." } }, ... ]
 *  @endcode
 *
 *  This outer-object wrapping preserves the named-field context of each inner
 *  object for round-trip fidelity with the XRPL JSON API. Elements with type
 *  `STI_NOTPRESENT` (absent optional fields in a template-bound context) are
 *  silently skipped.
 *
 *  @param p  JSON rendering options forwarded to each element.
 *  @return   A `json::Value` of array type.
 */
json::Value
STArray::getJson(JsonOptions p) const
{
    json::Value v = json::ValueType::Array;
    for (auto const& object : v_)
    {
        if (object.getSType() != STI_NOTPRESENT)
        {
            json::Value& inner = v.append(json::ValueType::Object);
            inner[object.getFName().getJsonName()] = object.getJson(p);
        }
    }
    return v;
}

/** Append the binary encoding of every element to @p s.
 *
 *  For each element the encoding is: field ID, element content, per-element
 *  object terminator (`STI_OBJECT, 1`). The outer array's own field ID and the
 *  end-of-array terminator (`STI_ARRAY, 1`) are written by the caller
 *  (`STObject::add()`), not here. This split is consistent across all
 *  `STBase` subclasses.
 *
 *  @param s The serializer to append to.
 */
void
STArray::add(Serializer& s) const
{
    for (STObject const& object : v_)
    {
        object.addFieldID(s);
        object.add(s);
        s.addFieldID(STI_OBJECT, 1);
    }
}

/** @return `STI_ARRAY` — the serialized type ID for this class. */
SerializedTypeID
STArray::getSType() const
{
    return STI_ARRAY;
}

/** Test deep equality with another STBase.
 *
 *  Performs a `dynamic_cast` to confirm @p t is also an STArray, then
 *  delegates to vector equality, which cascades through `STObject::operator==`.
 *
 *  @param t The object to compare against.
 *  @return `true` if @p t is an STArray whose elements are pairwise equal
 *      to this array's elements.
 */
bool
STArray::isEquivalent(STBase const& t) const
{
    auto v = dynamic_cast<STArray const*>(&t);
    return v != nullptr && v_ == v->v_;
}

/** @return `true` when the array is empty — an empty array need not be
 *  encoded on the wire.
 */
bool
STArray::isDefault() const
{
    return v_.empty();
}

/** Sort elements in place using a caller-supplied comparator.
 *
 *  Used to impose canonical ordering before serialization. Notable callers:
 *  - `TxMeta::addRaw()` — sorts `AffectedNodes` by `sfLedgerIndex` before
 *    writing metadata; deviation from canonical order is a consensus-fork risk.
 *  - `NFTokenHelpers` — sorts `sfNFTokens` entries by `sfNFTokenID` when
 *    managing NFT pages.
 *
 *  @note Multi-sign transactions require `sfSigners` to be sorted in ascending
 *      `AccountID` order before submission. That sorting is expected to be
 *      performed by the signing client, not by this method.
 *
 *  @param compare Function pointer returning `true` when the first argument
 *      should precede the second. Must satisfy strict-weak-ordering.
 */
void
STArray::sort(bool (*compare)(STObject const&, STObject const&))
{
    std::ranges::sort(v_, compare);
}

}  // namespace xrpl
