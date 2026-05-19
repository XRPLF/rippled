#pragma once

#include <xrpl/protocol/STArray.h>

#include <optional>

namespace xrpl {

/** Maximum JSON object nesting depth permitted during parsing. */
inline constexpr std::size_t kMaxParsedJsonDepth = 64;

/** Maximum number of elements permitted in any JSON array field during parsing.
    Requests exceeding this limit are rejected with an invalidParams error. */
inline constexpr std::size_t kMaxParsedJsonArraySize = 512;

/** Holds the serialized result of parsing an input JSON object.
    This does validation and checking on the provided JSON.
*/
class STParsedJSONObject
{
public:
    /** Parses and creates an STParsedJSON object.
        The result of the parsing is stored in object and error.
        Exceptions:
            Does not throw.
        @param name The name of the JSON field, used in diagnostics.
        @param json The JSON-RPC to parse.
    */
    STParsedJSONObject(std::string const& name, json::Value const& json);

    STParsedJSONObject() = delete;
    STParsedJSONObject(STParsedJSONObject const&) = delete;
    STParsedJSONObject&
    operator=(STParsedJSONObject const&) = delete;
    ~STParsedJSONObject() = default;

    /** The STObject if the parse was successful. */
    std::optional<STObject> object;

    /** On failure, an appropriate set of error values. */
    json::Value error;
};

}  // namespace xrpl
