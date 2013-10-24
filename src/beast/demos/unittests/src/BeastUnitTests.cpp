//------------------------------------------------------------------------------
/*
	This file is part of Beast: https://github.com/vinniefalco/Beast
	Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#include "BeastUnitTests.h"

class BeastUnitTests : public beast::UnitTests
{
public:
	explicit BeastUnitTests (bool shouldLog)
	: m_shouldLog (shouldLog)
	{
	}
	
	void logMessage (beast::String const& message)
	{
		if (m_shouldLog)
			std::cout << message.toStdString () << std::endl;
	}
	
private:
	bool const m_shouldLog;
};

int runUnitTests (beast::String const& match, beast::String const& format)
{
    bool const shouldLog = format != "junit";
	
    if (format != "junit" && format != "text" && format != "")
    {
		beast::String s;
        s << "Warning, unknown unittest-format='" << format << "'";
		// Log::out () << s.toStdString ();
    }
	
    BeastUnitTests tr (shouldLog);
	
    tr.runSelectedTests (match);
	
    if (format == "junit")
    {
		beast::UnitTestUtilities::JUnitXMLFormatter f (tr);
		
		beast::String const s = f.createDocumentString ();
		
        std::cout << s.toStdString ();
    }
    else
    {
		beast::UnitTests::Results const& r (tr.getResults ());
		
		beast::String s;
		
        s << "Summary: " <<
		beast::String (r.suites.size ()) << " suites, " <<
		beast::String (r.cases) << " cases, " <<
		beast::String (r.tests) << " tests, " <<
		beast::String (r.failures) << " failure" << ((r.failures != 1) ? "s" : "") << ".";
		
        tr.logMessage (s);
    }
	
    return tr.anyTestsFailed () ? EXIT_FAILURE : EXIT_SUCCESS;
}
