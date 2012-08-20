#include "utils.h"
#include "uint256.h"

#include <boost/asio.hpp>
#include <boost/regex.hpp>

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
			uQuality	= QUALITY_ONE*fQuality;
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

uint64_t be64toh(uint64_t value)
{
	return(value);
}

uint32_t htobe32(uint32_t value)
{
	return(htonl(value));
}

uint32_t be32toh(uint32_t value){ return(value); }

#endif

// vim:ts=4
