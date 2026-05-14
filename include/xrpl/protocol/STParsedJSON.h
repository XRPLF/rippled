#pragma once

#include <xrpl/protocol/STArray.h>

#include <optional>

namespace xrpl {

/** Maximum JSON object nesting depth permitted during parsing. */
inline constexpr std::size_t kMAX_PARSED_JSON_DEPTH = 64;

/** Maximum number of elements permitted in any JSON array field during parsing.
    Requests exceeding this limit are rejected with an invalidParams error. */
inline constexpr std::size_t kMAX_PARSED_JSON_ARRAY_SIZE = 512;

/** Single-use converter from a JSON object to an @ref STObject.
 *
 *  Sits at the boundary between the JSON/RPC layer and the binary-canonical
 *  Serialized Type (ST) system.  In a single constructor call it validates
 *  every field name against the XRPL protocol schema, coerces each value to
 *  its wire type, recurses into nested objects and arrays up to
 *  @ref kMAX_PARSED_JSON_DEPTH levels deep with each array bounded by
 *  @ref kMAX_PARSED_JSON_ARRAY_SIZE elements, and applies the field template
 *  for the transaction or ledger-entry type discovered during parsing.
 *
 *  Outcomes are communicated through two public members rather than via
 *  exceptions or a return value, which lets RPC handlers forward `error`
 *  directly to the client without additional formatting work:
 *  - On success: `object` holds the populated `STObject`; `error` is empty.
 *  - On failure: `object` is `std::nullopt`; `error` is an
 *    `rpcINVALID_PARAMS` JSON value with a dot-separated field path
 *    (e.g. `"tx_json.Signers[0].Signer.Account"`) pinpointing the offending
 *    field.
 *
 *  The class is non-copyable and not default-constructible; every instance
 *  represents exactly one completed parse attempt.
 *
 *  @see TransactionSign.cpp for the primary production call-site.
 */
class STParsedJSONObject
{
public:
    /** Parse @p json into a strongly-typed @ref STObject.
     *
     *  Iterates every member of the JSON object, resolves each field name
     *  via `SField::getField()`, recurses into nested objects and arrays,
     *  and finally calls `applyTemplateFromSField()` to enforce the field
     *  template for the detected transaction or ledger-entry type.
     *
     *  All internal exceptions are caught and translated into a structured
     *  `rpcINVALID_PARAMS` error stored in `error`; nothing propagates to
     *  the caller.
     *
     *  @param name  The logical name of the top-level field being parsed
     *      (e.g. `"tx_json"`); used as the root of dot-separated field-path
     *      strings in `error` messages.
     *  @param json  The JSON object to parse.  Must be a JSON object value;
     *      non-object input produces an `rpcINVALID_PARAMS` error.
     *  @note Does not throw.
     */
    STParsedJSONObject(std::string const& name, json::Value const& json);

    STParsedJSONObject() = delete;
    STParsedJSONObject(STParsedJSONObject const&) = delete;
    STParsedJSONObject&
    operator=(STParsedJSONObject const&) = delete;
    ~STParsedJSONObject() = default;

    /** The parsed object on success, or `std::nullopt` on any parse error. */
    std::optional<STObject> object;

    /** Structured `rpcINVALID_PARAMS` error on failure; empty on success.
     *
     *  The JSON value is suitable for forwarding directly to an RPC client.
     *  The `"error_message"` field contains a dot-separated field path
     *  identifying the first field that failed to parse.
     */
    json::Value error;
};

}  // namespace xrpl
