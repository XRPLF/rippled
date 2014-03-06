//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

// VFALCO TODO Replace these with something more robust and without macros.
//
#if ! BEAST_MSVC
#define _vsnprintf(a,b,c,d) vsnprintf(a,b,c,d)
#endif

std::string strprintf (const char* format, ...)
{
    char buffer[50000];
    char* p = buffer;
    int limit = sizeof (buffer);
    int ret;

    for (;;)
    {
        va_list arg_ptr;
        va_start (arg_ptr, format);
        ret = _vsnprintf (p, limit, format, arg_ptr);
        va_end (arg_ptr);

        if (ret >= 0 && ret < limit)
            break;

        if (p != buffer)
            delete[] p;

        limit *= 2;
        p = new char[limit];

        if (p == NULL)
            throw std::bad_alloc ();
    }

    std::string str (p, p + ret);

    if (p != buffer)
        delete[] p;

    return str;
}

int charUnHex (char cDigit)
{
    struct HexTab
    {
        int hex[256];

        HexTab ()
        {
            std::fill (std::begin (hex), std::end (hex), -1);
            for (int i = 0; i < 10; ++i)
                hex ['0'+i] = i;
            for (int i = 0; i < 6; ++i)
            {
                hex ['A'+i] = 10 + i;
                hex ['a'+i] = 10 + i;
            }
        }
        int operator[] (int i) const
        {
           return hex[i];
        }
    };

    static HexTab xtab;
    
    return xtab[cDigit];
}

// NIKB NOTE: This function is only used by strUnHex (const std::string& strSrc)
// which results in a pointless copy from std::string into std::vector. Should
// we just scrap this function altogether?
int strUnHex (std::string& strDst, const std::string& strSrc)
{
    std::string tmp;

    tmp.reserve ((strSrc.size () + 1) / 2);

    auto iter = strSrc.cbegin ();

    if (strSrc.size () & 1)
    {
        int c = charUnHex (*iter);

        if (c < 0)
            return -1;

        tmp.push_back(c);
        ++iter;
    }

    while (iter != strSrc.cend ())
    {
        int cHigh = charUnHex (*iter);
        ++iter;

        if (cHigh < 0)
            return -1;

        int cLow = charUnHex (*iter);
        ++iter;

        if (cLow < 0)
            return -1;

        tmp.push_back (static_cast<char>((cHigh << 4) | cLow));
    }

    strDst = std::move(tmp);

    return strDst.size ();
}

Blob strUnHex (const std::string& strSrc)
{
    std::string strTmp;

    if (strUnHex (strTmp, strSrc) == -1)
        return Blob ();

    return strCopy (strTmp);
}

uint64_t uintFromHex (const std::string& strSrc)
{
    uint64_t    uValue = 0;

    BOOST_FOREACH (char c, strSrc)
    uValue = (uValue << 4) | charUnHex (c);

    return uValue;
}

//
// Misc string
//

Blob strCopy (const std::string& strSrc)
{
    Blob vucDst;

    vucDst.resize (strSrc.size ());

    std::copy (strSrc.begin (), strSrc.end (), vucDst.begin ());

    return vucDst;
}

std::string strCopy (Blob const& vucSrc)
{
    std::string strDst;

    strDst.resize (vucSrc.size ());

    std::copy (vucSrc.begin (), vucSrc.end (), strDst.begin ());

    return strDst;

}

extern std::string urlEncode (const std::string& strSrc)
{
    std::string strDst;
    int         iOutput = 0;
    int         iSize   = strSrc.length ();

    strDst.resize (iSize * 3);

    for (int iInput = 0; iInput < iSize; iInput++)
    {
        unsigned char c = strSrc[iInput];

        if (c == ' ')
        {
            strDst[iOutput++]   = '+';
        }
        else if (isalnum (c))
        {
            strDst[iOutput++]   = c;
        }
        else
        {
            strDst[iOutput++]   = '%';
            strDst[iOutput++]   = charHex (c >> 4);
            strDst[iOutput++]   = charHex (c & 15);
        }
    }

    strDst.resize (iOutput);

    return strDst;
}

//
// IP Port parsing
//
// <-- iPort: "" = -1
// VFALCO TODO Make this not require boost... and especially boost::asio
bool parseIpPort (const std::string& strSource, std::string& strIP, int& iPort)
{
    boost::smatch   smMatch;
    bool            bValid  = false;

    static boost::regex reEndpoint ("\\`\\s*(\\S+)(?:\\s+(\\d+))?\\s*\\'");

    if (boost::regex_match (strSource, smMatch, reEndpoint))
    {
        boost::system::error_code   err;
        std::string                 strIPRaw    = smMatch[1];
        std::string                 strPortRaw  = smMatch[2];

        boost::asio::ip::address    addrIP      = boost::asio::ip::address::from_string (strIPRaw, err);

        bValid  = !err;

        if (bValid)
        {
            strIP   = addrIP.to_string ();
            iPort   = strPortRaw.empty () ? -1 : lexicalCastThrow <int> (strPortRaw);
        }
    }

    return bValid;
}

