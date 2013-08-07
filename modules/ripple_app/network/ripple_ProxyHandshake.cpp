//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

ProxyHandshake::ProxyHandshake (bool expectHandshake)
    : m_status (expectHandshake ? statusHandshake : statusNone)
    , m_gotCR (false)
{
    m_buffer.preallocateBytes (maxVersion1Bytes);
}

ProxyHandshake::~ProxyHandshake ()
{
}

std::size_t ProxyHandshake::feed (void const* inputBuffer, size_t inputBytes)
{
    std::size_t bytesConsumed = 0;

    char const* p = static_cast <char const*> (inputBuffer);

    if (m_status == statusHandshake)
    {
        if (! m_gotCR)
        {
            while (inputBytes > 0 && m_buffer.length () < maxVersion1Bytes - 1)
            {
                beast_wchar c = *p++;
                ++bytesConsumed;
                --inputBytes;
                m_buffer += c;

                if (c == '\r')
                {
                    m_gotCR = true;
                    break;
                }
                else if (c == '\n')
                {
                    m_status = statusFailed;
                }
            }

            if (m_buffer.length () > maxVersion1Bytes - 1)
            {
                m_status = statusFailed;
            }
        }
    }

    if (m_status == statusHandshake)
    {
        if (inputBytes > 0 && m_gotCR)
        {
            bassert (m_buffer.length () < maxVersion1Bytes);

            char const lf ('\n');

            if (*p == lf)
            {
                ++bytesConsumed;
                --inputBytes;
                m_buffer += lf;

                parseLine ();
            }
            else
            {
                m_status = statusFailed;
            }
        }
    }

    return bytesConsumed;
}

void ProxyHandshake::parseLine ()
{
    Version1 p;

    bool success = p.parse (m_buffer.getCharPointer (), m_buffer.length ());

    if (success)
    {
        m_endpoints = p.endpoints;
        m_status = statusOk;
    }
    else
    {
        m_status = statusFailed;
    }
}

int ProxyHandshake::indexOfFirstNonNumber (String const& input)
{
    bassert (input.length () > 0);

    int i = 0;
    for (; i < input.length (); ++i)
    {
        if (! CharacterFunctions::isDigit (input [i]))
            break;
    }

    return i;
}

bool ProxyHandshake::chop (String const& what, String& input)
{
    if (input.startsWith (what))
    {
        input = input.substring (what.length ());

        return true;
    }

    return false;
}

bool ProxyHandshake::chopUInt (int* value, int limit, String& input)
{
    if (input.length () <= 0)
        return false;

    String const s = input.substring (0, indexOfFirstNonNumber (input));

    if (s.length () <= 0)
        return false;

    int const n = s.getIntValue ();

    // Leading zeroes disallowed as per spec, to prevent confusion with octal
    if (String (n) != s)
        return false;

    if (n < 0 || n > limit)
        return false;

    input = input.substring (s.length ());

    *value = n;

    return true;
}

//------------------------------------------------------------------------------

/*

steps:

Proxy protocol lets us filter attackers by learning the source ip and port

1. Determine if we should use the proxy on a connection
    - Port just for proxy protocol connections
    - Filter on source IPs

2. Read a line from the connection to get the proxy information

3. Parse the line (human or binary?)

4. Code Interface to retrieve proxy information (ip/port) on connection

*/

ProxyHandshake::Version1::Version1 ()
{
}

bool ProxyHandshake::IPv4::Addr::chop (String& input)
{
    if (!ProxyHandshake::chopUInt (&a, 255, input))
        return false;

    if (!ProxyHandshake::chop (".", input))
        return false;

    if (!ProxyHandshake::chopUInt (&b, 255, input))
        return false;

    if (!ProxyHandshake::chop (".", input))
        return false;

    if (!ProxyHandshake::chopUInt (&c, 255, input))
        return false;

    if (!ProxyHandshake::chop (".", input))
        return false;

    if (!ProxyHandshake::chopUInt (&d, 255, input))
        return false;

    return true;
}

