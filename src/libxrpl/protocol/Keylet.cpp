/** @file
 *  Implements Keylet::check(), the runtime guardian that enforces type
 *  correctness after a SHAMap lookup.
 */

#include <xrpl/protocol/Keylet.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STLedgerEntry.h>

namespace xrpl {

/** Validates that a deserialized ledger entry corresponds to this keylet.
 *
 *  Applies a three-tier match, ordered from most-permissive to most-strict:
 *
 *  1. **`ltANY`** — wildcard; always returns `true`. The caller bears full
 *     responsibility for type safety (used by the `keylet::unchecked` family).
 *  2. **`ltCHILD`** — directory-child pseudo-type; returns `true` for any
 *     entry whose concrete type is not `ltDIR_NODE`. Directory nodes are
 *     structural bookkeeping objects; a child of a directory is definitionally
 *     something other than a directory node.
 *  3. **Exact match** — for all concrete types, both the entry's type and its
 *     key must equal those stored in this keylet. This is the normal case.
 *
 *  @param sle The deserialized ledger entry retrieved from the state map.
 *  @return `true` if `sle` legitimately corresponds to this keylet.
 *  @note `sle` must not carry `ltANY` or `ltCHILD` as its own type; these
 *      are pseudo-types used only for keylet construction and filtering. An
 *      `XRPL_ASSERT` enforces this precondition — a violation indicates a
 *      malformed entry read from the state map.
 */
bool
Keylet::check(STLedgerEntry const& sle) const
{
    XRPL_ASSERT(
        sle.getType() != ltANY && sle.getType() != ltCHILD,
        "xrpl::Keylet::check : valid input type");

    if (type == ltANY)
        return true;

    if (type == ltCHILD)
        return sle.getType() != ltDIR_NODE;

    return sle.getType() == type && sle.key() == key;
}

}  // namespace xrpl
