//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TIME_H
#define RIPPLE_TIME_H

boost::posix_time::ptime ptEpoch ();
int iToSeconds (boost::posix_time::ptime ptWhen);
boost::posix_time::ptime ptFromSeconds (int iSeconds);
uint64_t utFromSeconds (int iSeconds);

#endif
