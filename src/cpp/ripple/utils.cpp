
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

#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/foreach.hpp>
#include <boost/regex.hpp>
#include <boost/test/unit_test.hpp>

#include <openssl/rand.h>

#include "utils.h"
#include "uint256.h"

void getRand(unsigned char *buf, int num)
{
#ifdef PURIFY
	memset(buf, 0, num);
#endif
	if (RAND_bytes(buf, num) != 1)
	{
		assert(false);
		throw std::runtime_error("Entropy pool not seeded");
	}
}

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
// Hex suport
//

char charHex(int iDigit)
{
	return iDigit < 10 ? '0' + iDigit : 'A' - 10 + iDigit;
}

int charUnHex(char cDigit)
{
	return cDigit >= '0' && cDigit <= '9'
		? cDigit - '0'
		: cDigit >= 'A' && cDigit <= 'F'
			? cDigit - 'A' + 10
			: cDigit >= 'a' && cDigit <= 'f'
				? cDigit - 'a' + 10
				: -1;
}

int strUnHex(std::string& strDst, const std::string& strSrc)
{
	int	iBytes	= (strSrc.size()+1)/2;

	strDst.resize(iBytes);

	const char*	pSrc	= &strSrc[0];
	char*		pDst	= &strDst[0];

	if (strSrc.size() & 1)
	{
		int		c	= charUnHex(*pSrc++);

		if (c < 0)
		{
			iBytes	= -1;
		}
		else
		{
			*pDst++	= c;
		}
	}

	for (int i=0; iBytes >= 0 && i != iBytes; i++)
	{
		int		cHigh	= charUnHex(*pSrc++);
		int		cLow	= charUnHex(*pSrc++);

		if (cHigh < 0 || cLow < 0)
		{
			iBytes	= -1;
		}
		else
		{
			strDst[i]	= (cHigh << 4) | cLow;
		}
	}

	if (iBytes < 0)
		strDst.clear();

	return iBytes;
}

std::vector<unsigned char> strUnHex(const std::string& strSrc)
{
	std::string	strTmp;

	strUnHex(strTmp, strSrc);

	return strCopy(strTmp);
}

uint64_t uintFromHex(const std::string& strSrc)
{
	uint64_t	uValue = 0;

	BOOST_FOREACH(char c, strSrc)
		uValue = (uValue << 4) | charUnHex(c);

	return uValue;
}

//
// Misc string
//

std::vector<unsigned char> strCopy(const std::string& strSrc)
{
	std::vector<unsigned char> vucDst;

	vucDst.resize(strSrc.size());

	std::copy(strSrc.begin(), strSrc.end(), vucDst.begin());

	return vucDst;
}

std::string strCopy(const std::vector<unsigned char>& vucSrc)
{
	std::string strDst;

	strDst.resize(vucSrc.size());

	std::copy(vucSrc.begin(), vucSrc.end(), strDst.begin());

	return strDst;

}

