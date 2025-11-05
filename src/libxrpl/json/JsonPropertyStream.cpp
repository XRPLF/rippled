#include <xrpl/json/JsonPropertyStream.h>
#include <xrpl/json/json_value.h>

#include <string>

namespace ripple {

JsonPropertyStream::JsonPropertyStream() : m_top(Json::objectValue)
{
    m_stack.reserve(64);
    m_stack.push_back(&m_top);
}

Json::Value const&
JsonPropertyStream::top() const
{
    return m_top;
}

void
JsonPropertyStream::map_begin()
{
    // top is array
    Json::Value& top(*m_stack.back());
    Json::Value& map(top.append(Json::objectValue));
    m_stack.push_back(&map);
}

void
JsonPropertyStream::map_begin(std::string const& key)
{
    // top is a map
    Json::Value& top(*m_stack.back());
    Json::Value& map(top[key] = Json::objectValue);
    m_stack.push_back(&map);
}

void
JsonPropertyStream::map_end()
{
    m_stack.pop_back();
}

void
JsonPropertyStream::add(std::string const& key, short v)
{
    (*m_stack.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, unsigned short v)
{
    (*m_stack.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, int v)
{
    (*m_stack.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, unsigned int v)
{
    (*m_stack.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, long v)
{
    (*m_stack.back())[key] = int(v);
}

void
JsonPropertyStream::add(std::string const& key, float v)
{
    (*m_stack.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, double v)
{
    (*m_stack.back())[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, std::string const& v)
{
    (*m_stack.back())[key] = v;
}

void
JsonPropertyStream::array_begin()
{
    // top is array
    Json::Value& top(*m_stack.back());
    Json::Value& vec(top.append(Json::arrayValue));
    m_stack.push_back(&vec);
}

void
JsonPropertyStream::array_begin(std::string const& key)
{
    // top is a map
    Json::Value& top(*m_stack.back());
    Json::Value& vec(top[key] = Json::arrayValue);
    m_stack.push_back(&vec);
}

void
JsonPropertyStream::array_end()
{
    m_stack.pop_back();
}

void
JsonPropertyStream::add(short v)
{
    m_stack.back()->append(v);
}

void
JsonPropertyStream::add(unsigned short v)
{
    m_stack.back()->append(v);
}

void
JsonPropertyStream::add(int v)
{
    m_stack.back()->append(v);
}

void
JsonPropertyStream::add(unsigned int v)
{
    m_stack.back()->append(v);
}

void
JsonPropertyStream::add(long v)
{
    m_stack.back()->append(int(v));
}

void
JsonPropertyStream::add(float v)
{
    m_stack.back()->append(v);
}

void
JsonPropertyStream::add(double v)
{
    m_stack.back()->append(v);
}

void
JsonPropertyStream::add(std::string const& v)
{
    m_stack.back()->append(v);
}

}  // namespace ripple
