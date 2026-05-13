/** @file
 *  Registers every on-ledger object type with its canonical field schema.
 *
 *  This translation unit is the single registration point that populates the
 *  `LedgerFormats` singleton.  The constructor uses an X-macro pass over
 *  `ledger_entries.macro` so that both the `LedgerEntryType` enum (declared
 *  in the header) and the `SOTemplate` validation schemas (built here) are
 *  derived from one authoritative source.
 *
 *  @note `LedgerEntryType` numeric values are serialized into every ledger
 *      object and are therefore protocol-stable.  Reusing or renumbering them
 *      without explicit amendment logic causes a hard fork.
 */

#include <xrpl/protocol/LedgerFormats.h>

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/jss.h>  // IWYU pragma: keep

#include <vector>

namespace xrpl {

/** Return the three fields present in every ledger entry.
 *
 *  These fields are injected into every `SOTemplate` built by the
 *  `LedgerFormats` constructor, so a change here automatically propagates to
 *  all ~30 registered entry types.
 *
 *  The three universal fields are:
 *  - `sfLedgerIndex`     (`SoeOptional`) — position in the ledger; may be
 *      absent in some serialization contexts.
 *  - `sfLedgerEntryType` (`SoeRequired`) — numeric type discriminator used
 *      by `findByType()` for lookup; must always be present.
 *  - `sfFlags`           (`SoeRequired`) — object flag bitmask; must always
 *      be present, even when zero.
 *
 *  Using a function-local static avoids the static-initialization-order
 *  problem: the vector is constructed on first call, which occurs inside the
 *  `LedgerFormats` constructor before any entry type accesses it.
 *
 *  @return A reference to the immutable common-fields vector.
 */
std::vector<SOElement> const&
LedgerFormats::getCommonFields()
{
    static auto const kCOMMON_FIELDS = std::vector<SOElement>{
        {sfLedgerIndex, SoeOptional},
        {sfLedgerEntryType, SoeRequired},
        {sfFlags, SoeRequired},
    };
    return kCOMMON_FIELDS;
}

/** Populate the registry with all known ledger entry formats.
 *
 *  Uses an X-macro pass over `ledger_entries.macro`: the local `LEDGER_ENTRY`
 *  macro expands each entry into an `add(jss::name, tag, fields,
 *  getCommonFields())` call.  `UNWRAP(...)` strips the extra layer of
 *  parentheses that protects field-list commas from the preprocessor.
 *
 *  `#pragma push_macro`/`pop_macro` guards both `LEDGER_ENTRY` and `UNWRAP`
 *  so any pre-existing definitions in this translation unit are restored after
 *  the include, preventing hard-to-diagnose build failures.
 *
 *  `LEDGER_ENTRY_DUPLICATE` (defined in the macro file itself) handles the
 *  `DepositPreauth` name collision between transaction and ledger entry types:
 *  it expands identically to `LEDGER_ENTRY` but suppresses the `jss.h`
 *  `JSS()` emission to avoid a duplicate-symbol link error.
 *
 *  @note If `ledger_entries.macro` contains a duplicate numeric type ID,
 *      `KnownFormats::add()` detects it via `findByType()` and throws
 *      `LogicError` during static initialization, causing a crash on startup
 *      rather than silent registry corruption.
 */
LedgerFormats::LedgerFormats()
{
#pragma push_macro("UNWRAP")
#undef UNWRAP
#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY

#define UNWRAP(...) __VA_ARGS__
#define LEDGER_ENTRY(tag, value, name, rpcName, fields) \
    add(jss::name, tag, UNWRAP fields, getCommonFields());

#include <xrpl/protocol/detail/ledger_entries.macro>

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")
#undef UNWRAP
#pragma pop_macro("UNWRAP")
}

/** Return the singleton registry of all known ledger entry formats.
 *
 *  Uses a function-local static (Meyer's singleton) for thread-safe,
 *  once-only initialization guaranteed by C++11.  The first call constructs
 *  the `LedgerFormats` object and registers every entry type; subsequent
 *  calls return the same instance.
 *
 *  Callers use the returned object to validate and deserialize ledger entries,
 *  for example via `findByType(type)` to retrieve the `SOTemplate` for a
 *  given `LedgerEntryType`.
 *
 *  @return A `const` reference to the process-wide `LedgerFormats` instance.
 */
LedgerFormats const&
LedgerFormats::getInstance()
{
    static LedgerFormats const kINSTANCE;
    return kINSTANCE;
}

}  // namespace xrpl
