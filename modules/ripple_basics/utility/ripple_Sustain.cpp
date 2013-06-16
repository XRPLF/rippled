//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifdef __unix__

static pid_t pManager = static_cast<pid_t> (0);
static pid_t pChild = static_cast<pid_t> (0);

static void pass_signal (int a)
{
    kill (pChild, a);
}

static void stop_manager (int)
{
    kill (pChild, SIGINT);
    _exit (0);
}

bool HaveSustain ()
{
    return true;
}

std::string StopSustain ()
{
    if (getppid () != pManager)
        return std::string ();

    kill (pManager, SIGHUP);
    return "Terminating monitor";
}

std::string DoSustain ()
{
    int childCount = 0;
    pManager = getpid ();
    signal (SIGINT, stop_manager);
    signal (SIGHUP, stop_manager);
    signal (SIGUSR1, pass_signal);
    signal (SIGUSR2, pass_signal);

    while (1)
    {
        ++childCount;
        pChild = fork ();

        if (pChild == -1)
            _exit (0);

        if (pChild == 0)
        {
            setCallingThreadName ("main");
            signal (SIGINT, SIG_DFL);
            signal (SIGHUP, SIG_DFL);
            signal (SIGUSR1, SIG_DFL);
            signal (SIGUSR2, SIG_DFL);
            return str (boost::format ("Launching child %d") % childCount);;
        }

        setCallingThreadName (boost::str (boost::format ("#%d") % childCount).c_str ());

        do
        {
            int i;
            sleep (10);
            waitpid (-1, &i, 0);
        }
        while (kill (pChild, 0) == 0);

        rename ("core", boost::str (boost::format ("core.%d") % static_cast<int> (pChild)).c_str ());
        rename ("debug.log", boost::str (boost::format ("debug.log.%d") % static_cast<int> (pChild)).c_str ());
    }
}

#else

bool HaveSustain ()
{
    return false;
}
std::string DoSustain ()
{
    return std::string ();
}
std::string StopSustain ()
{
    return std::string ();
}

#endif