bool ProxyHandshake::Version1::parse (void const* headerData, size_t headerBytes)
{
    String input (static_cast <CharPointer_UTF8::CharType const*> (headerData), headerBytes);

    if (input.length () < 2)
        return false;

    if (! input.endsWith ("\r\n"))
        return false;

    input = input.dropLastCharacters (2);

    if (! ProxyHandshake::chop ("PROXY ", input))
        return false;

    if (ProxyHandshake::chop ("UNKNOWN", input))
    {
        endpoints.proto = protoUnknown;

        input = "";
    }
    else
    {
        if (ProxyHandshake::chop ("TCP4 ", input))
        {
            endpoints.proto = protoTcp4;

            if (! endpoints.ipv4.sourceAddr.chop (input))
                return false;

            if (! ProxyHandshake::chop (" ", input))
                return false;

            if (! endpoints.ipv4.destAddr.chop (input))
                return false;

            if (! ProxyHandshake::chop (" ", input))
                return false;

            if (! ProxyHandshake::chopUInt (&endpoints.ipv4.sourcePort, 65535, input))
                return false;

            if (! ProxyHandshake::chop (" ", input))
                return false;

            if (! ProxyHandshake::chopUInt (&endpoints.ipv4.destPort, 65535, input))
                return false;
        }
        else if (ProxyHandshake::chop ("TCP6 ", input))
        {
            endpoints.proto = protoTcp6;

            //bassertfalse;

            return false;
        }
        else 
        {
            return false;
        }
    }

    // Can't have anything extra between the last port number and the CRLF
    if (input.length () > 0)
        return false;

    return true;
}

//------------------------------------------------------------------------------

class ProxyHandshakeTests : public UnitTest
{
public:
    ProxyHandshakeTests () : UnitTest ("ProxyHandshake", "ripple")
    {
    }

    static std::string goodIpv4 ()
    {
        return "PROXY TCP4 255.255.255.255 255.255.255.255 65535 65535\r\n"; // 56 chars
    }

    static std::string goodIpv6 ()
    {
        return "PROXY TCP6 fffffffffffffffffffffffffffffffffffffff.fffffffffffffffffffffffffffffffffffffff 65535 65535\r\n";
              //1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123 4 (104 chars)
    }

    static std::string goodUnknown ()
    {
        return "PROXY UNKNOWN\r\n";
    }

    static std::string goodUnknownBig ()
    {
        return "PROXY UNKNOWN fffffffffffffffffffffffffffffffffffffff.fffffffffffffffffffffffffffffffffffffff 65535 65535\r\n";
              //1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456 7 (107 chars)
    }

    void testHandshake (std::string const& s, bool shouldSucceed)
    {
        if (s.size () > 1)
        {
            ProxyHandshake h (true);

            expect (h.getStatus () == ProxyHandshake::statusHandshake);

            for (int i = 0; i < s.size () && h.getStatus () == ProxyHandshake::statusHandshake ; ++i)
            {
                std::size_t const bytesConsumed = h.feed (& s[i], 1);

                if (i != s.size () - 1)
                    expect (h.getStatus () == ProxyHandshake::statusHandshake);

                expect (bytesConsumed == 1);
            }

            if (shouldSucceed)
            {
                expect (h.getStatus () == ProxyHandshake::statusOk);
            }
            else
            {
                expect (h.getStatus () == ProxyHandshake::statusFailed);
            }
        }
        else
        {
            bassertfalse;
        }
    }

    void testVersion1String (std::string const& s, bool shouldSucceed)
    {
        ProxyHandshake::Version1 p;

        if (shouldSucceed)
        {
            expect (p.parse (s.c_str (), s.size ()));
        }
        else
        {
            unexpected (p.parse (s.c_str (), s.size ()));
        }

        for (int i = 1; i < s.size () - 1; ++i)
        {
            String const partial = String (s).dropLastCharacters (i);
            std::string ss (partial.toStdString ());

            expect (! p.parse (ss.c_str (), ss.size ()));
        }

        testHandshake (s, shouldSucceed);
    }

    void testVersion1 ()
    {
        beginTestCase ("version1");

        testVersion1String (goodIpv4 (), true);
        testVersion1String (goodIpv6 (), false);
        testVersion1String (goodUnknown (), true);
        testVersion1String (goodUnknownBig (), true);
    }

    void runTest ()
    {
        testVersion1 ();
    }
};

static ProxyHandshakeTests proxyHandshakeTests;
