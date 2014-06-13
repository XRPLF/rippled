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

#include <boost/algorithm/string.hpp>

namespace ripple {

Log::Log (LogSeverity s, LogPartition& partition)
    : m_level (s)
    , m_partition (&partition)
{
}

Log::~Log ()
{
    if (m_partition->doLog (m_level))
        m_partition->write (
            LogPartition::convertLogSeverity (m_level), m_os.str());
}

//------------------------------------------------------------------------------

std::string Log::replaceFirstSecretWithAsterisks (std::string s)
{
    using namespace std;

    char const* secretToken = "\"secret\"";

    // Look for the first occurrence of "secret" in the string.
    //
    size_t startingPosition = s.find (secretToken);

    if (startingPosition != string::npos)
    {
        // Found it, advance past the token.
        //
        startingPosition += strlen (secretToken);

        // Replace the next 35 characters at most, without overwriting the end.
        //
        size_t endingPosition = std::min (startingPosition + 35, s.size () - 1);

        for (size_t i = startingPosition; i < endingPosition; ++i)
            s [i] = '*';
    }

    return s;
}

} // ripple
