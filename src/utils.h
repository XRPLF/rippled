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
extern uint64_t be64toh(uint64_t value);
#endif

#define vt_f_black          "\033[30m"
#define vt_f_red            "\033[31m"
#define vt_f_green          "\033[32m"
#define vt_f_yellow         "\033[33m"
#define vt_f_blue           "\033[34m"
#define vt_f_megenta        "\033[35m"
#define vt_f_cyan           "\033[36m"
#define vt_f_white          "\033[37m"
#define vt_f_default        "\033[39m"

#define vt_b_black          "\033[40m"
#define vt_b_red            "\033[41m"
#define vt_b_green          "\033[42m"
#define vt_b_yellow         "\033[43m"
#define vt_b_blue           "\033[44m"
#define vt_b_megenta        "\033[45m"
#define vt_b_cyan           "\033[46m"
#define vt_b_white          "\033[47m"
#define vt_b_default        "\033[49m"

#define vt_f_bold_black    "\033[1m\033[30m"
#define vt_f_bold_red      "\033[1m\033[31m"
#define vt_f_bold_green    "\033[1m\033[32m"
#define vt_f_bold_yellow   "\033[1m\033[33m"
#define vt_f_bold_blue     "\033[1m\033[34m"
#define vt_f_bold_megenta  "\033[1m\033[35m"
#define vt_f_bold_cyan     "\033[1m\033[36m"
#define vt_f_bold_white    "\033[1m\033[37m"
#define vt_f_bold_default  "\033[1m\033[39m"

#define vt_bold             "\033[1m"
#define vt_dim              "\033[2m"     // does not work for xterm
#define vt_normal           "\033[22m"    // intensity

#define vt_n_enable         "\033[7m"     // negative
#define vt_n_disable        "\033[27m"

#define vt_u_single         "\033[4m"     // underline
#define vt_u_double         "\033[21m"    // does not work for xterm
#define vt_u_disable        "\033[24m"

#define vt_reset    vt_f_default vt_b_default vt_normal vt_n_disable vt_u_disable

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

template<typename T> T lexical_cast_s(const std::string& string)
{ // lexically cast a string to the selected type. Does not throw
	try
	{
		return boost::lexical_cast<T>(string);
	}
	catch (...)
	{
		return 0;
	}
}

template<typename T> std::string lexical_cast_i(T t)
{ // lexicaly cast the selected type to a string. Does not throw
	try
	{
		return boost::lexical_cast<std::string>(t);
	}
	catch (...)
	{
		return "";
	}
}

#endif

// vim:ts=4
