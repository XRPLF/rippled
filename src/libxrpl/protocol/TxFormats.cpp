/** @file
 *  Central registry materializing the field schema for every XRPL transaction
 *  type.
 *
 *  Each transaction type is registered once in `TxFormats::TxFormats()` by
 *  expanding `transactions.macro` with a bespoke `TRANSACTION` macro.  The
 *  resulting `SOTemplate` objects govern wire parsing, JSON validation, and
 *  programmatic construction for the lifetime of the process.
 */

#include <xrpl/protocol/TxFormats.h>

#include <xrpl/protocol/Feature.h>  // IWYU pragma: keep
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/jss.h>  // IWYU pragma: keep

#include <vector>

namespace xrpl {

/** Return the fields shared by every XRPL transaction, regardless of type.
 *
 *  The returned vector is merged with each transaction's unique fields inside
 *  `KnownFormats::add()` to produce the complete `SOTemplate` for that type.
 *  Callers must not mutate the returned reference.
 *
 *  Required fields — `sfTransactionType`, `sfAccount`, `sfSequence`,
 *  `sfFee`, and `sfSigningPubKey` — form the minimum viable transaction
 *  skeleton; a serialized object missing any of them fails template validation.
 *
 *  Notable optional fields and their protocol roles:
 *  - `sfPreviousTxnID` — retained for backward compatibility with the pre-027
 *      wire format; kept optional rather than removed to avoid invalidating
 *      older transaction blobs.
 *  - `sfSigners` — the multi-signature array; coexists with `sfSigningPubKey`
 *      because single-sig and multi-sig are orthogonal modes at the format
 *      level.
 *  - `sfTicketSequence` — allows a transaction to consume a ticket instead of
 *      the account's current sequence number, enabling out-of-order submission.
 *  - `sfNetworkID` — lets sidechain networks distinguish their transactions
 *      from mainnet ones at the wire level.
 *  - `sfDelegate` — supports the delegation feature, allowing an account to
 *      authorize another account to act on its behalf.
 *
 *  @return A stable reference to the static common-field list.  The vector is
 *      initialized on the first call and lives for the lifetime of the process.
 */
std::vector<SOElement> const&
TxFormats::getCommonFields()
{
    static auto const kCOMMON_FIELDS = std::vector<SOElement>{
        {sfTransactionType, SoeRequired},
        {sfFlags, SoeOptional},
        {sfSourceTag, SoeOptional},
        {sfAccount, SoeRequired},
        {sfSequence, SoeRequired},
        {sfPreviousTxnID, SoeOptional},
        {sfLastLedgerSequence, SoeOptional},
        {sfAccountTxnID, SoeOptional},
        {sfFee, SoeRequired},
        {sfOperationLimit, SoeOptional},
        {sfMemos, SoeOptional},
        {sfSigningPubKey, SoeRequired},
        {sfTicketSequence, SoeOptional},
        {sfTxnSignature, SoeOptional},
        {sfSigners, SoeOptional},
        {sfNetworkID, SoeOptional},
        {sfDelegate, SoeOptional},
    };
    return kCOMMON_FIELDS;
}

/** Populate the registry with every known transaction format.
 *
 *  Iterates `transactions.macro` via X-macro expansion, calling
 *  `KnownFormats::add()` once per transaction type.  Each call merges the
 *  type-specific `SOElement` list with `getCommonFields()` into an
 *  `SOTemplate` and indexes the result by both `TxType` and name.
 *
 *  The `UNWRAP(...)` helper strips the extra parentheses that surround each
 *  field list in the macro file — those parens prevent the comma-separated
 *  field entries from being interpreted as separate macro arguments.
 *
 *  The `push_macro` / `pop_macro` sandwich preserves any pre-existing
 *  definitions of `TRANSACTION` or `UNWRAP` in the translation unit, avoiding
 *  hard-to-diagnose macro collisions with platform or third-party headers.
 *
 *  @note A duplicate `TxType` value triggers `logicError()` (process abort)
 *      inside `KnownFormats::add()`, making type-ID collisions a hard crash
 *      at static-initialization time rather than a silent runtime bug.
 */
TxFormats::TxFormats()
{
#pragma push_macro("UNWRAP")
#undef UNWRAP
#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define UNWRAP(...) __VA_ARGS__
#define TRANSACTION(tag, value, name, delegable, amendment, privileges, fields) \
    add(jss::name, tag, UNWRAP fields, getCommonFields());

#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
#undef UNWRAP
#pragma pop_macro("UNWRAP")
}

/** Return the process-wide singleton registry of all transaction formats.
 *
 *  The registry is constructed on the first call via a function-local static,
 *  guaranteeing both lazy initialization and thread-safe construction under
 *  the C++11 rules for local statics.  The same reference is returned on
 *  every subsequent call.
 *
 *  @return A stable `const` reference to the singleton `TxFormats` instance.
 */
TxFormats const&
TxFormats::getInstance()
{
    static TxFormats const kINSTANCE;
    return kINSTANCE;
}

}  // namespace xrpl
