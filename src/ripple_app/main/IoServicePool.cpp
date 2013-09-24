//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class IoServicePool::ServiceThread : private Thread
{
public:
    explicit ServiceThread (
        String const& name,
        IoServicePool& owner,
        boost::asio::io_service& service)
        : Thread (name)
        , m_owner (owner)
        , m_service (service)
    {
        startThread ();
    }

    ~ServiceThread ()
    {
        // block until thread exits
        stopThread ();
    }

    void start ()
    {
        startThread ();
    }

    void run ()
    {
        m_service.run ();

        m_owner.onThreadExit();
    }

private:
    IoServicePool& m_owner;
    boost::asio::io_service& m_service;
};

//------------------------------------------------------------------------------

IoServicePool::IoServicePool (Stoppable& parent, String const& name, int numberOfThreads)
    : Stoppable (name.toStdString().c_str(), parent)
    , m_name (name)
    , m_service (numberOfThreads)
    , m_work (boost::ref (m_service))
{
    bassert (numberOfThreads > 0);

    m_threads.ensureStorageAllocated (numberOfThreads);

    for (int i = 0; i < numberOfThreads; ++i)
    {
        ++m_threadsRunning;
        m_threads.add (new ServiceThread (m_name, *this, m_service));
        m_threads[i]->start ();
    }
}

IoServicePool::~IoServicePool ()
{
    // the dtor of m_threads will block until each thread exits.
}

boost::asio::io_service& IoServicePool::getService ()
{
    return m_service;
}

IoServicePool::operator boost::asio::io_service& ()
{
    return m_service;
}

void IoServicePool::onStop ()
{
    // VFALCO NOTE This is a hack! We should gracefully
    //             cancel all pending I/O, and delete the work
    //             object using boost::optional, and let run()
    //             just return naturally.
    //
    m_service.stop ();
}

void IoServicePool::onChildrenStopped ()
{
}

// Called every time io_service::run() returns and a thread will exit.
//
void IoServicePool::onThreadExit()
{
    // service must be stopping for threads to exit.
    bassert (isStopping());

    // must have at least count 1
    bassert (m_threadsRunning.get() > 0);

    if (--m_threadsRunning == 0)
    {
        // last thread just exited
        stopped ();
    }
}
