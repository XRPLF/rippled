#include <xrpl/json/JsonPropertyStream.h>

#include <xrpl/json/json_value.h>

#include <string>

namespace xrpl {

JsonPropertyStream::JsonPropertyStream() : topValue(json::ValueType::Object)
{
    stack.reserve(64);
    stack.push_back(&topValue);
}

json::Value const&
JsonPropertyStream::top() const
{
    return topValue;
}

void
JsonPropertyStream::mapBegin()
{
    // top is array
    json::Value& top(*stack.back());
    json::Value& map(top.append(json::ValueType::Object));
    stack.push_back(&map);
}

void
JsonPropertyStream::mapBegin(std::string const& key)
{
    // top is a map
    json::Value& top(*stack.back());
    json::Value& map(top[key] = json::ValueType::Object);
    stack.push_back(&map);
}

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

void
JsonPropertyStream::arrayBegin()
{
    // top is array
    json::Value& top(*stack.back());
    json::Value& vec(top.append(json::ValueType::Array));
    stack.push_back(&vec);
}

void
JsonPropertyStream::arrayBegin(std::string const& key)
{
    // top is a map
    json::Value& top(*stack.back());
    json::Value& vec(top[key] = json::ValueType::Array);
    stack.push_back(&vec);
}

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
