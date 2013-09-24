//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace ripple {
namespace RPC {

Service::Service ()
{
}

Service::~Service ()
{
}

Handlers const& Service::handlers() const
{
    return m_handlers;
}

}
}
