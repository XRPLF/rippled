/** @file
 *  Serializes a `Json::Value` tree to an `Output` streaming sink via `Writer`.
 *
 *  Provides two entry points — `outputJson()` for streaming to an arbitrary
 *  sink without a full-size allocation, and `jsonAsString()` for callers that
 *  need the result as a `std::string`.  The recursive worker that drives both
 *  is kept in an anonymous namespace so callers never need to instantiate a
 *  `Writer` directly when serializing a pre-built value tree.
 */

#include <xrpl/json/Output.h>

#include <xrpl/json/Writer.h>
#include <xrpl/json/json_value.h>

#include <string>

namespace json {

namespace {

/** Recursively serialize @p value into @p writer.
 *
 *  Dispatches on every case of `Json::ValueType`.  Scalar types are written
 *  with a single `writer.output()` call.  Collections use a two-step pattern:
 *  `writer.rawAppend()` (for array elements) or `writer.rawSet(tag)` (for
 *  object fields) emits the structural separator or key, then a recursive call
 *  writes the child value — which may itself open a nested collection.  This
 *  lets the `Writer`'s internal collection stack handle arbitrary nesting
 *  depth without any state kept here.
 *
 *  The switch has no `default` branch because `Json::ValueType` is a closed
 *  enumeration; every defined case is covered.
 *
 *  @param value  The JSON value to serialize.
 *  @param writer The `Writer` instance that owns the output sink and tracks
 *      open collection state.
 */
void
outputJson(Value const& value, Writer& writer)
{
    switch (value.type())
    {
        case ValueType::Null: {
            writer.output(nullptr);
            break;
        }

        case ValueType::Int: {
            writer.output(value.asInt());
            break;
        }

        case ValueType::UInt: {
            writer.output(value.asUInt());
            break;
        }

        case ValueType::Real: {
            writer.output(value.asDouble());
            break;
        }

        case ValueType::String: {
            writer.output(value.asString());
            break;
        }

        case ValueType::Boolean: {
            writer.output(value.asBool());
            break;
        }

        case ValueType::Array: {
            writer.startRoot(Writer::CollectionType::Array);
            for (auto const& i : value)
            {
                writer.rawAppend();
                outputJson(i, writer);
            }
            writer.finish();
            break;
        }

        case ValueType::Object: {
            writer.startRoot(Writer::CollectionType::Object);
            auto members = value.getMemberNames();
            for (auto const& tag : members)
            {
                writer.rawSet(tag);
                outputJson(value[tag], writer);
            }
            writer.finish();
            break;
        }
    }  // switch
}

}  // namespace

/** Serialize @p value to the streaming sink @p out.
 *
 *  Constructs a `Writer` on the stack bound to @p out, then delegates to the
 *  recursive worker.  The `Writer` destructor calls `finishAll()`, which
 *  closes any collections left open — guaranteeing a well-formed JSON document
 *  even if an exception unwinds the stack before the recursive walk completes.
 *
 *  @param value  The JSON value tree to serialize.
 *  @param out    Sink callback that receives serialized string fragments.
 *  @see jsonAsString() for a convenience wrapper that collects fragments into
 *      a `std::string`.
 */
void
outputJson(Value const& value, Output const& out)
{
    Writer writer(out);
    outputJson(value, writer);
}

/** Return the minimal JSON string representation of @p value.
 *
 *  Allocates a `std::string`, wraps it with `stringOutput()`, and forwards to
 *  `outputJson()`.  Prefer `outputJson()` with a direct sink when avoiding a
 *  full-size allocation matters.
 *
 *  @param value  The JSON value tree to serialize.
 *  @return A minimal (no whitespace) JSON string.
 */
std::string
jsonAsString(Value const& value)
{
    std::string s;
    Writer writer(stringOutput(s));
    outputJson(value, writer);
    return s;
}

}  // namespace json
