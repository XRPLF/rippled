
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

// vim:ts=4
