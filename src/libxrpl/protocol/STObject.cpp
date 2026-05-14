/** @file STObject.cpp
 *  Implementation of STObject — the heterogeneous, field-keyed container that
 *  underlies every XRPL transaction, ledger entry, and inner object.
 *
 *  Fields are stored in `v_` (a `std::vector<detail::STVar>`) either in
 *  insertion order (free mode, `type_ == nullptr`) or in template order
 *  (templated mode, `type_` points to an `SOTemplate`).  Templated mode
 *  provides O(1) field lookup and guarantees every slot is pre-populated with
 *  a sentinel (`STI_NOTPRESENT`) for optional/default fields.
 */

#include <xrpl/protocol/STObject.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/InnerObjectFormats.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
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
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/detail/STVar.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

/** Move constructor.  Transfers field storage and template pointer from
 *  `other`, leaving it in a valid but empty state.
 */
STObject::STObject(STObject&& other)
    : STBase(other.getFName()), v_(std::move(other.v_)), type_(other.type_)
{
}

/** Construct a free (schema-less) STObject with the given field name. */
STObject::STObject(SField const& name) : STBase(name)
{
}

/** Construct a templated STObject from a schema and field name.
 *
 *  Calls `set(type)` to pre-populate every slot defined by `type`.
 *
 *  @param type  The SOTemplate that defines the layout of this object.
 *  @param name  The SField identifying this object within its parent.
 */
STObject::STObject(SOTemplate const& type, SField const& name) : STBase(name)
{
    set(type);
}

/** Construct a templated STObject by deserializing from a byte stream.
 *
 *  Deserializes fields from `sit` in free mode, then calls `applyTemplate`
 *  to re-order and validate them against `type`.
 *
 *  @param type  The SOTemplate to enforce after deserialization.
 *  @param sit   The byte stream to read from.
 *  @param name  The SField identifying this object within its parent.
 *  @throws FieldErr if a required field is missing or an unexpected field
 *      is present.
 */
STObject::STObject(SOTemplate const& type, SerialIter& sit, SField const& name) : STBase(name)
{
    v_.reserve(type.size());
    set(sit);
    applyTemplate(type);  // May throw
}

/** Construct an STObject by deserializing from a byte stream with a depth guard.
 *
 *  The `depth` parameter prevents stack exhaustion when deserializing
 *  deeply nested `STObject`/`STArray` structures from untrusted input.
 *  Recursion beyond depth 10 throws `std::runtime_error`.
 *
 *  @param sit    The byte stream to read from.
 *  @param name   The SField identifying this object within its parent.
 *  @param depth  Current nesting depth; capped at 10.
 *  @throws std::runtime_error if `depth > 10` or if malformed data is
 *      encountered (e.g., duplicate fields, unknown field IDs).
 */
STObject::STObject(SerialIter& sit, SField const& name, int depth) noexcept(false) : STBase(name)
{
    if (depth > 10)
        Throw<std::runtime_error>("Maximum nesting depth of STObject exceeded");
    set(sit, depth);
}

/** Factory for inner objects that conditionally applies a schema template.
 *
 *  Template application was introduced in two amendment phases to preserve
 *  replay compatibility with historical ledger entries serialized before
 *  schemas existed:
 *  - When no `Rules` are available (pre-consensus or unit-test context),
 *    the template is always applied.
 *  - `fixInnerObjTemplate` added templates to AMM inner objects
 *    (`sfAuctionSlot`, `sfVoteEntry`).
 *  - `fixInnerObjTemplate2` extended templates to all other inner objects.
 *
 *  This method reads the ambient transaction rules via
 *  `getCurrentTransactionRules()`, making it implicitly context-sensitive.
 *
 *  @param name  The SField identifying the inner object type.
 *  @return A new STObject, bound to its SOTemplate when the active rules
 *      permit it.
 */
STObject
STObject::makeInnerObject(SField const& name)
{
    STObject obj{name};

    std::optional<Rules> const& rules = getCurrentTransactionRules();
    bool const isAMMObj = name == sfAuctionSlot || name == sfVoteEntry;
    if (!rules || (rules->enabled(fixInnerObjTemplate) && isAMMObj) ||
        (rules->enabled(fixInnerObjTemplate2) && !isAMMObj))
    {
        if (SOTemplate const* elements =
                InnerObjectFormats::getInstance().findSOTemplateBySField(name))
            obj.set(*elements);
    }
    return obj;
}

/** Copy this object into `buf` using placement-new via `STBase::emplace`. */
STBase*
STObject::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

/** Move this object into `buf` using placement-new via `STBase::emplace`. */
STBase*
STObject::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

/** Returns `STI_OBJECT`, the serialized type identifier for this class. */
SerializedTypeID
STObject::getSType() const
{
    return STI_OBJECT;
}

/** Returns `true` when the field storage vector is empty.
 *
 *  An STObject is considered default (absent) only when it holds no fields
 *  at all.  A templated object initialized via `set(SOTemplate)` is never
 *  empty because every slot is pre-populated.
 */
bool
STObject::isDefault() const
{
    return v_.empty();
}

