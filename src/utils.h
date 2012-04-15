#ifndef __UTILS__
#define __UTILS__

#include <boost/date_time/posix_time/posix_time.hpp>

#define nothing()   do {} while (0)

boost::posix_time::ptime ptEpoch();
int iToSeconds(boost::posix_time::ptime ptWhen);
boost::posix_time::ptime ptFromSeconds(int iSeconds);

#endif
