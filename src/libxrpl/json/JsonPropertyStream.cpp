#include <xrpl/json/JsonPropertyStream.h>

#include <xrpl/json/json_value.h>

#include <string>

namespace xrpl {

JsonPropertyStream::JsonPropertyStream() : topValue(Json::ObjectValue)
{
    stack.reserve(64);
    stack.push_back(&topValue);
}

Json::Value const&
JsonPropertyStream::top() const
{
    return topValue;
}

void
JsonPropertyStream::mapBegin()
{
    // top is array
    Json::Value& top(*stack.back());
    Json::Value& map(top.append(Json::ObjectValue));
    stack.push_back(&map);
}

void
JsonPropertyStream::mapBegin(std::string const& key)
{
    // top is a map
    Json::Value& top(*stack.back());
    Json::Value& map(top[key] = Json::ObjectValue);
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
    Json::Value& top(*stack.back());
    Json::Value& vec(top.append(Json::ArrayValue));
    stack.push_back(&vec);
}

void
JsonPropertyStream::arrayBegin(std::string const& key)
{
    // top is a map
    Json::Value& top(*stack.back());
    Json::Value& vec(top[key] = Json::ArrayValue);
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
