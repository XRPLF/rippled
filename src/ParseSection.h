#ifndef _PARSE_SECTION_
#define _PARSE_SECTION_

#include <map>
#include <vector>
#include <string>

typedef std::map<const std::string, std::vector<std::string> > section;

section ParseSection(const std::string strInput, const bool bTrim);
void PrintSection(section secInput);

#endif
