/** @file
 *  Public interface for the `server_definitions` RPC endpoint.
 *
 *  Exposes a single function that returns the complete XRPL protocol schema
 *  as a static, process-lifetime JSON object.  The payload is built once
 *  (Meyers singleton) and includes a SHA-512-half fingerprint for
 *  client-side cache validation.
 *
 *  @see ServerDefinitions.cpp for the full construction details.
 */
#pragma once

#include <xrpl/json/json_value.h>

namespace xrpl {

/** Return the complete XRPL protocol schema assembled at process startup.
 *
 *  The returned object contains eleven top-level keys:
 *  `TYPES`, `LEDGER_ENTRY_TYPES`, `TRANSACTION_TYPES`, `FIELDS`,
 *  `TRANSACTION_RESULTS`, `TRANSACTION_FORMATS`, `LEDGER_ENTRY_FORMATS`,
 *  `TRANSACTION_FLAGS`, `LEDGER_ENTRY_FLAGS`, `ACCOUNT_SET_FLAGS`, and
 *  `hash` (a SHA-512-half fingerprint of the other ten keys).
 *
 *  This function is the non-RPC access path to the same singleton used by
 *  the `server_definitions` JSON-RPC handler — for example, it is called by
 *  the `--definitions` CLI flag in `Main.cpp` to dump the schema to stdout.
 *
 *  @return Const reference to the fully populated definitions JSON object,
 *      valid for the lifetime of the process.
 *  @note The reference is safe to capture and hold indefinitely; the
 *      underlying singleton is never destroyed.
 */
json::Value const&
getServerDefinitionsJson();

}  // namespace xrpl