extern std::string urlEncode(const std::string& strSrc)
{
	std::string	strDst;
	int			iOutput	= 0;
	int			iSize	= strSrc.length();

	strDst.resize(iSize*3);

	for (int iInput = 0; iInput < iSize; iInput++) {
		unsigned char c	= strSrc[iInput];

		if (c == ' ')
		{
			strDst[iOutput++]	= '+';
		}
		else if (isalnum(c))
		{
			strDst[iOutput++]	= c;
		}
		else
		{
			strDst[iOutput++]	= '%';
			strDst[iOutput++]	= charHex(c >> 4);
			strDst[iOutput++]	= charHex(c & 15);
		}
	}

	strDst.resize(iOutput);

	return strDst;
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

//
// IP Port parsing
//
// <-- iPort: "" = -1
bool parseIpPort(const std::string& strSource, std::string& strIP, int& iPort)
{
	boost::smatch	smMatch;
	bool			bValid	= false;

	static boost::regex	reEndpoint("\\`\\s*(\\S+)(?:\\s+(\\d+))?\\s*\\'");

	if (boost::regex_match(strSource, smMatch, reEndpoint))
	{
		boost::system::error_code	err;
		std::string					strIPRaw	= smMatch[1];
		std::string					strPortRaw	= smMatch[2];

		boost::asio::ip::address	addrIP		= boost::asio::ip::address::from_string(strIPRaw, err);

		bValid	= !err;
		if (bValid)
		{
			strIP	= addrIP.to_string();
			iPort	= strPortRaw.empty() ? -1 : boost::lexical_cast<int>(strPortRaw);
		}
	}

	return bValid;
}

bool parseUrl(const std::string& strUrl, std::string& strScheme, std::string& strDomain, int& iPort, std::string& strPath)
{
	// scheme://username:password@hostname:port/rest
	static boost::regex	reUrl("(?i)\\`\\s*([[:alpha:]][-+.[:alpha:][:digit:]]*)://([^:/]+)(?::(\\d+))?(/.*)?\\s*?\\'");
	boost::smatch	smMatch;

	bool	bMatch	= boost::regex_match(strUrl, smMatch, reUrl);			// Match status code.

	if (bMatch)
	{
		std::string	strPort;

		strScheme	= smMatch[1];
		strDomain	= smMatch[2];
		strPort		= smMatch[3];
		strPath		= smMatch[4];

		boost::algorithm::to_lower(strScheme);

		iPort	= strPort.empty() ? -1 : lexical_cast_s<int>(strPort);
		// std::cerr << strUrl << " : " << bMatch << " : '" << strDomain << "' : '" << strPort << "' : " << iPort << " : '" << strPath << "'" << std::endl;
	}
	// std::cerr << strUrl << " : " << bMatch << " : '" << strDomain << "' : '" << strPath << "'" << std::endl;

	return bMatch;
}

//
// Quality parsing
// - integers as is.
// - floats multiplied by a billion
bool parseQuality(const std::string& strSource, uint32& uQuality)
{
	uQuality	= lexical_cast_s<uint32>(strSource);

	if (!uQuality)
	{
		float	fQuality	= lexical_cast_s<float>(strSource);

		if (fQuality)
			uQuality	= (uint32)(QUALITY_ONE*fQuality);
	}

	return !!uQuality;
}

/*
void intIPtoStr(int ip,std::string& retStr)
{
	unsigned char bytes[4];
	bytes[0] = ip & 0xFF;
	bytes[1] = (ip >> 8) & 0xFF;
	bytes[2] = (ip >> 16) & 0xFF;
	bytes[3] = (ip >> 24) & 0xFF;

	retStr=str(boost::format("%d.%d.%d.%d") % bytes[3] % bytes[2] % bytes[1] % bytes[0] );
}

int strIPtoInt(std::string& ipStr)
{

}
*/
#ifdef WIN32

//#include "Winsock2.h"
//#include <windows.h>
// from: http://stackoverflow.com/questions/3022552/is-there-any-standard-htonl-like-function-for-64-bits-integers-in-c
// but we don't need to check the endianness
uint64_t htobe64(uint64_t value)
{
	// The answer is 42
	//static const int num = 42;

	// Check the endianness
	//if (*reinterpret_cast<const char*>(&num) == num)
	//{
	const uint32_t high_part = htonl(static_cast<uint32_t>(value >> 32));
	const uint32_t low_part = htonl(static_cast<uint32_t>(value & 0xFFFFFFFFLL));

	return (static_cast<uint64_t>(low_part) << 32) | high_part;
	//} else
	//{
	//	return value;
	//}
}

uint64_t be64toh(uint64_t value)
{
	return(_byteswap_uint64(value));
}

uint32_t htobe32(uint32_t value)
{
	return(htonl(value));
}

uint32_t be32toh(uint32_t value)
{
	return( _byteswap_ulong(value));
}

#endif

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

BOOST_AUTO_TEST_SUITE( Utils)

BOOST_AUTO_TEST_CASE( ParseUrl )
{
	std::string	strScheme;
	std::string	strDomain;
	int			iPort;
	std::string	strPath;

	if (!parseUrl("lower://domain", strScheme, strDomain, iPort, strPath))
		BOOST_FAIL("parseUrl: lower://domain failed");

	if (strScheme != "lower")
		BOOST_FAIL("parseUrl: lower://domain : scheme failed");

	if (strDomain != "domain")
		BOOST_FAIL("parseUrl: lower://domain : domain failed");

	if (iPort != -1)
		BOOST_FAIL("parseUrl: lower://domain : port failed");

	if (strPath != "")
		BOOST_FAIL("parseUrl: lower://domain : path failed");

	if (!parseUrl("UPPER://domain:234/", strScheme, strDomain, iPort, strPath))
		BOOST_FAIL("parseUrl: UPPER://domain:234/ failed");

	if (strScheme != "upper")
		BOOST_FAIL("parseUrl: UPPER://domain:234/ : scheme failed");

	if (iPort != 234)
		BOOST_FAIL(boost::str(boost::format("parseUrl: UPPER://domain:234/ : port failed: %d") % iPort));

	if (strPath != "/")
		BOOST_FAIL("parseUrl: UPPER://domain:234/ : path failed");

	if (!parseUrl("Mixed://domain/path", strScheme, strDomain, iPort, strPath))
		BOOST_FAIL("parseUrl: Mixed://domain/path failed");

	if (strScheme != "mixed")
		BOOST_FAIL("parseUrl: Mixed://domain/path tolower failed");

	if (strPath != "/path")
		BOOST_FAIL("parseUrl: Mixed://domain/path path failed");
}

BOOST_AUTO_TEST_SUITE_END()

// vim:ts=4
