//
// Copyright (C) 2004-2007 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_ORACLE_SOURCE
#include "soci/oracle/soci-oracle.h"
#include "soci/connection-parameters.h"
#include "soci/backend-loader.h"
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

// iterates the string pointed by i, searching for pairs of key value.
// it returns the position after the value
std::string::const_iterator get_key_value(std::string::const_iterator & i,
                                          std::string::const_iterator const & end,
                                          std::string & key,
                                          std::string & value)
{
    bool in_value = false;
    bool quoted = false;

    key.clear();
    value.clear();

    while (i != end)
    {
        if (in_value == false)
        {
            if (*i == '=')
            {
                in_value = true;
                if (i != end && *(i + 1) == '"')
                {
                    quoted = true;
                    ++i; // jump over the quote
                }
            }
            else if (!isspace(*i))
            {
                key += *i;
            }
        }
        else
        {
            if ((quoted == true && *i == '"') || (quoted == false && isspace(*i)))
            {
                return ++i;
            }
            else
            {
                value += *i;
            }
        }
        ++i;
    }
    return i;
}

// decode charset and ncharset names
int charset_code(const std::string & name)
{
    // Note: unofficial reference for charset ids is:
    // http://www.mydul.net/charsets.html
            
    int code;
    
    if (name == "utf8")
    {
        code = 871;
    }
    else if (name == "utf16")
    {
        code = OCI_UTF16ID;
    }
    else if (name == "we8mswin1252" || name == "win1252")
    {
        code = 178;
    }
    else
    {
        // allow explicit number value
        
        std::istringstream ss(name);

        ss >> code;
        if (!ss)
        {
            throw soci_error("Invalid character set name.");
        }
    }

    return code;
}

// retrieves service name, user name and password from the
// uniform connect string
void chop_connect_string(std::string const & connectString,
    std::string & serviceName, std::string & userName,
    std::string & password, int & mode, bool & decimals_as_strings,
    int & charset, int & ncharset)
{
    serviceName.clear();
    userName.clear();
    password.clear();
    mode = OCI_DEFAULT;
    decimals_as_strings = false;
    charset = 0;
    ncharset = 0;

    std::string key, value;
    std::string::const_iterator i = connectString.begin();
    while (i != connectString.end())
    {
        i = get_key_value(i, connectString.end(), key, value);
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
        else if (key == "charset")
        {
            charset = charset_code(value);
        }
        else if (key == "ncharset")
        {
            ncharset = charset_code(value);
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
    int charset;
    int ncharset;

    chop_connect_string(parameters.get_connect_string(), serviceName, userName, password,
        mode, decimals_as_strings, charset, ncharset);

    return new oracle_session_backend(serviceName, userName, password,
        mode, decimals_as_strings, charset, ncharset);
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
