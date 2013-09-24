//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_INIFILE_H_INCLUDED
#define RIPPLE_INIFILE_H_INCLUDED

// VFALCO TODO Rename to IniFile and clean up
typedef std::map <const std::string, std::vector<std::string> > Section;

// VFALCO TODO Wrap this up in a class interface
//

Section ParseSection (const std::string& strInput, const bool bTrim);
void SectionPrint (Section secInput);
void SectionEntriesPrint (std::vector<std::string>* vspEntries, const std::string& strSection);
bool SectionSingleB (Section& secSource, const std::string& strSection, std::string& strValue);
int SectionCount (Section& secSource, const std::string& strSection);
Section::mapped_type* SectionEntries (Section& secSource, const std::string& strSection);

/** Parse a section of lines as a key/value array.

    Each line is in the form <key>=<value>.
    Spaces are considered part of the key and value.
*/
StringPairArray parseKeyValueSection (Section& secSource, String const& strSection);

#endif
