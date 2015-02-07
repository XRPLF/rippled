//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, Rafal Bobrowski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_FIREBIRD_SOURCE
#include "soci-firebird.h"
#include "error-firebird.h"
#include "session.h"
#include <locale>
#include <map>
#include <sstream>
#include <string>

using namespace soci;
using namespace soci::details::firebird;

namespace
{

// Helpers of explodeISCConnectString() for reading words from a string. "Word"
// here is defined very loosely as just a sequence of non-space characters.
//
// All these helper functions update the input iterator to point to the first
// character not consumed by them.

// Advance the input iterator until the first non-space character or end of the
// string.
void skipWhiteSpace(std::string::const_iterator& i, std::string::const_iterator const &end)
{
    std::locale const loc;
    for (; i != end; ++i)
    {
        if (!std::isspace(*i, loc))
            break;
    }
}

// Return the string of all characters until the first space or the specified
// delimiter.
//
// Throws if the first non-space character after the end of the word is not the
// delimiter. However just returns en empty string, without throwing, if
// nothing is left at all in the string except for white space.
std::string
getWordUntil(std::string const &s, std::string::const_iterator &i, char delim)
{
    std::string::const_iterator const end = s.end();
    skipWhiteSpace(i, end);

    // We need to handle this case specially because it's not an error if
    // nothing at all remains in the string. But if anything does remain, then
    // we must have the delimiter.
    if (i == end)
        return std::string();

    // Simply put anything until the delimiter into the word, stopping at the
    // first white space character.
    std::string word;
    std::locale const loc;
    for (; i != end; ++i)
    {
        if (*i == delim)
            break;

        if (std::isspace(*i, loc))
        {
            skipWhiteSpace(i, end);
            if (i == end || *i != delim)
            {
                std::ostringstream os;
                os << "Expected '" << delim << "' at position "
                   << (i - s.begin() + 1)
                   << " in Firebird connection string \""
                   << s << "\".";

                throw soci_error(os.str());
            }

            break;
        }

        word += *i;
    }

    if (i == end)
    {
        std::ostringstream os;
        os << "Expected '" << delim
           << "' not found before the end of the string "
           << "in Firebird connection string \""
           << s << "\".";

        throw soci_error(os.str());
    }

    ++i;    // Skip the delimiter itself.

    return word;
}

// Return a possibly quoted word, i.e. either just a sequence of non-space
// characters or everything inside a double-quoted string.
//
// Throws if the word is quoted and the closing quote is not found. However
// doesn't throw, just returns an empty string if there is nothing left.
std::string
getPossiblyQuotedWord(std::string const &s, std::string::const_iterator &i)
{
    std::string::const_iterator const end = s.end();
    skipWhiteSpace(i, end);

    std::string word;

    if (i != end && *i == '"')
    {
        for (;;)
        {
            if (++i == end)
            {
                std::ostringstream os;
                os << "Expected '\"' not found before the end of the string "
                      "in Firebird connection string \""
                   << s << "\".";

                throw soci_error(os.str());
            }

            if (*i == '"')
            {
                ++i;
                break;
            }

            word += *i;
        }
    }
    else // Not quoted.
    {
        std::locale const loc;
        for (; i != end; ++i)
        {
            if (std::isspace(*i, loc))
                break;

            word += *i;
        }
    }

    return word;
}

// retrieves parameters from the uniform connect string which is supposed to be
// in the form "key=value[ key2=value2 ...]" and the values may be quoted to
// allow including spaces into them. Notice that currently there is no way to
// include both a space and a double quote in a value.
std::map<std::string, std::string>
explodeISCConnectString(std::string const &connectString)
{
    std::map<std::string, std::string> parameters;

    std::string key, value;
    for (std::string::const_iterator i = connectString.begin(); ; )
    {
        key = getWordUntil(connectString, i, '=');
        if (key.empty())
            break;

        value = getPossiblyQuotedWord(connectString, i);

        parameters.insert(std::pair<std::string, std::string>(key, value));
    }

    return parameters;
}

// extracts given parameter from map previusly build with explodeISCConnectString
bool getISCConnectParameter(std::map<std::string, std::string> const & m, std::string const & key,
    std::string & value)
{
    std::map <std::string, std::string> :: const_iterator i;
    value.clear();

    i = m.find(key);

    if (i != m.end())
    {
        value = i->second;
        return true;
    }
    else
    {
        return false;
    }
}

} // namespace anonymous

