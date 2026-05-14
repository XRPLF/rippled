#pragma once

#include <xrpl/protocol/KnownFormats.h>

namespace xrpl {

/** Singleton registry of field schemas for all XRPL inner object types.
 *
 *  Inner objects are the structured sub-objects that appear nested inside
 *  transactions and ledger entries — for example, `sfSigner`, `sfSignerEntry`,
 *  `sfNFToken`, `sfAuctionSlot`, and `sfPriceData`. This registry plays the
 *  same role for those nested objects as `TxFormats` plays for top-level
 *  transactions: it maps each inner object's `SField` code to an `SOTemplate`
 *  that declares which child fields are `soeREQUIRED`, `soeOPTIONAL`, or
 *  `soeDEFAULT`.
 *
 *  The key type is `int` (the integer field code returned by
 *  `SField::getCode()`) rather than a dedicated enum, because inner objects
 *  are already identified by their `SField` descriptors in wire format.
 *
 *  The registry is complete and immutable after the first call to
 *  `getInstance()`. Duplicate key registration triggers a `LogicError` at
 *  static-init time. The returned `const&` from `getInstance()` is safe for
 *  concurrent reads without additional locking.
 *
 *  @see TxFormats, LedgerFormats, SOTemplate
 */
class InnerObjectFormats : public KnownFormats<int, InnerObjectFormats>
{
private:
    /** Register all known inner object schemas.
     *
     *  Each `add()` call maps an `SField`'s JSON name and integer field code
     *  to an `SOTemplate` that specifies the required, optional, and default
     *  child fields. The `SField` code doubles as the registry key, so no
     *  separate enumeration is needed.
     */
    InnerObjectFormats();

public:
    /** Return the process-wide singleton instance.
     *
     *  Initialized on first call via a Meyer's function-local static; safe
     *  for concurrent access after construction. The object is immutable
     *  after it is returned for the first time.
     *
     *  @return A `const` reference to the singleton registry.
     */
    static InnerObjectFormats const&
    getInstance();

    /** Look up the field schema for a structured inner object.
     *
     *  Translates an `SField` to its registered `SOTemplate` by matching on
     *  the field's integer code. The returned pointer is stable for the
     *  lifetime of the process; callers may cache it safely.
     *
     *  Called by `STObject::makeInnerObject()` (amendment-gated on
     *  `fixInnerObjTemplate` / `fixInnerObjTemplate2`) and by
     *  `STObject::applyTemplateFromSField()` to enforce field-presence rules
     *  during construction and deserialization.
     *
     *  @param sField The `SField` identifying the inner object type.
     *  @return A pointer to the matching `SOTemplate`, or `nullptr` if
     *      `sField` is not a registered inner object type.
     */
    [[nodiscard]] SOTemplate const*
    findSOTemplateBySField(SField const& sField) const;
};

}  // namespace xrpl
