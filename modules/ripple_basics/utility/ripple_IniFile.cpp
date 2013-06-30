//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

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

            nothing ();
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

    return pmtEntries ? -1 : pmtEntries->size ();
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

// vim:ts=4
