#include "ParseSection.h"

#include <iostream>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>

#define SECTION_DEFAULT_NAME	""

section ParseSection(const std::string strInput, const bool bTrim)
{
    std::string strData(strInput);
    std::vector<std::string> vLines;
    section secResult;

    // Convert DOS format to unix.
    boost::algorithm::replace_all(strData, "\r\n", "\n");

    // Convert MacOS format to unix.
    boost::algorithm::replace_all(strData, "\r", "\n");

    boost::algorithm::split(vLines, strData, boost::algorithm::is_any_of("\n"));

    // Set the default section name.
    std::string strSection  = SECTION_DEFAULT_NAME;

    // Initialize the default section.
    secResult[strSection]   = section::mapped_type();

    // Parse each line.
    BOOST_FOREACH(std::string& strValue, vLines)
    {
		if (strValue.empty() || strValue[0] == '#')
		{
			// Blank line or comment, do nothing.
		}
		else if (strValue[0] == '[' && strValue[strValue.length()-1] == ']') {
			// New section.

			strSection				= strValue.substr(1, strValue.length()-2);
			secResult[strSection]   = section::mapped_type();
		}
		else
		{
			// Another line in a section.
			if (bTrim)
			{
				boost::algorithm::trim_right_if(strValue, boost::algorithm::is_space());
				boost::algorithm::trim_left_if(strValue, boost::algorithm::is_space());
			}

			secResult[strSection].push_back(strValue);
		}
    }

    return secResult;
}

void PrintSection(section secInput)
{
    std::cerr << "PrintSection>" << std::endl;
    BOOST_FOREACH(section::value_type& pairSection, secInput)
    {
		std::cerr << "[" << pairSection.first << "]" << std::endl;
		BOOST_FOREACH(std::string& value, pairSection.second)
		{
			std::cerr << value << std::endl;
		}
    }
    std::cerr << "PrintSection<" << std::endl;
}

section::mapped_type* sectionEntries(section& secSource, std::string strSection)
{
	section::iterator		it;
	section::mapped_type*	smtResult;

	it	= secSource.find(strSection);
    if (it == secSource.end())
    {
		smtResult	= 0;
	}
	else
	{
	    //section::mapped_type&	vecEntries	= it->second;

		smtResult	= &(it->second);
	}

	return smtResult;
}

int sectionCount(section& secSource, std::string strSection)
{
	section::mapped_type* pmtEntries	= sectionEntries(secSource, strSection);

	return pmtEntries ? -1 : pmtEntries->size();
}

bool sectionSingleB(section& secSource, std::string strSection, std::string& strValue)
{
	section::mapped_type*	pmtEntries	= sectionEntries(secSource, strSection);
	bool					bSingle		= pmtEntries && 1 == pmtEntries->size();

	if (bSingle)
	{
		strValue	= (*pmtEntries)[0];
    }

	return bSingle;
}

// vim:ts=4