firebird_session_backend::firebird_session_backend(
    connection_parameters const & parameters) : dbhp_(0), trhp_(0)
                                         , decimals_as_strings_(false)
{
    // extract connection parameters
    std::map<std::string, std::string>
        params(explodeISCConnectString(parameters.get_connect_string()));

    ISC_STATUS stat[stat_size];
    std::string param;

    // preparing connection options
    if (getISCConnectParameter(params, "user", param))
    {
        setDPBOption(isc_dpb_user_name, param);
    }

    if (getISCConnectParameter(params, "password", param))
    {
        setDPBOption(isc_dpb_password, param);
    }

    if (getISCConnectParameter(params, "role", param))
    {
        setDPBOption(isc_dpb_sql_role_name, param);
    }

    if (getISCConnectParameter(params, "charset", param))
    {
        setDPBOption(isc_dpb_lc_ctype, param);
    }

    if (getISCConnectParameter(params, "service", param) == false)
    {
        throw soci_error("Service name not specified.");
    }

    // connecting data base
    if (isc_attach_database(stat, static_cast<short>(param.size()),
        const_cast<char*>(param.c_str()), &dbhp_,
        static_cast<short>(dpb_.size()), const_cast<char*>(dpb_.c_str())))
    {
        throw_iscerror(stat);
    }

    if (getISCConnectParameter(params, "decimals_as_strings", param))
    {
        decimals_as_strings_ = param == "1" || param == "Y" || param == "y";
    }
    // starting transaction
    begin();
}


void firebird_session_backend::begin()
{
    // Transaction is always started in ctor, because Firebird can't work
    // without active transaction.
    // Transaction will be automatically commited in cleanUp method.
    if (trhp_ == 0)
    {
        ISC_STATUS stat[stat_size];
        if (isc_start_transaction(stat, &trhp_, 1, &dbhp_, 0, NULL))
        {
            throw_iscerror(stat);
        }
    }
}

firebird_session_backend::~firebird_session_backend()
{
    cleanUp();
}

void firebird_session_backend::setDPBOption(int const option, std::string const & value)
{

    if (dpb_.size() == 0)
    {
        dpb_.append(1, static_cast<char>(isc_dpb_version1));
    }

    // now we are adding new option
    dpb_.append(1, static_cast<char>(option));
    dpb_.append(1, static_cast<char>(value.size()));
    dpb_.append(value);
}

void firebird_session_backend::commit()
{
    ISC_STATUS stat[stat_size];

    if (trhp_ != 0)
    {
        if (isc_commit_transaction(stat, &trhp_))
        {
            throw_iscerror(stat);
        }

        trhp_ = 0;
    }

#ifndef SOCI_FIREBIRD_NORESTARTTRANSACTION
    begin();
#endif

}

void firebird_session_backend::rollback()
{
    ISC_STATUS stat[stat_size];

    if (trhp_ != 0)
    {
        if (isc_rollback_transaction(stat, &trhp_))
        {
            throw_iscerror(stat);
        }

        trhp_ = 0;
    }

#ifndef SOCI_FIREBIRD_NORESTARTTRANSACTION
    begin();
#endif

}

void firebird_session_backend::cleanUp()
{
    ISC_STATUS stat[stat_size];

    // at the end of session our transaction is finally commited.
    if (trhp_ != 0)
    {
        if (isc_commit_transaction(stat, &trhp_))
        {
            throw_iscerror(stat);
        }

        trhp_ = 0;
    }

    if (isc_detach_database(stat, &dbhp_))
    {
        throw_iscerror(stat);
    }

    dbhp_ = 0L;
}

bool firebird_session_backend::get_next_sequence_value(
    session & s, std::string const & sequence, long & value)
{
    // We could use isq_execute2() directly but this is even simpler.
    s << "select next value for " + sequence + " from rdb$database",
          into(value);

    return true;
}

firebird_statement_backend * firebird_session_backend::make_statement_backend()
{
    return new firebird_statement_backend(*this);
}

firebird_rowid_backend * firebird_session_backend::make_rowid_backend()
{
    return new firebird_rowid_backend(*this);
}

firebird_blob_backend * firebird_session_backend::make_blob_backend()
{
    return new firebird_blob_backend(*this);
}
