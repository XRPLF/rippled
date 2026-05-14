/** @file
 *  String-returning façade over the JSON writer hierarchy.
 *
 *  Provides `to_string` (compact, single-line output via `FastWriter`) and
 *  `pretty` (indented, human-readable output via `StyledWriter`). Both
 *  functions construct their writer on the stack, making each call
 *  self-contained and thread-safe with no shared mutable state.
 *
 *  @see json::FastWriter, json::StyledWriter
 */

#include <xrpl/json/to_string.h>

#include <xrpl/json/json_writer.h>

#include <string>

namespace json {

/** Serialize a JSON value to a compact, single-line string.
 *
 *  Delegates to `FastWriter`, which emits the entire document on one line
 *  with no extraneous whitespace. Suitable for network RPC responses and
 *  log output where bandwidth and parse overhead matter.
 *
 *  @param value The JSON value to serialize.
 *  @return A compact JSON string with no newlines or extra whitespace.
 *  @note No validation is performed; all type-dispatch and escaping are
 *      handled inside `FastWriter::writeValue`.
 *  @see json::pretty
 */
std::string
to_string(Value const& value)
{
    return FastWriter().write(value);
}

/** Serialize a JSON value to an indented, human-readable string.
 *
 *  Delegates to `StyledWriter`, which applies 3-space indentation per
 *  nesting level and a 74-character right-margin heuristic to decide
 *  when arrays should break across lines. Intended for diagnostic output
 *  and human inspection.
 *
 *  @param value The JSON value to serialize.
 *  @return A formatted JSON string with indentation and line breaks.
 *  @note No validation is performed; all type-dispatch and escaping are
 *      handled inside `StyledWriter::writeValue`.
 *  @see json::to_string
 */
std::string
pretty(Value const& value)
{
    return StyledWriter().write(value);
}

}  // namespace json