/** Serialize all fields (including signing fields) into `s`. */
void
STObject::add(Serializer& s) const
{
    add(s, WhichFields::WithAllFields);  // just inner elements
}

/** Move-assign from `other`, transferring field storage and template pointer. */
STObject&
STObject::operator=(STObject&& other)
{
    setFName(other.getFName());
    type_ = other.type_;
    v_ = std::move(other.v_);
    return *this;
}

/** Initialize this object from a template, pre-populating every slot.
 *
 *  Clears `v_` and rebuilds it in template order.  `soeREQUIRED` fields
 *  receive a type-correct default value; all other fields (`soeOPTIONAL`,
 *  `soeDEFAULT`) receive the `STI_NOTPRESENT` sentinel, which
 *  `isFieldPresent()` recognizes as absent.
 *
 *  @param type  The SOTemplate that defines the layout of this object.
 */
void
STObject::set(SOTemplate const& type)
{
    v_.clear();
    v_.reserve(type.size());
    type_ = &type;

    for (auto const& elem : type)
    {
        if (elem.style() != SoeRequired)
        {
            v_.emplace_back(detail::gNonPresentObject, elem.sField());
        }
        else
        {
            v_.emplace_back(detail::gDefaultObject, elem.sField());
        }
    }
}

/** Validate and reorder fields against a schema after free-mode deserialization.
 *
 *  Rebuilds `v_` in template order:
 *  - Each template slot is filled from the existing (insertion-ordered) `v_`,
 *    or populated with `STI_NOTPRESENT` when the field is absent.
 *  - A `soeDEFAULT` field whose serialized value equals the type's default
 *    is rejected — the ledger forbids explicitly encoding default values.
 *  - A `soeREQUIRED` field that is missing throws `FieldErr`.
 *  - Any fields left over in `v_` after template matching must be discardable
 *    (`SField::isDiscardable()`); non-discardable unknown fields throw.
 *
 *  The final `v_.swap(v)` atomically replaces the old unordered data.
 *
 *  @param type  The SOTemplate to enforce.
 *  @throws FieldErr on required-missing, explicit-default, or unknown
 *      non-discardable field.
 */
void
STObject::applyTemplate(SOTemplate const& type)
{
    auto throwFieldErr = [](std::string const& field, char const* description) {
        std::stringstream ss;
        ss << "Field '" << field << "' " << description;
        std::string const text{ss.str()};
        JLOG(debugLog().error()) << "STObject::applyTemplate failed: " << text;
        Throw<FieldErr>(text);
    };

    type_ = &type;
    decltype(v_) v;
    v.reserve(type.size());
    for (auto const& e : type)
    {
        auto const iter = std::ranges::find_if(
            v_, [&](detail::STVar const& b) { return b.get().getFName() == e.sField(); });
        if (iter != v_.end())
        {
            if ((e.style() == SoeDefault) && iter->get().isDefault())
            {
                throwFieldErr(e.sField().fieldName, "may not be explicitly set to default.");
            }
            v.emplace_back(std::move(*iter));
            v_.erase(iter);
        }
        else
        {
            if (e.style() == SoeRequired)
            {
                throwFieldErr(e.sField().fieldName, "is required but missing.");
            }
            v.emplace_back(detail::gNonPresentObject, e.sField());
        }
    }
    for (auto const& e : v_)
    {
        if (!e->getFName().isDiscardable())
        {
            throwFieldErr(e->getFName().getName(), "found in disallowed location.");
        }
    }
    v_.swap(v);
}

/** Apply the schema registered for `sField` in `InnerObjectFormats`, if any.
 *
 *  Looks up the global `InnerObjectFormats` singleton for a template keyed by
 *  `sField`.  When found, delegates to `applyTemplate`.  No-op if the field
 *  has no registered template (e.g., for inner objects without a known schema).
 *
 *  @param sField  The SField whose registered SOTemplate should be applied.
 *  @throws FieldErr (from `applyTemplate`) if the object does not conform.
 */
void
STObject::applyTemplateFromSField(SField const& sField)
{
    SOTemplate const* elements = InnerObjectFormats::getInstance().findSOTemplateBySField(sField);
    if (elements != nullptr)
        applyTemplate(*elements);  // May throw
}

/** Deserialize fields from a byte stream into this object (free mode).
 *
 *  Reads `(type, field)` ID pairs from `sit`, looks up each `SField`, and
 *  constructs a child `STVar` at `depth+1`.  When a child is itself an
 *  `STObject`, `applyTemplateFromSField()` is called immediately to bind it
 *  to any known schema.
 *
 *  Termination rules:
 *  - `STI_OBJECT / field==1` — end-of-object marker; consumed and returns
 *    `true`.
 *  - `STI_ARRAY / field==1` inside an object — malformed data; throws.
 *  - End of `sit` without a marker — returns `false` (top-level object).
 *
 *  After all fields are read, the method enforces the no-duplicate-field
 *  invariant using `getSortedFields` + `std::adjacent_find`; duplicate fields
 *  throw `std::runtime_error("Duplicate field detected")`.
 *
 *  @param sit    The byte stream to read from.
 *  @param depth  Current nesting depth (caller increments before passing).
 *  @return `true` if the object was terminated by an end-of-object marker,
 *      `false` if `sit` was exhausted without one.
 *  @throws std::runtime_error on unknown field ID, embedded end-of-array
 *      marker, or duplicate field.
 */
