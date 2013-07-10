//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_IOSERVICE_H_INCLUDED
#define RIPPLE_IOSERVICE_H_INCLUDED

namespace basio
{

/** Hides a boost::asio::ioservice implementation.
*/
class IoService
{
public:
    static IoService* New (std::size_t concurrency_hint);

    virtual ~IoService ();

    operator boost::asio::io_service& ();

    void stop ();
    bool stopped ();
    void run ();

private:
    explicit IoService (std::size_t concurrency_hint);

private:
    beast::ScopedPointer <boost::asio::io_service> m_impl;
};

}

#endif
