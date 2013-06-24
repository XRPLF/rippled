//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_INIFILE_RIPPLEHEADER
#define RIPPLE_INIFILE_RIPPLEHEADER

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

#endif
