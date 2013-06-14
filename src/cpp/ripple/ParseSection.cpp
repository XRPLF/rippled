#include "ParseSection.h"

#include <iostream>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>

#define SECTION_DEFAULT_NAME    ""

// for logging
struct ParseSectionLog { };

SETUP_LOG (ParseSectionLog)

section ParseSection (const std::string& strInput, const bool bTrim)
{
    std::string strData (strInput);
    std::vector<std::string> vLines;
    section secResult;

    // Convert DOS format to unix.
    boost::algorithm::replace_all (strData, "\r\n", "\n");

    // Convert MacOS format to unix.
    boost::algorithm::replace_all (strData, "\r", "\n");

    boost::algorithm::split (vLines, strData, boost::algorithm::is_any_of ("\n"));

    // Set the default section name.
    std::string strSection  = SECTION_DEFAULT_NAME;

    // Initialize the default section.
    secResult[strSection]   = section::mapped_type ();

    // Parse each line.
    BOOST_FOREACH (std::string & strValue, vLines)
    {
        if (strValue.empty () || strValue[0] == '#')
        {
            // Blank line or comment, do nothing.

            nothing ();
        }
        else if (strValue[0] == '[' && strValue[strValue.length () - 1] == ']')
        {
            // New section.

            strSection              = strValue.substr (1, strValue.length () - 2);
            secResult[strSection]   = section::mapped_type ();
        }
        else
        {
            // Another line for section.
            if (bTrim)
                boost::algorithm::trim (strValue);

            if (!strValue.empty ())
                secResult[strSection].push_back (strValue);
        }
    }

    return secResult;
}

void sectionEntriesPrint (std::vector<std::string>* vspEntries, const std::string& strSection)
{
    std::cerr << "[" << strSection << "]" << std::endl;

    if (vspEntries)
    {
        BOOST_FOREACH (std::string & strValue, *vspEntries)
        {
            std::cerr << strValue << std::endl;
        }
    }
}

void sectionPrint (section secInput)
{
    BOOST_FOREACH (section::value_type & pairSection, secInput)
    {
        sectionEntriesPrint (&pairSection.second, pairSection.first);
    }
}

section::mapped_type* sectionEntries (section& secSource, const std::string& strSection)
{
    section::iterator       it;
    section::mapped_type*   smtResult;

    it  = secSource.find (strSection);

    if (it == secSource.end ())
    {
        smtResult   = 0;
    }
    else
    {
        //section::mapped_type& vecEntries  = it->second;

        smtResult   = & (it->second);
    }

    return smtResult;
}

int sectionCount (section& secSource, const std::string& strSection)
{
    section::mapped_type* pmtEntries    = sectionEntries (secSource, strSection);

    return pmtEntries ? -1 : pmtEntries->size ();
}

bool sectionSingleB (section& secSource, const std::string& strSection, std::string& strValue)
{
    section::mapped_type*   pmtEntries  = sectionEntries (secSource, strSection);
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

// vim:ts=4
