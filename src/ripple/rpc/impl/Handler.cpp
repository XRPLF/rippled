//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace ripple {
namespace RPC {

Handler::Handler (Handler const& other)
    : m_method (other.m_method)
    , m_function (other.m_function)
{
}

Handler& Handler::operator= (Handler const& other)
{
    m_method = other.m_method;
    m_function = other.m_function;
    return *this;
}

std::string const& Handler::method() const
{
    return m_method;
}

Json::Value Handler::operator() (Json::Value const& args) const
{
    return m_function (args);
}

}
}
