#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/json/json_value.h>

#include <cstddef>
#include <string>

namespace json {

/**
 * How much of a request or a reply a log line carries.
 *
 * A client chooses the size of what it sends, so nothing it cannot choose
 * bounds the length. The cap is the length limit of a log line, so one value
 * can fill a line and no more. A value at the cap, together with the
 * timestamp and the rest of the message, makes the line longer than the
 * limit, so Logs::format cuts the line and ends it in an ellipsis.
 */
inline constexpr std::size_t kMaxLoggedChars = xrpl::Logs::kMaximumMessageCharacters;

/**
 * Returns a copy of @p request with every credential-bearing field replaced by
 * a placeholder.
 *
 * Masks at every depth, since the JSON-RPC transport nests a credential inside
 * `params` and a batch nests one request per entry. Recursion is bounded by
 * json::Reader::kNestLimit for a value that was parsed.
 *
 * @param request The request to mask.
 * @return The masked copy.
 */
[[nodiscard]] Value
maskSecrets(Value const& request);

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
hasSecret(Value const& value);

/**
 * Renders @p value for a log line: masked when it carries a credential, then
 * truncated to kMaxLoggedChars.
 *
 * A value with no credential is serialized as it is, so the copy the mask
 * takes is paid only when hasSecret() reports one.
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
loggable(Value const& value);

}  // namespace json
