//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace basio
{

IoService* IoService::New (std::size_t concurrency_hint)
{
    return new IoService (concurrency_hint);
}

IoService::~IoService ()
{
}

IoService::operator boost::asio::io_service& ()
{
    return *m_impl;
}

IoService::IoService (std::size_t concurrency_hint)
    : m_impl (new boost::asio::io_service (concurrency_hint))
{
}

void IoService::stop ()
{
    m_impl->stop ();
}

bool IoService::stopped ()
{
    return m_impl->stopped ();
}

void IoService::run ()
{
    m_impl->run ();
}

}
