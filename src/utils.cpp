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

boost::posix_time::ptime ptFromSeconds(int iSeconds)
{
	return iSeconds < 0
		? boost::posix_time::ptime(boost::posix_time::not_a_date_time)
		: ptEpoch() + boost::posix_time::seconds(iSeconds);
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

void strUnHex(std::string& strDst, const std::string& strSrc)
{
	int	iBytes	= strSrc.size()/2;

	strDst.resize(iBytes);

	for (int i=0; i != iBytes; i++)
		strDst[i]	= (charUnHex(strSrc[i*2]) << 4) | charUnHex(strSrc[i*2+1]);
}

std::vector<unsigned char> strUnHex(const std::string& strSrc)
{
	std::string	strTmp;

	strUnHex(strTmp, strSrc);

	return strCopy(strTmp);
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

//
// DH support
//

void DH_der_gen(std::string& strDer, int iKeyLength)
{
	DH*	dh	= 0;
	int	iCodes;

	do {
		dh	= DH_generate_parameters(iKeyLength, DH_GENERATOR_5, NULL, NULL);
		iCodes	= 0;
		DH_check(dh, &iCodes);
	} while (iCodes & (DH_CHECK_P_NOT_PRIME|DH_CHECK_P_NOT_SAFE_PRIME|DH_UNABLE_TO_CHECK_GENERATOR|DH_NOT_SUITABLE_GENERATOR));

	strDer.resize(i2d_DHparams(dh, NULL));

	unsigned char* next	= reinterpret_cast<unsigned char *>(&strDer[0]);

	(void) i2d_DHparams(dh, &next);
}

void DH_der_gen_hex(std::string& strDer, int iKeyLength)
{
	std::string	strBuf;

	DH_der_gen(strBuf, iKeyLength);

	strDer	= strHex(strBuf);
}

DH* DH_der_load(const std::string& strDer)
{
	const unsigned char *pbuf	= reinterpret_cast<const unsigned char *>(&strDer[0]);

	return d2i_DHparams(NULL, &pbuf, strDer.size());
}

DH* DH_der_load_hex(const std::string& strDer)
{
	std::string	strBuf;

	strUnHex(strBuf, strDer);

	return DH_der_load(strBuf);
}

#ifdef WIN32
#define _WINSOCK_
#include <winsock2.h>

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

#endif

// vim:ts=4
