#include <xrpl/json/JsonPropertyStream.h>

#include <xrpl/json/json_value.h>
#include <xrpl/basics/TraceLog.h>

#include <string>

namespace xrpl {

JsonPropertyStream::JsonPropertyStream() : topValue(json::ObjectValue)
{
    TRACE_FUNC();
    stack.reserve(64);
    stack.push_back(&topValue);
}

json::Value const&
JsonPropertyStream::top() const
{
    TRACE_FUNC();
    return topValue;
}

void
JsonPropertyStream::mapBegin()
{
    TRACE_FUNC();
    // top is array
    json::Value& top(*stack.back());
    json::Value& map(top.append(json::ObjectValue));
    stack.push_back(&map);
}

void
JsonPropertyStream::mapBegin(std::string const& key)
{
    TRACE_FUNC();
    // top is a map
    json::Value& top(*stack.back());
    json::Value& map(top[key] = json::ObjectValue);
    stack.push_back(&map);
}

void
JsonPropertyStream::mapEnd()
{
    TRACE_FUNC();
    stack.pop_back();
}

void
JsonPropertyStream::add(std::string const& key, short v)
{
    TRACE_FUNC();
    (*stack.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, unsigned short v)
{
    TRACE_FUNC();
    (*stack.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, int v)
{
    TRACE_FUNC();
    (*stack.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, unsigned int v)
{
    TRACE_FUNC();
    (*stack.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, long v)
{
    TRACE_FUNC();
    (*stack.back())[key] = int(v);
}

void
JsonPropertyStream::add(std::string const& key, float v)
{
    TRACE_FUNC();
    (*stack.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, double v)
{
    TRACE_FUNC();
    (*stack.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, std::string const& v)
{
    TRACE_FUNC();
    (*stack.back())[key] = v;
}

void
JsonPropertyStream::arrayBegin()
{
    TRACE_FUNC();
    // top is array
    json::Value& top(*stack.back());
    json::Value& vec(top.append(json::ArrayValue));
    stack.push_back(&vec);
}

void
JsonPropertyStream::arrayBegin(std::string const& key)
{
    TRACE_FUNC();
    // top is a map
    json::Value& top(*stack.back());
    json::Value& vec(top[key] = json::ArrayValue);
    stack.push_back(&vec);
}

void
JsonPropertyStream::arrayEnd()
{
    TRACE_FUNC();
    stack.pop_back();
}

void
JsonPropertyStream::add(short v)
{
    TRACE_FUNC();
    stack.back()->append(v);
}

void
JsonPropertyStream::add(unsigned short v)
{
    TRACE_FUNC();
    stack.back()->append(v);
}

void
JsonPropertyStream::add(int v)
{
    TRACE_FUNC();
    stack.back()->append(v);
}

void
JsonPropertyStream::add(unsigned int v)
{
    TRACE_FUNC();
    stack.back()->append(v);
}

void
JsonPropertyStream::add(long v)
{
    TRACE_FUNC();
    stack.back()->append(int(v));
}

void
JsonPropertyStream::add(float v)
{
    TRACE_FUNC();
    stack.back()->append(v);
}

void
JsonPropertyStream::add(double v)
{
    TRACE_FUNC();
    stack.back()->append(v);
}

void
JsonPropertyStream::add(std::string const& v)
{
    TRACE_FUNC();
    stack.back()->append(v);
}

}  // namespace xrpl