bool
STObject::set(SerialIter& sit, int depth)
{
    bool reachedEndOfObject = false;

    v_.clear();

    while (!sit.empty())
    {
        int type = 0;
        int field = 0;

        sit.getFieldID(type, field);

        if (type == STI_OBJECT && field == 1)
        {
            reachedEndOfObject = true;
            break;
        }

        if (type == STI_ARRAY && field == 1)
        {
            JLOG(debugLog().error()) << "Encountered object with embedded end-of-array marker";
            Throw<std::runtime_error>("Illegal end-of-array marker in object");
        }

        auto const& fn = SField::getField(type, field);

        if (fn.isInvalid())
        {
            JLOG(debugLog().error())
                << "Unknown field: field_type=" << type << ", field_name=" << field;
            Throw<std::runtime_error>("Unknown field");
        }

        v_.emplace_back(sit, fn, depth + 1);

        if (auto const obj = dynamic_cast<STObject*>(&(v_.back().get())))
            obj->applyTemplateFromSField(fn);  // May throw
    }

    auto const sf = getSortedFields(*this, WhichFields::WithAllFields);

    auto const dup = std::ranges::adjacent_find(sf, [](STBase const* lhs, STBase const* rhs) {
        return lhs->getFName() == rhs->getFName();
    });

    if (dup != sf.cend())
        Throw<std::runtime_error>("Duplicate field detected");

    return reachedEndOfObject;
}

/** Returns `true` if this object contains a field equal to `t`.
 *
 *  Looks up the field by name and compares via `STBase::operator==`.
 *
 *  @param t  The field to search for, identified by its `SField` name.
 *  @return `true` if a field with the same name and value exists.
 */
bool
STObject::hasMatchingEntry(STBase const& t) const
{
    STBase const* o = peekAtPField(t.getFName());

    if (o == nullptr)
        return false;

    return t == *o;
}

/** Returns a human-readable representation including the field name.
 *
 *  Produces `"<name> = { <field1>, <field2>, ... }"`, or `"{ ... }"` when
 *  the object has no name.  `STI_NOTPRESENT` slots are skipped.
 */
std::string
STObject::getFullText() const
{
    std::string ret;
    bool first = true;

    if (getFName().hasName())
    {
        ret = getFName().getName();
        ret += " = {";
    }
    else
    {
        ret = "{";
    }

    for (auto const& elem : v_)
    {
        if (elem->getSType() != STI_NOTPRESENT)
        {
            if (!first)
            {
                ret += ", ";
            }
            else
            {
                first = false;
            }

            ret += elem->getFullText();
        }
    }

    ret += "}";
    return ret;
}

/** Returns a terse `"{ ... }"` representation of all fields (including absent
 *  sentinel slots).  Primarily used for diagnostic logging.
 */
std::string
STObject::getText() const
{
    std::string ret = "{";
    bool first = false;
    for (auto const& elem : v_)
    {
        if (!first)
        {
            ret += ", ";
            first = false;
        }

        ret += elem->getText();
    }
    ret += "}";
    return ret;
}

/** Compare two STObjects for structural and value equivalence.
 *
 *  Fast path: when both objects share the same `SOTemplate` pointer
 *  (`type_`), fields are compared positionally in `v_` order, which is
 *  O(n).  This is sound because the same template guarantees identical slot
 *  layout.
 *
 *  Slow path: when templates differ (or either is a free object), both
 *  objects are sorted by `fieldCode` and compared element-by-element.
 *
 *  @param t  The object to compare against; must also be an `STObject`.
 *  @return `true` if all present fields match in type and value.
 */
bool
STObject::isEquivalent(STBase const& t) const
{
    STObject const* v = dynamic_cast<STObject const*>(&t);

    if (v == nullptr)
        return false;

    if (type_ != nullptr && v->type_ == type_)
    {
        return std::ranges::equal(
            begin(), end(), v->begin(), v->end(), [](STBase const& st1, STBase const& st2) {
                return (st1.getSType() == st2.getSType()) && st1.isEquivalent(st2);
            });
    }

    auto const sf1 = getSortedFields(*this, WhichFields::WithAllFields);
    auto const sf2 = getSortedFields(*v, WhichFields::WithAllFields);

    return std::ranges::equal(sf1, sf2, [](STBase const* st1, STBase const* st2) {
        return (st1->getSType() == st2->getSType()) && st1->isEquivalent(*st2);
    });
}

/** Compute a domain-separated SHA-512 half-hash over all fields.
 *
 *  Prepends `prefix` (a 4-byte `HashPrefix` constant) to the serialization to
 *  prevent cross-domain hash collisions, then returns the first 256 bits of
 *  SHA-512.
 *
 *  @param prefix  The `HashPrefix` tag identifying the hash domain (e.g.,
 *      `HashPrefix::transactionID`, `HashPrefix::leafNode`).
 *  @return The 256-bit digest of `prefix ‖ canonical_serialization`.
 */
