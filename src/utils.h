#ifndef __UTILS__
#define __UTILS__

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/format.hpp>

#include <openssl/dh.h>

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

char charHex(int iDigit);

template<class Iterator>
void strHex(std::string& strDst, Iterator first, int iSize)
{
	strDst.resize(iSize*2);

	for (int i = 0; i < iSize; i++) {
		unsigned char c	= *first++;

		strDst[i*2]		= charHex(c >> 4);
		strDst[i*2+1]	= charHex(c & 15);
	}
}

inline void strHex(std::string& strDst, const std::string& strSrc) {
	strHex(strDst, strSrc.begin(), strSrc.size());
}

inline void strHex(std::string& strDst, const std::vector<unsigned char> vchData) {
	strHex(strDst, vchData.begin(), vchData.size());
}

int charUnHex(char cDigit);
void strUnHex(std::string& strDst, const std::string& strSrc);

DH* DH_der_load(const std::string& strDer);
DH* DH_der_load_hex(const std::string& strDer);
void DH_der_gen(std::string& strDer, int iKeyLength);
void DH_der_gen_hex(std::string& strDer, int iKeyLength);

#endif

// vim:ts=4
