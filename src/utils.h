#ifndef __UTILS__
#define __UTILS__

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/format.hpp>

#define nothing()   do {} while (0)

boost::posix_time::ptime ptEpoch();
int iToSeconds(boost::posix_time::ptime ptWhen);
boost::posix_time::ptime ptFromSeconds(int iSeconds);

template<class Iterator>
std::string strJoin(Iterator first, Iterator last, std::string strSeperator)
{
	std::ostringstream  ossValues;

	for (Iterator start = first; first != last; first++)
	{
		ossValues << str(boost::format("%s%s") % (start == first ? "" : strSeperator) % *first);
	}

	return ossValues.str();
}
#endif

// vim:ts=4
