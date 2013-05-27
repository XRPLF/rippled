
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
