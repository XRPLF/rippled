//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef _PARSE_SECTION_
#define _PARSE_SECTION_

typedef std::map<const std::string, std::vector<std::string> > Section;

Section ParseSection (const std::string& strInput, const bool bTrim);
void SectionPrint (Section secInput);
void SectionEntriesPrint (std::vector<std::string>* vspEntries, const std::string& strSection);
bool SectionSingleB (Section& secSource, const std::string& strSection, std::string& strValue);
int SectionCount (Section& secSource, const std::string& strSection);
Section::mapped_type* SectionEntries (Section& secSource, const std::string& strSection);

#endif
