/** @file
 *  Implements JsonPropertyStream, the JSON sink for the PropertyStream
 *  diagnostics framework.  The sole production consumer is the `print`
 *  admin RPC handler (doPrint), which writes the application's entire
 *  Source hierarchy into this stream and returns stream.top() as the
 *  RPC response.
 */

#include <xrpl/json/JsonPropertyStream.h>

#include <xrpl/json/json_value.h>

#include <string>

namespace xrpl {

/** Initialise the root object and seed the insertion-point stack.
 *
 *  `topValue` is constructed as an empty JSON object.  A pointer to it is
 *  pushed onto `stack` so that the first `mapBegin`/`arrayBegin` call has a
 *  valid parent.  64 slots are pre-reserved to avoid heap reallocations
 *  during a traversal; in practice the diagnostic Source tree is never
 *  that deep.
 */
JsonPropertyStream::JsonPropertyStream() : topValue(json::ValueType::Object)
{
    stack.reserve(64);
    stack.push_back(&topValue);
}

/** Return the completed JSON object tree.
 *
 *  Callers (e.g. doPrint) retrieve this after the write traversal is
 *  finished; the result is undefined if `stack` still has un-popped frames.
 *
 *  @return A const reference to the root `objectValue` owned by this stream.
 */
json::Value const&
JsonPropertyStream::top() const
{
    return topValue;
}

/** Open an anonymous child object inside the current array context.
 *
 *  Appends a new `objectValue` element to the top-of-stack array and pushes
 *  a pointer to it, making it the new insertion point.
 *
 *  @pre The current top-of-stack value must be an array.
 */
void
JsonPropertyStream::mapBegin()
{
    json::Value& top(*stack.back());
    json::Value& map(top.append(json::ValueType::Object));
    stack.push_back(&map);
}

/** Open a named child object inside the current map context.
 *
 *  Assigns a new `objectValue` to `top[key]` and pushes a pointer to the
 *  resulting child, making it the new insertion point.
 *
 *  @param key The property name under which the child object is stored.
 *  @pre The current top-of-stack value must be an object.
 */
void
JsonPropertyStream::mapBegin(std::string const& key)
{
    json::Value& top(*stack.back());
    json::Value& map(top[key] = json::ValueType::Object);
    stack.push_back(&map);
}

/** Close the current map context and restore the parent insertion point. */
void
JsonPropertyStream::mapEnd()
{
    stack.pop_back();
}

void
JsonPropertyStream::add(std::string const& key, short v)
{
    (*stack.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, unsigned short v)
{
    (*stack.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, int v)
{
    (*stack.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, unsigned int v)
{
    (*stack.back())[key] = v;
}

/** Insert a keyed `long` value, narrowed to `int`.
 *
 *  The underlying `json::Value` has no native 64-bit signed integer type, so
 *  `long` is explicitly cast to `int` before storage.  Values wider than 32
 *  bits will be silently truncated.
 *
 *  @param key The property name.
 *  @param v   The value to store; truncated to `int` range on 64-bit platforms.
 */
void
JsonPropertyStream::add(std::string const& key, long v)
{
    (*stack.back())[key] = int(v);
}

void
JsonPropertyStream::add(std::string const& key, float v)
{
    (*stack.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, double v)
{
    (*stack.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, std::string const& v)
{
    (*stack.back())[key] = v;
}

/** Open an anonymous child array inside the current array context.
 *
 *  Appends a new `arrayValue` element to the top-of-stack array and pushes
 *  a pointer to it, making it the new insertion point.
 *
 *  @pre The current top-of-stack value must be an array.
 */
void
JsonPropertyStream::arrayBegin()
{
    json::Value& top(*stack.back());
    json::Value& vec(top.append(json::ValueType::Array));
    stack.push_back(&vec);
}

/** Open a named child array inside the current map context.
 *
 *  Assigns a new `arrayValue` to `top[key]` and pushes a pointer to the
 *  resulting child, making it the new insertion point.
 *
 *  @param key The property name under which the child array is stored.
 *  @pre The current top-of-stack value must be an object.
 */
void
JsonPropertyStream::arrayBegin(std::string const& key)
{
    json::Value& top(*stack.back());
    json::Value& vec(top[key] = json::ValueType::Array);
    stack.push_back(&vec);
}

/** Close the current array context and restore the parent insertion point. */
void
JsonPropertyStream::arrayEnd()
{
    stack.pop_back();
}

void
JsonPropertyStream::add(short v)
{
    stack.back()->append(v);
}

void
JsonPropertyStream::add(unsigned short v)
{
    stack.back()->append(v);
}

void
JsonPropertyStream::add(int v)
{
    stack.back()->append(v);
}

void
JsonPropertyStream::add(unsigned int v)
{
    stack.back()->append(v);
}

/** Append a `long` value to the current array, narrowed to `int`.
 *
 *  Same truncation caveat as the keyed overload: values wider than 32 bits
 *  are silently truncated because `json::Value` has no native 64-bit signed
 *  integer type.
 *
 *  @param v The value to append; truncated to `int` range on 64-bit platforms.
 */
void
JsonPropertyStream::add(long v)
{
    stack.back()->append(int(v));
}

void
JsonPropertyStream::add(float v)
{
    stack.back()->append(v);
}

void
JsonPropertyStream::add(double v)
{
    stack.back()->append(v);
}

void
JsonPropertyStream::add(std::string const& v)
{
    stack.back()->append(v);
}

}  // namespace xrpl
