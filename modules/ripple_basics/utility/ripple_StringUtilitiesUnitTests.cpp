//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

BOOST_AUTO_TEST_SUITE ( Utils)

BOOST_AUTO_TEST_CASE ( ParseUrl )
{
    std::string strScheme;
    std::string strDomain;
    int         iPort;
    std::string strPath;

    if (!parseUrl ("lower://domain", strScheme, strDomain, iPort, strPath))
        BOOST_FAIL ("parseUrl: lower://domain failed");

    if (strScheme != "lower")
        BOOST_FAIL ("parseUrl: lower://domain : scheme failed");

    if (strDomain != "domain")
        BOOST_FAIL ("parseUrl: lower://domain : domain failed");

    if (iPort != -1)
        BOOST_FAIL ("parseUrl: lower://domain : port failed");

    if (strPath != "")
        BOOST_FAIL ("parseUrl: lower://domain : path failed");

    if (!parseUrl ("UPPER://domain:234/", strScheme, strDomain, iPort, strPath))
        BOOST_FAIL ("parseUrl: UPPER://domain:234/ failed");

    if (strScheme != "upper")
        BOOST_FAIL ("parseUrl: UPPER://domain:234/ : scheme failed");

    if (iPort != 234)
        BOOST_FAIL (boost::str (boost::format ("parseUrl: UPPER://domain:234/ : port failed: %d") % iPort));

    if (strPath != "/")
        BOOST_FAIL ("parseUrl: UPPER://domain:234/ : path failed");

    if (!parseUrl ("Mixed://domain/path", strScheme, strDomain, iPort, strPath))
        BOOST_FAIL ("parseUrl: Mixed://domain/path failed");

    if (strScheme != "mixed")
        BOOST_FAIL ("parseUrl: Mixed://domain/path tolower failed");

    if (strPath != "/path")
        BOOST_FAIL ("parseUrl: Mixed://domain/path path failed");
}

BOOST_AUTO_TEST_SUITE_END ()
