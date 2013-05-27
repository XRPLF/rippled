
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
			setCallingThreadName("main");
			signal(SIGINT, SIG_DFL);
			signal(SIGHUP, SIG_DFL);
			signal(SIGUSR1, SIG_DFL);
			signal(SIGUSR2, SIG_DFL);
			return str(boost::format("Launching child %d") % childCount);;
		}
		setCallingThreadName(boost::str(boost::format("#%d") % childCount).c_str());
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
