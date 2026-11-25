#pragma once

#include <xrpl/json/json_value.h>

#include <cstddef>
#include <string>

namespace xrpl::rpc {

/**
 * How much of a request or a reply a log line carries.
 *
 * A client chooses the size of what it sends, so nothing it cannot choose
 * bounds the length.
 */
inline constexpr std::size_t kMaxLoggedChars = 10000;

/**
 * Returns a copy of @p request with every credential-bearing field replaced by
 * a placeholder.
 *
 * Apply to every request echoed back to a caller. Masks at every depth, since
 * the JSON-RPC transport nests a credential inside `params` and a batch nests
 * one request per entry. Recursion is bounded by json::Reader::kNestLimit.
 *
 * @param request The request to mask.
 * @return The masked copy.
 */
[[nodiscard]] json::Value
maskSecrets(json::Value const& request);

/**
 * Reports whether @p value carries a credential-bearing field at any depth.
 *
 * Walks objects and arrays without copying and stops at the first field named
 * in kCredentialFields, so a caller pays for masking only when there is
 * something to mask. A value with no members carries none.
 *
 * @param value The request or reply to inspect.
 * @return True if a credential-bearing field is present at any depth.
 */
[[nodiscard]] bool
hasSecret(json::Value const& value);

/**
 * Renders @p value for a log line: masked when it carries a credential, then
 * truncated to kMaxLoggedChars.
 *
 * Every site that writes a request or a reply body to the log goes through
 * this. A value with no credential is serialized as it is, so the copy the
 * mask takes is paid only when hasSecret() reports one.
 *
 * Call it inside the JLOG argument and never before the macro: that argument is
 * evaluated only when the sink is active, so hoisting the call puts the walk
 * and the serialization on every request.
 *
 * Truncation is by bytes, so a multi-byte character can be split.
 *
 * @param value The request or reply to render.
 * @return The masked, truncated text.
 */
[[nodiscard]] std::string
loggable(json::Value const& value);

}  // namespace xrpl::rpc
