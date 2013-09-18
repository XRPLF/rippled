//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_APP_IOSERVICEPOOL_H_INCLUDED
#define RIPPLE_APP_IOSERVICEPOOL_H_INCLUDED

/** An io_service with an associated group of threads. */
class IoServicePool : public Service
{
public:
    IoServicePool (Service& parent, String const& name, int numberOfThreads);
    ~IoServicePool ();

    void runAsync ();

    boost::asio::io_service& getService ();
    operator boost::asio::io_service& ();

    void onServiceStop ();
    void onServiceChildrenStopped ();

private:
    class ServiceThread;

    void onThreadExit();

    String m_name;
    boost::asio::io_service m_service;
    boost::optional <boost::asio::io_service::work> m_work;
    OwnedArray <ServiceThread> m_threads;
    Atomic <int> m_threadsRunning;
    bool m_running;
};

#endif