uint256
STObject::getHash(HashPrefix prefix) const
{
    Serializer s;
    s.add32(prefix);
    add(s, WhichFields::WithAllFields);
    return s.getSHA512Half();
}

/** Compute a domain-separated hash over signing fields only.
 *
 *  Identical to `getHash` but excludes fields where `SField::shouldInclude`
 *  returns `false` for signing (e.g., `sfTxnSignature`, `sfSigners`).  This
 *  is the digest that a private key signs and a verifier reconstructs.
 *
 *  @param prefix  The `HashPrefix` tag for this signing context (e.g.,
 *      `HashPrefix::txSign`, `HashPrefix::txMultiSign`).
 *  @return The 256-bit signing digest.
 */
uint256
STObject::getSigningHash(HashPrefix prefix) const
{
    Serializer s;
    s.add32(prefix);
    add(s, WhichFields::OmitSigningFields);
    return s.getSHA512Half();
}

/** Return the index of `field` within `v_`, or -1 if not found.
 *
 *  In templated mode delegates to `SOTemplate::getIndex()`, which is O(1).
 *  In free mode performs a linear scan through `v_`.
 *
 *  @param field  The SField to locate.
 *  @return Zero-based index into `v_`, or -1 when absent.
 */
int
STObject::getFieldIndex(SField const& field) const
{
    if (type_ != nullptr)
        return type_->getIndex(field);

    int i = 0;
    for (auto const& elem : v_)
    {
        if (elem->getFName() == field)
            return i;
        ++i;
    }
    return -1;
}

/** Return a const reference to `field`, throwing if absent.
 *
 *  @param field  The SField to retrieve.
 *  @throws std::runtime_error ("Field not found") if the field is not present.
 */
STBase const&
STObject::peekAtField(SField const& field) const
{
    int const index = getFieldIndex(field);

    if (index == -1)
        throwFieldNotFound(field);

    return peekAtIndex(index);
}

/** Return a mutable reference to `field`, throwing if absent.
 *
 *  @param field  The SField to retrieve.
 *  @throws std::runtime_error ("Field not found") if the field is not present.
 */
STBase&
STObject::getField(SField const& field)
{
    int const index = getFieldIndex(field);

    if (index == -1)
        throwFieldNotFound(field);

    return getIndex(index);
}

/** Return the `SField` associated with the slot at `index`.
 *
 *  @param index  Zero-based offset into `v_`.
 */
SField const&
STObject::getFieldSType(int index) const
{
    return v_[index]->getFName();
}

/** Return a const pointer to `field`, or `nullptr` if absent.
 *
 *  Callers that need a non-throwing presence check should prefer this over
 *  `peekAtField`.
 *
 *  @param field  The SField to look up.
 *  @return Pointer to the field's `STBase`, or `nullptr`.
 */
STBase const*
STObject::peekAtPField(SField const& field) const
{
    int const index = getFieldIndex(field);

    if (index == -1)
        return nullptr;

    return peekAtPIndex(index);
}

/** Return a mutable pointer to `field`, optionally creating it in free mode.
 *
 *  In templated mode the field must already exist; creation is never
 *  performed.  In free mode, when `createOkay` is `true` and the field is
 *  absent, a new slot initialized to the default value is appended.
 *
 *  @param field      The SField to look up or create.
 *  @param createOkay When `true`, auto-creates the field in free objects.
 *  @return Mutable pointer to the field's `STBase`, or `nullptr` if absent
 *      and not created.
 */
STBase*
STObject::getPField(SField const& field, bool createOkay)
{
    int const index = getFieldIndex(field);

    if (index == -1)
    {
        if (createOkay && isFree())
            return getPIndex(emplaceBack(detail::gDefaultObject, field));

        return nullptr;
    }

    return getPIndex(index);
}

/** Returns `true` if `field` is present and not the `STI_NOTPRESENT` sentinel.
 *
 *  In templated mode, optional and default slots always exist in `v_` but
 *  carry the sentinel when logically absent.  This method distinguishes the
 *  two cases correctly.
 *
 *  @param field  The SField to test.
 */
bool
STObject::isFieldPresent(SField const& field) const
{
    int const index = getFieldIndex(field);

    if (index == -1)
        return false;

    return peekAtIndex(index).getSType() != STI_NOTPRESENT;
}

/** Return a mutable reference to the inner `STObject` at `field`.
 *
 *  @param field  Must identify an `STObject`-typed field.
 *  @throws std::runtime_error if absent or wrong type.
 */
STObject&
STObject::peekFieldObject(SField const& field)
{
    return peekField<STObject>(field);
}

/** Return a mutable reference to the inner `STArray` at `field`.
 *
 *  @param field  Must identify an `STArray`-typed field.
 *  @throws std::runtime_error if absent or wrong type.
 */
STArray&
STObject::peekFieldArray(SField const& field)
{
    return peekField<STArray>(field);
}

/** Set one or more bits in `sfFlags`, creating the field in free objects.
 *
 *  Uses `getPField(sfFlags, createOkay=true)` so that free objects have the
 *  field created on demand.  Templated objects must already have `sfFlags` in
 *  their schema.
 *
 *  @param f  Bitmask of flags to set.
 *  @return `true` if the flags were set; `false` if `sfFlags` could not be
 *      found or created.
 */
