//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// VFALCO TODO Tidy this up into a RippleTime object

//
// Time support
// We have our own epoch.
//

boost::posix_time::ptime ptEpoch ()
{
    return boost::posix_time::ptime (boost::gregorian::date (2000, boost::gregorian::Jan, 1));
}

int iToSeconds (boost::posix_time::ptime ptWhen)
{
    return ptWhen.is_not_a_date_time ()
           ? -1
           : (ptWhen - ptEpoch ()).total_seconds ();
}

// Convert our time in seconds to a ptime.
boost::posix_time::ptime ptFromSeconds (int iSeconds)
{
    return iSeconds < 0
           ? boost::posix_time::ptime (boost::posix_time::not_a_date_time)
           : ptEpoch () + boost::posix_time::seconds (iSeconds);
}

// Convert from our time to UNIX time in seconds.
uint64_t utFromSeconds (int iSeconds)
{
    boost::posix_time::time_duration    tdDelta =
        boost::posix_time::ptime (boost::gregorian::date (2000, boost::gregorian::Jan, 1))
        - boost::posix_time::ptime (boost::gregorian::date (1970, boost::gregorian::Jan, 1))
        + boost::posix_time::seconds (iSeconds)
        ;

    return tdDelta.total_seconds ();
}
