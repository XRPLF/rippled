#ifndef __UTILS__
#define __UTILS__

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/format.hpp>

#include <openssl/dh.h>

#include "types.h"

#define nothing()			do {} while (0)
#define fallthru()			do {} while (0)
#define NUMBER(x)			(sizeof(x)/sizeof((x)[0]))
#define ADDRESS(p)			strHex(uint64( ((char*) p) - ((char*) 0)))
#define ADDRESS_SHARED(p)	strHex(uint64( ((char*) (p).get()) - ((char*) 0)))

#ifndef MAX
#define MAX(x,y) ((x) < (y) ? (y) : (x))
#endif

#ifndef MIN
#define MIN(x,y) ((x) > (y) ? (y) : (x))
#endif

#ifdef WIN32
extern uint64_t htobe64(uint64_t value);
#endif

boost::posix_time::ptime ptEpoch();
int iToSeconds(boost::posix_time::ptime ptWhen);
boost::posix_time::ptime ptFromSeconds(int iSeconds);

/*
void intIPtoStr(int ip,std::string& retStr);
int strIPtoInt(std::string& ipStr);
*/

template<class Iterator>
std::string strJoin(Iterator first, Iterator last, std::string strSeperator)
{
	std::ostringstream	ossValues;

	for (Iterator start = first; first != last; first++)
	{
		ossValues << str(boost::format("%s%s") % (start == first ? "" : strSeperator) % *first);
	}

	return ossValues.str();
}

char charHex(int iDigit);

template<class Iterator>
std::string strHex(Iterator first, int iSize)
{
	std::string		strDst;

	strDst.resize(iSize*2);

	for (int i = 0; i < iSize; i++) {
		unsigned char c	= *first++;

		strDst[i*2]		= charHex(c >> 4);
		strDst[i*2+1]	= charHex(c & 15);
	}

	return strDst;
}

inline const std::string strHex(const std::string& strSrc)
{
	return strHex(strSrc.begin(), strSrc.size());
}

inline std::string strHex(const std::vector<unsigned char>& vucData)
{
	return strHex(vucData.begin(), vucData.size());
}

inline std::string strHex(const uint64 uiHost)
{
	uint64_t	uBig	= htobe64(uiHost);

	return strHex((unsigned char*) &uBig, sizeof(uBig));
}

inline static std::string sqlEscape(const std::string& strSrc)
{
	return str(boost::format("X'%s'") % strHex(strSrc));
}

template<class Iterator>
bool isZero(Iterator first, int iSize)
{
	while (iSize && !*first++)
		--iSize;

	return !iSize;
}

int charUnHex(char cDigit);
void strUnHex(std::string& strDst, const std::string& strSrc);

std::vector<unsigned char> strUnHex(const std::string& strSrc);

std::vector<unsigned char> strCopy(const std::string& strSrc);
std::string strCopy(const std::vector<unsigned char>& vucSrc);

bool parseIpPort(const std::string& strSource, std::string& strIP, int& iPort);

DH* DH_der_load(const std::string& strDer);
std::string DH_der_gen(int iKeyLength);

inline std::string strGetEnv(const std::string& strKey)
{
	return getenv(strKey.c_str()) ? getenv(strKey.c_str()) : "";
}
#endif

// vim:ts=4