bool
STObject::setFlag(std::uint32_t f)
{
    STUInt32* t = dynamic_cast<STUInt32*>(getPField(sfFlags, true));

    if (t == nullptr)
        return false;

    t->setValue(t->value() | f);
    return true;
}

/** Clear one or more bits in `sfFlags`.
 *
 *  Unlike `setFlag`, this method never creates the field — if `sfFlags` is
 *  absent the call is a no-op.
 *
 *  @param f  Bitmask of flags to clear.
 *  @return `true` if the flags were cleared; `false` if `sfFlags` is absent.
 */
bool
STObject::clearFlag(std::uint32_t f)
{
    STUInt32* t = dynamic_cast<STUInt32*>(getPField(sfFlags));

    if (t == nullptr)
        return false;

    t->setValue(t->value() & ~f);
    return true;
}

/** Returns `true` if all bits in `f` are set in `sfFlags`.
 *
 *  @param f  Bitmask to test.
 */
bool
STObject::isFlag(std::uint32_t f) const
{
    return (getFlags() & f) == f;
}

/** Return the raw value of `sfFlags`, or 0 if the field is absent. */
std::uint32_t
STObject::getFlags(void) const
{
    STUInt32 const* t = dynamic_cast<STUInt32 const*>(peekAtPField(sfFlags));

    if (t == nullptr)
        return 0;

    return t->value();
}

/** Transition a field from the `STI_NOTPRESENT` sentinel to a default-value
 *  instance, making it logically present.
 *
 *  For templated objects the slot already exists; its `STVar` is replaced with
 *  a default-constructed instance of the appropriate ST type.  For free
 *  objects the field is appended as a non-present sentinel (the sentinel is
 *  then replaced on first `set` or `setField*` call).
 *
 *  @param field  The SField to make present.
 *  @return Pointer to the now-present field slot.
 *  @throws std::runtime_error if the field does not exist in a templated
 *      object.
 */
STBase*
STObject::makeFieldPresent(SField const& field)
{
    int const index = getFieldIndex(field);

    if (index == -1)
    {
        if (!isFree())
            throwFieldNotFound(field);

        return getPIndex(emplaceBack(detail::gNonPresentObject, field));
    }

    STBase* f = getPIndex(index);  // NOLINT(misc-const-correctness)

    if (f->getSType() != STI_NOTPRESENT)
        return f;

    v_[index] = detail::STVar(detail::gDefaultObject, f->getFName());
    return getPIndex(index);
}

/** Transition a present field back to the `STI_NOTPRESENT` sentinel.
 *
 *  Only meaningful in templated mode where optional/default slots keep their
 *  position in `v_`.  If the field is already absent this is a no-op.
 *
 *  @param field  The SField to make absent.
 *  @throws std::runtime_error if `field` is not in the object at all.
 */
void
STObject::makeFieldAbsent(SField const& field)
{
    int const index = getFieldIndex(field);

    if (index == -1)
        throwFieldNotFound(field);

    STBase const& f = peekAtIndex(index);

    if (f.getSType() == STI_NOTPRESENT)
        return;
    v_[index] = detail::STVar(detail::gNonPresentObject, f.getFName());
}

/** Physically erase `field` from `v_` by SField.
 *
 *  This permanently removes the slot (shifts subsequent indices).  In
 *  templated mode this invalidates the positional index assumptions; it is
 *  intended only for free-mode objects or special-case cleanup.
 *
 *  @param field  The SField to remove.
 *  @return `true` if the field was found and removed; `false` if absent.
 */
bool
STObject::delField(SField const& field)
{
    int const index = getFieldIndex(field);

    if (index == -1)
        return false;

    delField(index);
    return true;
}

/** Physically erase the slot at `index` from `v_`.
 *
 *  @param index  Zero-based index into `v_`.
 */
void
STObject::delField(int index)
{
    v_.erase(v_.begin() + index);
}

/** Return the `SOEStyle` of `field` as declared in the active template.
 *
 *  Returns `SoeInvalid` when the object is in free mode (no template).
 *
 *  @param field  The SField to query.
 */
SOEStyle
STObject::getStyle(SField const& field) const
{
    return (type_ != nullptr) ? type_->style(field) : SoeInvalid;
}

// --- Typed field getters ---
// All getters delegate to getFieldByValue<T> (returns by value, throws if the
// field is absent and not optional) or getFieldByConstRef<T> (returns a const
// reference to a function-local static empty value when the field is absent,
// allowing safe access to optional fields without a prior isFieldPresent check).

/** @return Value of `field` as `unsigned char`.
 *  @throws std::runtime_error if absent or wrong type.
 */
unsigned char
STObject::getFieldU8(SField const& field) const
{
    return getFieldByValue<STUInt8>(field);
}

/** @return Value of `field` as `uint16_t`.
 *  @throws std::runtime_error if absent or wrong type.
 */
std::uint16_t
STObject::getFieldU16(SField const& field) const
{
    return getFieldByValue<STUInt16>(field);
}

