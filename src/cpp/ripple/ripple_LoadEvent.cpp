
LoadEvent::LoadEvent (LoadMonitor& monitor, const std::string& name, bool shouldStart)
    : mMonitor (monitor)
    , mRunning (false)
    , mName (name)
{
    mStartTime = boost::posix_time::microsec_clock::universal_time ();

    if (shouldStart)
        start ();
}

LoadEvent::~LoadEvent ()
{
    if (mRunning)
        stop ();
}

void LoadEvent::reName (const std::string& name)
{
    mName = name;
}

void LoadEvent::start ()
{
    mRunning = true;
    mStartTime = boost::posix_time::microsec_clock::universal_time ();
}

void LoadEvent::stop ()
{
    assert (mRunning);

    mRunning = false;
    mMonitor.addCountAndLatency (mName,
                                 static_cast<int> ((boost::posix_time::microsec_clock::universal_time () - mStartTime).total_milliseconds ()));
}
