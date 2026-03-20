#include <xrpl/json/JsonPropertyStream.h>
#include <xrpl/json/json_value.h>

#include <string>

namespace xrpl {

JsonPropertyStream::JsonPropertyStream() : top_(Json::objectValue)
{
    stack_.reserve(64);
    stack_.push_back(&top_);
}

Json::Value const&
JsonPropertyStream::top() const
{
    return top_;
}

void
JsonPropertyStream::mapBegin()
{
    // top is array
    Json::Value& top(*stack_.back());
    Json::Value& map(top.append(Json::objectValue));
    stack_.push_back(&map);
}

void
JsonPropertyStream::mapBegin(std::string const& key)
{
    // top is a map
    Json::Value& top(*stack_.back());
    Json::Value& map(top[key] = Json::objectValue);
    stack_.push_back(&map);
}

void
JsonPropertyStream::mapEnd()
{
    stack_.pop_back();
}

void
JsonPropertyStream::add(std::string const& key, short v)
{
    (*stack_.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, unsigned short v)
{
    (*stack_.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, int v)
{
    (*stack_.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, unsigned int v)
{
    (*stack_.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, long v)
{
    (*stack_.back())[key] = int(v);
}

void
JsonPropertyStream::add(std::string const& key, float v)
{
    (*stack_.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, double v)
{
    (*stack_.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, std::string const& v)
{
    (*stack_.back())[key] = v;
}

void
JsonPropertyStream::arrayBegin()
{
    // top is array
    Json::Value& top(*stack_.back());
    Json::Value& vec(top.append(Json::arrayValue));
    stack_.push_back(&vec);
}

void
JsonPropertyStream::arrayBegin(std::string const& key)
{
    // top is a map
    Json::Value& top(*stack_.back());
    Json::Value& vec(top[key] = Json::arrayValue);
    stack_.push_back(&vec);
}

void
JsonPropertyStream::arrayEnd()
{
    stack_.pop_back();
}

void
JsonPropertyStream::add(short v)
{
    stack_.back()->append(v);
}

void
JsonPropertyStream::add(unsigned short v)
{
    stack_.back()->append(v);
}

void
JsonPropertyStream::add(int v)
{
    stack_.back()->append(v);
}

void
JsonPropertyStream::add(unsigned int v)
{
    stack_.back()->append(v);
}

void
JsonPropertyStream::add(long v)
{
    stack_.back()->append(int(v));
}

void
JsonPropertyStream::add(float v)
{
    stack_.back()->append(v);
}

void
JsonPropertyStream::add(double v)
{
    stack_.back()->append(v);
}

void
JsonPropertyStream::add(std::string const& v)
{
    stack_.back()->append(v);
}

}  // namespace xrpl