/** @return Value of `field` as `uint32_t`.
 *  @throws std::runtime_error if absent or wrong type.
 */
std::uint32_t
STObject::getFieldU32(SField const& field) const
{
    return getFieldByValue<STUInt32>(field);
}

/** @return Value of `field` as `uint64_t`.
 *  @throws std::runtime_error if absent or wrong type.
 */
std::uint64_t
STObject::getFieldU64(SField const& field) const
{
    return getFieldByValue<STUInt64>(field);
}

/** @return Value of `field` as `uint128`.
 *  @throws std::runtime_error if absent or wrong type.
 */
uint128
STObject::getFieldH128(SField const& field) const
{
    return getFieldByValue<STUInt128>(field);
}

/** @return Value of `field` as `uint160`.
 *  @throws std::runtime_error if absent or wrong type.
 */
uint160
STObject::getFieldH160(SField const& field) const
{
    return getFieldByValue<STUInt160>(field);
}

/** @return Value of `field` as `uint192`.
 *  @throws std::runtime_error if absent or wrong type.
 */
uint192
STObject::getFieldH192(SField const& field) const
{
    return getFieldByValue<STUInt192>(field);
}

/** @return Value of `field` as `uint256`.
 *  @throws std::runtime_error if absent or wrong type.
 */
uint256
STObject::getFieldH256(SField const& field) const
{
    return getFieldByValue<STUInt256>(field);
}

/** @return Value of `field` as `int32_t`.
 *  @throws std::runtime_error if absent or wrong type.
 */
std::int32_t
STObject::getFieldI32(SField const& field) const
{
    return getFieldByValue<STInt32>(field);
}

/** @return The `AccountID` stored in `field`.
 *  @throws std::runtime_error if absent or wrong type.
 */
AccountID
STObject::getAccountID(SField const& field) const
{
    return getFieldByValue<STAccount>(field);
}

/** Return the variable-length blob in `field` as a new `Blob`.
 *
 *  Returns an empty `Blob` when the optional field is absent.
 *
 *  @param field  Must be an `STBlob`-typed field.
 *  @throws std::runtime_error if wrong type.
 */
Blob
STObject::getFieldVL(SField const& field) const
{
    STBlob const empty;
    STBlob const& b = getFieldByConstRef<STBlob>(field, empty);
    return Blob(b.data(), b.data() + b.size());
}

/** Return a const reference to the `STAmount` in `field`.
 *
 *  Returns a reference to a function-local static empty `STAmount` when the
 *  optional field is absent, allowing callers to skip a presence check.
 *
 *  @param field  Must be an `STAmount`-typed field.
 */
STAmount const&
STObject::getFieldAmount(SField const& field) const
{
    static STAmount const kEMPTY{};
    return getFieldByConstRef<STAmount>(field, kEMPTY);
}

/** Return a const reference to the `STPathSet` in `field`.
 *
 *  Returns an empty path set when the optional field is absent.
 *
 *  @param field  Must be an `STPathSet`-typed field.
 */
STPathSet const&
STObject::getFieldPathSet(SField const& field) const
{
    static STPathSet const kEMPTY{};
    return getFieldByConstRef<STPathSet>(field, kEMPTY);
}

/** Return a const reference to the `STVector256` in `field`.
 *
 *  Returns an empty vector when the optional field is absent.
 *
 *  @param field  Must be an `STVector256`-typed field.
 */
STVector256 const&
STObject::getFieldV256(SField const& field) const
{
    static STVector256 const kEMPTY{};
    return getFieldByConstRef<STVector256>(field, kEMPTY);
}

/** Return a copy of the inner `STObject` at `field`, with template applied.
 *
 *  Unlike the other const-ref getters, this returns by value.  If the field
 *  is present, `applyTemplateFromSField` is called on the copy so the caller
 *  receives a template-bound object even when the source was deserialized in
 *  free mode.  If absent, returns an empty `STObject` constructed with `field`
 *  as its name.
 *
 *  @param field  Must be an `STObject`-typed field.
 */
STObject
STObject::getFieldObject(SField const& field) const
{
    STObject const empty{field};
    auto ret = getFieldByConstRef<STObject>(field, empty);
    if (ret != empty)
        ret.applyTemplateFromSField(field);
    return ret;
}

/** Return a const reference to the `STArray` in `field`.
 *
 *  Returns an empty array when the optional field is absent.
 *
 *  @param field  Must be an `STArray`-typed field.
 */
STArray const&
STObject::getFieldArray(SField const& field) const
{
    static STArray const kEMPTY{};
    return getFieldByConstRef<STArray>(field, kEMPTY);
}

/** Return a const reference to the `STCurrency` in `field`.
 *
 *  Returns an empty currency when the optional field is absent.
 *
 *  @param field  Must be an `STCurrency`-typed field.
 */
STCurrency const&
STObject::getFieldCurrency(SField const& field) const
{
    static STCurrency const kEMPTY{};
    return getFieldByConstRef<STCurrency>(field, kEMPTY);
}

/** Return a const reference to the `STNumber` in `field`.
 *
 *  Returns an empty `STNumber` when the optional field is absent.
 *
 *  @param field  Must be an `STNumber`-typed field.
 */
