//
// Copyright (C) 2004-2007 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_ORACLE_SOURCE
#include "soci-oracle.h"
#include <connection-parameters.h>
#include <backend-loader.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;

// retrieves service name, user name and password from the
// uniform connect string
void chop_connect_string(std::string const & connectString,
    std::string & serviceName, std::string & userName,
    std::string & password, int & mode, bool & decimals_as_strings)
{
    // transform the connect string into a sequence of tokens
    // separated by spaces, this is done by replacing each first '='
    // in each original token with space
    // note: each original token is a key=value pair and only the first
    // '=' there is replaced with space, so that potential '=' signs
    // in the value part are left intact

    std::string tmp;
    bool in_value = false;
    for (std::string::const_iterator i = connectString.begin(),
             end = connectString.end(); i != end; ++i)
    {
        if (*i == '=' && in_value == false)
        {
            // this is the first '=' in the key=value pair
            tmp += ' ';
            in_value = true;
        }
        else
        {
            tmp += *i;
            if (*i == ' ' || *i == '\t')
            {
                // follow with the next key=value pair
                in_value = false;
            }
        }
    }

    serviceName.clear();
    userName.clear();
    password.clear();
    mode = OCI_DEFAULT;
    decimals_as_strings = false;

    std::istringstream iss(tmp);
    std::string key, value;
    while (iss >> key >> value)
    {
        if (key == "service")
        {
            serviceName = value;
        }
        else if (key == "user")
        {
            userName = value;
        }
        else if (key == "password")
        {
            password = value;
        }
        else if (key == "mode")
        {
            if (value == "sysdba")
            {
                mode = OCI_SYSDBA;
            }
            else if (value == "sysoper")
            {
                mode = OCI_SYSOPER;
            }
            else if (value == "default")
            {
                mode = OCI_DEFAULT;
            }
            else
            {
                throw soci_error("Invalid connection mode.");
            }
        }
        else if (key == "decimals_as_strings")
        {
            decimals_as_strings = value == "1" || value == "Y" || value == "y";
        }
    }
}

// concrete factory for Empty concrete strategies
oracle_session_backend * oracle_backend_factory::make_session(
     connection_parameters const & parameters) const
{
    std::string serviceName, userName, password;
    int mode;
    bool decimals_as_strings;

    chop_connect_string(parameters.get_connect_string(), serviceName, userName, password,
            mode, decimals_as_strings);

    return new oracle_session_backend(serviceName, userName, password,
            mode, decimals_as_strings);
}

oracle_backend_factory const soci::oracle;

extern "C"
{

// for dynamic backend loading
SOCI_ORACLE_DECL backend_factory const * factory_oracle()
{
    return &soci::oracle;
}

SOCI_ORACLE_DECL void register_factory_oracle()
{
    soci::dynamic_backends::register_backend("oracle", soci::oracle);
}

} // extern "C"