bool parseUrl (const std::string& strUrl, std::string& strScheme, std::string& strDomain, int& iPort, std::string& strPath)
{
    // scheme://username:password@hostname:port/rest
    static boost::regex reUrl ("(?i)\\`\\s*([[:alpha:]][-+.[:alpha:][:digit:]]*)://([^:/]+)(?::(\\d+))?(/.*)?\\s*?\\'");
    boost::smatch   smMatch;

    bool    bMatch  = boost::regex_match (strUrl, smMatch, reUrl);          // Match status code.

    if (bMatch)
    {
        std::string strPort;

        strScheme   = smMatch[1];
        strDomain   = smMatch[2];
        strPort     = smMatch[3];
        strPath     = smMatch[4];

        boost::algorithm::to_lower (strScheme);

        iPort   = strPort.empty () ? -1 : lexicalCast <int> (strPort);
        // Log::out() << strUrl << " : " << bMatch << " : '" << strDomain << "' : '" << strPort << "' : " << iPort << " : '" << strPath << "'";
    }

    // Log::out() << strUrl << " : " << bMatch << " : '" << strDomain << "' : '" << strPath << "'";

    return bMatch;
}

//
// Quality parsing
// - integers as is.
// - floats multiplied by a billion
bool parseQuality (const std::string& strSource, uint32& uQuality)
{
    uQuality    = lexicalCast <uint32> (strSource);

    if (!uQuality)
    {
        float   fQuality    = lexicalCast <float> (strSource);

        if (fQuality)
            uQuality    = (uint32) (QUALITY_ONE * fQuality);
    }

    return !!uQuality;
}

StringPairArray parseDelimitedKeyValueString (String parameters, beast_wchar delimiter)
{
    StringPairArray keyValues;

    while (parameters.isNotEmpty ())
    {
        String pair;

        {
            int const delimiterPos = parameters.indexOfChar (delimiter);

            if (delimiterPos != -1)
            {
                pair = parameters.substring (0, delimiterPos);

                parameters = parameters.substring (delimiterPos + 1);
            }
            else
            {
                pair = parameters;

                parameters = String::empty;
            }
        }

        int const equalPos = pair.indexOfChar ('=');

        if (equalPos != -1)
        {
            String const key = pair.substring (0, equalPos);
            String const value = pair.substring (equalPos + 1, pair.length ());

            keyValues.set (key, value);
        }
    }

    return keyValues;
}

//------------------------------------------------------------------------------

class StringUtilitiesTests : public UnitTest
{
public:
    void testUnHexSuccess (std::string strIn, std::string strExpected)
    {
        std::string strOut;

        expect (strUnHex (strOut, strIn) == strExpected.length (),
            "strUnHex: parsing correct input failed");

        expect (strOut == strExpected,
            "strUnHex: parsing doesn't produce expected result");
    }

    void testUnHexFailure (std::string strIn)
    {
        std::string strOut;

        expect (strUnHex (strOut, strIn) == -1,
            "strUnHex: parsing incorrect input succeeded");

        expect (strOut.empty (),
            "strUnHex: parsing incorrect input returned data");
    }

    void testUnHex ()
    {
        beginTestCase ("strUnHex");

        testUnHexSuccess ("526970706c6544", "RippleD");
        testUnHexSuccess ("A", "\n");
        testUnHexSuccess ("0A", "\n");
        testUnHexSuccess ("D0A", "\r\n");
        testUnHexSuccess ("0D0A", "\r\n");
        testUnHexSuccess ("200D0A", " \r\n");
        testUnHexSuccess ("282A2B2C2D2E2F29", "(*+,-./)");

        // Check for things which contain some or only invalid characters
        testUnHexFailure ("123X");
        testUnHexFailure ("V");
        testUnHexFailure ("XRP");
    }

    void testParseUrl ()
    {
        beginTestCase ("parseUrl");

        std::string strScheme;
        std::string strDomain;
        int         iPort;
        std::string strPath;

        unexpected (!parseUrl ("lower://domain", strScheme, strDomain, iPort, strPath),
            "parseUrl: lower://domain failed");

        unexpected (strScheme != "lower",
            "parseUrl: lower://domain : scheme failed");

        unexpected (strDomain != "domain",
            "parseUrl: lower://domain : domain failed");

        unexpected (iPort != -1,
            "parseUrl: lower://domain : port failed");

        unexpected (strPath != "",
            "parseUrl: lower://domain : path failed");

        unexpected (!parseUrl ("UPPER://domain:234/", strScheme, strDomain, iPort, strPath),
            "parseUrl: UPPER://domain:234/ failed");

        unexpected (strScheme != "upper",
            "parseUrl: UPPER://domain:234/ : scheme failed");

        unexpected (iPort != 234,
            boost::str (boost::format ("parseUrl: UPPER://domain:234/ : port failed: %d") % iPort));

        unexpected (strPath != "/",
            "parseUrl: UPPER://domain:234/ : path failed");

        unexpected (!parseUrl ("Mixed://domain/path", strScheme, strDomain, iPort, strPath),
            "parseUrl: Mixed://domain/path failed");

        unexpected (strScheme != "mixed",
            "parseUrl: Mixed://domain/path tolower failed");

        unexpected (strPath != "/path",
            "parseUrl: Mixed://domain/path path failed");
    }

    void runTest ()
    {
        testParseUrl ();

        testUnHex ();
    }

    StringUtilitiesTests () : UnitTest ("StringUtilities", "ripple")
    {
    }
};

static StringUtilitiesTests stringUtilitiesTests;