STNumber const&
STObject::getFieldNumber(SField const& field) const
{
    static STNumber const kEMPTY{};
    return getFieldByConstRef<STNumber>(field, kEMPTY);
}

/** Set a field by transferring ownership from a heap-allocated `STBase`.
 *
 *  Convenience overload that delegates to `set(STBase&&)`.
 *
 *  @param v  The field to set; must not be null.
 */
void
STObject::set(std::unique_ptr<STBase> v)
{
    set(std::move(*v.get()));
}

/** Set or replace a field by value.
 *
 *  If a slot for `v.getFName()` already exists in `v_`, it is replaced
 *  in-place.  For free objects, if the field is not present it is appended.
 *  For templated objects, unknown fields are rejected with
 *  `std::runtime_error`.
 *
 *  @param v  The new field value; its `SField` identity determines the slot.
 *  @throws std::runtime_error when in templated mode and the field is not
 *      in the schema.
 */
void
STObject::set(STBase&& v)
{
    auto const i = getFieldIndex(v.getFName());
    if (i != -1)
    {
        v_[i] = std::move(v);
    }
    else
    {
        if (!isFree())
            Throw<std::runtime_error>("missing field in templated STObject");
        v_.emplace_back(std::move(v));
    }
}

// --- Typed field setters ---
// Integer/bitstring setters delegate to setFieldUsingSetValue<T>, which calls
// T::setValue().  Complex-type setters delegate to setFieldUsingAssignment<T>,
// which uses T::operator=.  All setters make the field present if it currently
// holds the STI_NOTPRESENT sentinel, and create it in free objects when absent.

/** Set `field` to `v` (unsigned char). */
void
STObject::setFieldU8(SField const& field, unsigned char v)
{
    setFieldUsingSetValue<STUInt8>(field, v);
}

/** Set `field` to `v` (uint16_t). */
void
STObject::setFieldU16(SField const& field, std::uint16_t v)
{
    setFieldUsingSetValue<STUInt16>(field, v);
}

/** Set `field` to `v` (uint32_t). */
void
STObject::setFieldU32(SField const& field, std::uint32_t v)
{
    setFieldUsingSetValue<STUInt32>(field, v);
}

/** Set `field` to `v` (uint64_t). */
void
STObject::setFieldU64(SField const& field, std::uint64_t v)
{
    setFieldUsingSetValue<STUInt64>(field, v);
}

/** Set `field` to `v` (uint128). */
void
STObject::setFieldH128(SField const& field, uint128 const& v)
{
    setFieldUsingSetValue<STUInt128>(field, v);
}

/** Set `field` to `v` (uint192). */
void
STObject::setFieldH192(SField const& field, uint192 const& v)
{
    setFieldUsingSetValue<STUInt192>(field, v);
}

/** Set `field` to `v` (uint256). */
void
STObject::setFieldH256(SField const& field, uint256 const& v)
{
    setFieldUsingSetValue<STUInt256>(field, v);
}

/** Set `field` to `v` (int32_t). */
void
STObject::setFieldI32(SField const& field, std::int32_t v)
{
    setFieldUsingSetValue<STInt32>(field, v);
}

/** Set `field` to `v` (STVector256). */
void
STObject::setFieldV256(SField const& field, STVector256 const& v)
{
    setFieldUsingSetValue<STVector256>(field, v);
}

/** Set `field` to `v` (AccountID). */
void
STObject::setAccountID(SField const& field, AccountID const& v)
{
    setFieldUsingSetValue<STAccount>(field, v);
}

/** Set `field` to the contents of `v` (variable-length blob from Blob). */
void
STObject::setFieldVL(SField const& field, Blob const& v)
{
    setFieldUsingSetValue<STBlob>(field, Buffer(v.data(), v.size()));
}

/** Set `field` to the contents of `s` (variable-length blob from Slice).
 *
 *  Copies the bytes from `s` into an owned `Buffer`.
 */
void
STObject::setFieldVL(SField const& field, Slice const& s)
{
    setFieldUsingSetValue<STBlob>(field, Buffer(s.data(), s.size()));
}

/** Set `field` to `v` (STAmount). */
void
STObject::setFieldAmount(SField const& field, STAmount const& v)
{
    setFieldUsingAssignment(field, v);
}

/** Set `field` to `v` (STCurrency). */
void
STObject::setFieldCurrency(SField const& field, STCurrency const& v)
{
    setFieldUsingAssignment(field, v);
}

/** Set `field` to `v` (STIssue). */
void
STObject::setFieldIssue(SField const& field, STIssue const& v)
{
    setFieldUsingAssignment(field, v);
}

/** Set `field` to `v` (STNumber). */
void
STObject::setFieldNumber(SField const& field, STNumber const& v)
{
    setFieldUsingAssignment(field, v);
}

/** Set `field` to `v` (STPathSet). */
void
STObject::setFieldPathSet(SField const& field, STPathSet const& v)
{
    setFieldUsingAssignment(field, v);
}

/** Set `field` to `v` (STArray). */
void
STObject::setFieldArray(SField const& field, STArray const& v)
{
    setFieldUsingAssignment(field, v);
}

