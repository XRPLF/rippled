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

#include <ripple/basics/utility/IniFile.h>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

namespace ripple {

#define SECTION_DEFAULT_NAME    ""

struct ParseSectionLog; // for Log

SETUP_LOGN (ParseSectionLog,"ParseSection")

Section ParseSection (const std::string& strInput, const bool bTrim)
{
    std::string strData (strInput);
    std::vector<std::string> vLines;
    Section secResult;

    // Convert DOS format to unix.
    boost::algorithm::replace_all (strData, "\r\n", "\n");

    // Convert MacOS format to unix.
    boost::algorithm::replace_all (strData, "\r", "\n");

    boost::algorithm::split (vLines, strData, boost::algorithm::is_any_of ("\n"));

    // Set the default Section name.
    std::string strSection  = SECTION_DEFAULT_NAME;

    // Initialize the default Section.
    secResult[strSection]   = Section::mapped_type ();

    // Parse each line.
    BOOST_FOREACH (std::string & strValue, vLines)
    {
        if (strValue.empty () || strValue[0] == '#')
        {
            // Blank line or comment, do nothing.
        }
        else if (strValue[0] == '[' && strValue[strValue.length () - 1] == ']')
        {
            // New Section.

            strSection              = strValue.substr (1, strValue.length () - 2);
            secResult[strSection]   = Section::mapped_type ();
        }
        else
        {
            // Another line for Section.
            if (bTrim)
                boost::algorithm::trim (strValue);

            if (!strValue.empty ())
                secResult[strSection].push_back (strValue);
        }
    }

    return secResult;
}

void SectionEntriesPrint (std::vector<std::string>* vspEntries, const std::string& strSection)
{
    Log::out() << "[" << strSection << "]";

    if (vspEntries)
    {
        BOOST_FOREACH (std::string & strValue, *vspEntries)
        {
            Log::out() << strValue;
        }
    }
}

void SectionPrint (Section secInput)
{
    BOOST_FOREACH (Section::value_type & pairSection, secInput)
    {
        SectionEntriesPrint (&pairSection.second, pairSection.first);
    }
}

Section::mapped_type* SectionEntries (Section& secSource, const std::string& strSection)
{
    Section::iterator       it;
    Section::mapped_type*   smtResult;

    it  = secSource.find (strSection);

    if (it == secSource.end ())
    {
        smtResult   = 0;
    }
    else
    {
        //Section::mapped_type& vecEntries  = it->second;

        smtResult   = & (it->second);
    }

    return smtResult;
}

int SectionCount (Section& secSource, const std::string& strSection)
{
    Section::mapped_type* pmtEntries    = SectionEntries (secSource, strSection);

    return pmtEntries ? pmtEntries->size () : 0;
}

bool SectionSingleB (Section& secSource, const std::string& strSection, std::string& strValue)
{
    Section::mapped_type*   pmtEntries  = SectionEntries (secSource, strSection);
    bool                    bSingle     = pmtEntries && 1 == pmtEntries->size ();

    if (bSingle)
    {
        strValue    = (*pmtEntries)[0];
    }
    else if (pmtEntries)
    {
        WriteLog (lsWARNING, ParseSectionLog) << boost::str (boost::format ("Section [%s]: requires 1 line not %d lines.")
                                              % strSection
                                              % pmtEntries->size ());
    }

    return bSingle;
}

beast::StringPairArray
parseKeyValueSection (Section& secSource, beast::String const& strSection)
{
    beast::StringPairArray result;

    // yuck.
    std::string const stdStrSection (strSection.toStdString ());

    typedef Section::mapped_type Entries;

    Entries* const entries = SectionEntries (secSource, stdStrSection);

    if (entries != nullptr)
    {
        for (Entries::const_iterator iter = entries->begin (); iter != entries->end (); ++iter)
        {
            beast::String const line (iter->c_str ());

            int const equalPos = line.indexOfChar ('=');

            if (equalPos != -1)
            {
                beast::String const key = line.substring (0, equalPos);
                beast::String const value = line.substring (equalPos + 1, line.length ());

                result.set (key, value);
            }
        }
    }

    return result;
}

} // ripple
