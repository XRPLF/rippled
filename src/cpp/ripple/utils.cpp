
#ifdef __linux__
#include <sys/types.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#endif

#ifdef __FreeBSD__
#include <sys/types.h>
#include <sys/wait.h>
#endif

#ifdef WIN32
#define _WINSOCK_
#include <winsock2.h>
#endif

#include <fstream>

//#include <boost/algorithm/string.hpp>
//#include <boost/asio.hpp>
#include <boost/foreach.hpp>
//#include <boost/regex.hpp>
//#include <boost/test/unit_test.hpp>

//#include <openssl/rand.h>

#include "utils.h"
#include "uint256.h"

//
// Time support
// We have our own epoch.
//

boost::posix_time::ptime ptEpoch()
{
	return boost::posix_time::ptime(boost::gregorian::date(2000, boost::gregorian::Jan, 1));
}

int iToSeconds(boost::posix_time::ptime ptWhen)
{
	return ptWhen.is_not_a_date_time()
		? -1
		: (ptWhen-ptEpoch()).total_seconds();
}

// Convert our time in seconds to a ptime.
boost::posix_time::ptime ptFromSeconds(int iSeconds)
{
	return iSeconds < 0
		? boost::posix_time::ptime(boost::posix_time::not_a_date_time)
		: ptEpoch() + boost::posix_time::seconds(iSeconds);
}

// Convert from our time to UNIX time in seconds.
uint64_t utFromSeconds(int iSeconds)
{
	boost::posix_time::time_duration	tdDelta	=
		boost::posix_time::ptime(boost::gregorian::date(2000, boost::gregorian::Jan, 1))
		-boost::posix_time::ptime(boost::gregorian::date(1970, boost::gregorian::Jan, 1))
		+boost::posix_time::seconds(iSeconds)
		;

	return tdDelta.total_seconds();
}

//
// DH support
//

std::string DH_der_gen(int iKeyLength)
{
	DH*			dh	= 0;
	int			iCodes;
	std::string strDer;

	do {
		dh	= DH_generate_parameters(iKeyLength, DH_GENERATOR_5, NULL, NULL);
		iCodes	= 0;
		DH_check(dh, &iCodes);
	} while (iCodes & (DH_CHECK_P_NOT_PRIME|DH_CHECK_P_NOT_SAFE_PRIME|DH_UNABLE_TO_CHECK_GENERATOR|DH_NOT_SUITABLE_GENERATOR));

	strDer.resize(i2d_DHparams(dh, NULL));

	unsigned char* next	= reinterpret_cast<unsigned char *>(&strDer[0]);

	(void) i2d_DHparams(dh, &next);

	return strDer;
}

DH* DH_der_load(const std::string& strDer)
{
	const unsigned char *pbuf	= reinterpret_cast<const unsigned char *>(&strDer[0]);

	return d2i_DHparams(NULL, &pbuf, strDer.size());
}

#ifdef PR_SET_NAME
#define HAVE_NAME_THREAD
extern void NameThread(const char* n)
{
	static std::string pName;

	if (pName.empty())
	{
		std::ifstream cLine("/proc/self/cmdline", std::ios::in);
		cLine >> pName;
		if (pName.empty())
			pName = "rippled";
		else
		{
			size_t zero = pName.find_first_of('\0');
			if ((zero != std::string::npos) && (zero != 0))
				pName = pName.substr(0, zero);
			size_t slash = pName.find_last_of('/');
			if (slash != std::string::npos)
				pName = pName.substr(slash + 1);
		}
		pName += " ";
	}

	prctl(PR_SET_NAME, (pName + n).c_str(), 0, 0, 0);
}
#endif

#ifndef HAVE_NAME_THREAD
extern void NameThread(const char*)
{ ; }
#endif

#ifdef __unix__

static pid_t pManager = static_cast<pid_t>(0);
static pid_t pChild = static_cast<pid_t>(0);

static void pass_signal(int a)
{
	kill(pChild, a);
}

static void stop_manager(int)
{
	kill(pChild, SIGINT);
	_exit(0);
}

bool HaveSustain()
{
	return true;
}

std::string StopSustain()
{
	if (getppid() != pManager)
		return std::string();
	kill(pManager, SIGHUP);
	return "Terminating monitor";
}

std::string DoSustain()
{
	int childCount = 0;
	pManager = getpid();
	signal(SIGINT, stop_manager);
	signal(SIGHUP, stop_manager);
	signal(SIGUSR1, pass_signal);
	signal(SIGUSR2, pass_signal);
	while (1)
	{
		++childCount;
		pChild = fork();
		if (pChild == -1)
			_exit(0);
		if (pChild == 0)
		{
			NameThread("main");
			signal(SIGINT, SIG_DFL);
			signal(SIGHUP, SIG_DFL);
			signal(SIGUSR1, SIG_DFL);
			signal(SIGUSR2, SIG_DFL);
			return str(boost::format("Launching child %d") % childCount);;
		}
		NameThread(boost::str(boost::format("#%d") % childCount).c_str());
		do
		{
			int i;
			sleep(10);
			waitpid(-1, &i, 0);
		}
		while (kill(pChild, 0) == 0);
		rename("core", boost::str(boost::format("core.%d") % static_cast<int>(pChild)).c_str());
		rename("debug.log", boost::str(boost::format("debug.log.%d") % static_cast<int>(pChild)).c_str());
	}
}

#else

bool HaveSustain()			{ return false; }
std::string DoSustain()		{ return std::string(); }
std::string StopSustain()	{ return std::string(); }

#endif

// vim:ts=4