/** Set `field` to `v` (STObject). */
void
STObject::setFieldObject(SField const& field, STObject const& v)
{
    setFieldUsingAssignment(field, v);
}

/** Serialize this object to a JSON object.
 *
 *  Iterates `v_`, skipping `STI_NOTPRESENT` slots, and adds each present
 *  field as `{ jsonName: field.getJson(options) }`.
 *
 *  @param options  Controls display format (e.g., human-readable amounts).
 *  @return A `json::Value` of object type.
 */
json::Value
STObject::getJson(JsonOptions options) const
{
    json::Value ret(json::ValueType::Object);

    for (auto const& elem : v_)
    {
        if (elem->getSType() != STI_NOTPRESENT)
            ret[elem->getFName().getJsonName()] = elem->getJson(options);
    }
    return ret;
}

/** Compare two STObjects for equality, considering only binary fields.
 *
 *  Non-binary fields (metadata, computed values whose `SField::isBinary()`
 *  returns `false`) are excluded from comparison.  The algorithm is O(n²) by
 *  design: for each binary field in `*this`, it searches all fields in `obj`
 *  for a match.  The final check ensures the two sets have the same
 *  cardinality, preventing a subset from comparing equal to a superset.
 *
 *  @note For a more efficient alternative when both objects share the same
 *      template, use `isEquivalent()`.
 *
 *  @param obj  The object to compare against.
 *  @return `true` if all binary fields match in both objects.
 */
bool
STObject::operator==(STObject const& obj) const
{
    int matches = 0;
    for (auto const& t1 : v_)
    {
        if ((t1->getSType() != STI_NOTPRESENT) && t1->getFName().isBinary())
        {
            bool match = false;
            for (auto const& t2 : obj.v_)
            {
                if (t1->getFName() == t2->getFName())
                {
                    if (t2 != t1)
                        return false;

                    match = true;
                    ++matches;
                    break;
                }
            }

            if (!match)
                return false;
        }
    }

    int fields = 0;
    for (auto const& t2 : obj.v_)
    {
        if ((t2->getSType() != STI_NOTPRESENT) && t2->getFName().isBinary())
            ++fields;
    }

    return fields == matches;
}

/** Serialize the object's fields into `s` in canonical field-code order.
 *
 *  Obtains a `fieldCode`-sorted view of present fields via `getSortedFields`,
 *  then for each field:
 *  - writes the field-type/ID header (`addFieldID`),
 *  - writes the field value (`field->add(s)`),
 *  - appends an end-of-object or end-of-array termination marker for nested
 *    `STObject` and `STArray` fields.
 *
 *  The sort is mandatory for canonical binary encoding; two logically
 *  identical objects must produce identical bytes regardless of insertion
 *  order.
 *
 *  @param s            The serializer to append to.
 *  @param whichFields  `WithAllFields` includes signing fields;
 *      `OmitSigningFields` excludes them (used for signing hash computation).
 */
void
STObject::add(Serializer& s, WhichFields whichFields) const
{
    std::vector<STBase const*> const fields{getSortedFields(*this, whichFields)};

    for (STBase const* const field : fields)
    {
        // When we serialize an object inside another object,
        // the type associated by rule with this field name
        // must be OBJECT, or the object cannot be deserialized
        SerializedTypeID const sType{field->getSType()};
        XRPL_ASSERT(
            (sType != STI_OBJECT) || (field->getFName().fieldType == STI_OBJECT),
            "xrpl::STObject::add : valid field type");
        field->addFieldID(s);
        field->add(s);
        if (sType == STI_ARRAY || sType == STI_OBJECT)
            s.addFieldID(sType, 1);
    }
}

/** Build a sorted, filtered view of the fields in `objToSort`.
 *
 *  Collects pointers to all present (non-`STI_NOTPRESENT`) fields whose
 *  `SField::shouldInclude(bool)` returns `true` for the requested
 *  `whichFields` mode, then sorts them ascending by `SField::fieldCodeMem`
 *  (which encodes `(SerializedTypeID << 16) | fieldValue`).
 *
 *  This ordering is the canonical XRPL binary sort order.  Both
 *  serialization (`add`) and duplicate detection (`set(SerialIter)`) rely on
 *  it.
 *
 *  @param objToSort   The object whose fields are to be sorted.
 *  @param whichFields Controls whether signing-only fields are included.
 *  @return A vector of non-owning pointers in ascending `fieldCode` order.
 */
std::vector<STBase const*>
STObject::getSortedFields(STObject const& objToSort, WhichFields whichFields)
{
    std::vector<STBase const*> sf;
    sf.reserve(objToSort.getCount());

    for (detail::STVar const& elem : objToSort.v_)
    {
        STBase const& base = elem.get();
        if ((base.getSType() != STI_NOTPRESENT) &&
            base.getFName().shouldInclude(static_cast<bool>(whichFields)))
        {
            sf.push_back(&base);
        }
    }

    std::ranges::sort(sf, [](STBase const* lhs, STBase const* rhs) {
        return lhs->getFName().fieldCodeMem < rhs->getFName().fieldCodeMem;
    });

    return sf;
}

}  // namespace xrpl
