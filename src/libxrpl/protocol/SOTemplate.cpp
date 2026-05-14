/** @file
 *  Implements the SOTemplate schema registry used by every serialized
 *  object type in the XRP Ledger.
 *
 *  An SOTemplate is the immutable field schema for one transaction or
 *  ledger entry type (e.g., Payment, AccountRoot).  It is constructed
 *  once at program startup as part of the TxFormats / LedgerFormats /
 *  InnerObjectFormats singletons and then queried on every
 *  serialization, deserialization, and field-validation call.
 *
 *  The core data structure is a direct-address lookup table
 *  (`indices_`) sized to the total number of registered SFields.
 *  Given an SField, its position inside `elements_` is found in O(1)
 *  with a single array subscript — no hashing, no comparisons.
 *
 *  The two constructor overloads (initializer_list and vector) share
 *  the same implementation; the initializer_list form delegates to the
 *  vector form.  Both accept separate `uniqueFields` and `commonFields`
 *  lists so callers (KnownFormats) can maintain the common field set
 *  once and pass it to every format without duplication.
 */

#include <xrpl/protocol/SOTemplate.h>

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/SField.h>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace xrpl {

/** Convenience overload that converts initializer lists to vectors and
 *  delegates to the vector constructor.
 *
 *  @param uniqueFields Fields specific to this object type.
 *  @param commonFields Fields shared across all object types (e.g., Fee,
 *      Sequence, Signature for transactions).
 */
SOTemplate::SOTemplate(
    std::initializer_list<SOElement> uniqueFields,
    std::initializer_list<SOElement> commonFields)
    : SOTemplate(std::vector(uniqueFields), std::vector(commonFields))
{
}

/** Build the schema from two field lists and construct the O(1) index table.
 *
 *  The two lists are merged into a single ordered sequence (`elements_`),
 *  unique fields first.  The index table (`indices_`) is then populated so
 *  that `indices_[sField.getNum()]` holds the position of that field in
 *  `elements_`, enabling O(1) lookup in `getIndex()`.
 *
 *  `indices_` is snapshotted at construction time from
 *  `SField::getNumFields()`.  Fields registered after this template is
 *  constructed cannot be added to it, which is intentional — templates
 *  are immutable after construction.
 *
 *  Two invariants are enforced for every field:
 *  - **Range:** `0 < fieldNum < indices_.size()`.  Sentinel fields such as
 *    `sfInvalid` (non-positive field number) and any field registered after
 *    snapshot (field number ≥ table size) are rejected.
 *  - **Uniqueness:** a field may appear at most once across both lists.
 *    Duplicates would silently corrupt the index mapping, so the check is
 *    fatal: any violation throws immediately during program initialization.
 *
 *  @param uniqueFields Fields specific to this object type; placed first in
 *      `elements_`.
 *  @param commonFields Fields shared across all object types; appended after
 *      unique fields.
 *  @throws std::runtime_error if any field has an out-of-range field number
 *      or appears more than once across the two lists.
 */
SOTemplate::SOTemplate(std::vector<SOElement> uniqueFields, std::vector<SOElement> commonFields)
    : indices_(SField::getNumFields() + 1, -1)
{
    elements_ = std::move(uniqueFields);
    std::ranges::move(commonFields, std::back_inserter(elements_));

    for (std::size_t i = 0; i < elements_.size(); ++i)
    {
        SField const& sField{elements_[i].sField()};

        if (sField.getNum() <= 0 || sField.getNum() >= indices_.size())
            Throw<std::runtime_error>("Invalid field index for SOTemplate.");

        if (getIndex(sField) != -1)
            Throw<std::runtime_error>("Duplicate field index for SOTemplate.");

        indices_[sField.getNum()] = i;
    }
}

/** Return the position of @p sField in `elements_`, or -1 if absent.
 *
 *  Performs a direct array subscript into `indices_` using the field's
 *  registered number, giving O(1) cost regardless of template size.
 *  This is the hot path called on every field access during serialization
 *  and deserialization.
 *
 *  @param sField The field to look up.
 *  @return Index into `elements_` where the field's SOElement lives, or
 *      -1 if the field is not part of this template.
 *  @throws std::runtime_error if @p sField has a non-positive or
 *      out-of-range field number (i.e., it is a sentinel field or was
 *      registered after this template was constructed).
 */
int
SOTemplate::getIndex(SField const& sField) const
{
    if (sField.getNum() <= 0 || sField.getNum() >= indices_.size())
        Throw<std::runtime_error>("Invalid field index for getIndex().");

    return indices_[sField.getNum()];
}

}  // namespace xrpl
