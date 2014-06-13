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

#include <beast/utility/Journal.h>
#include <beast/unit_test/suite.h>

namespace beast {

class Journal_test : public unit_test::suite
{
public:
    class TestSink : public Journal::Sink
    {
    private:
        int m_count;

    public:
        TestSink()
            : m_count(0)
        {
        }

        int
        count() const
        {
            return m_count;
        }

        void
        reset()
        {
            m_count = 0;
        }

        void
        write (Journal::Severity, std::string const&)
        {
            ++m_count;
        }
    };

    void run ()
    {
        TestSink sink;

        sink.severity(Journal::kInfo);

        Journal j(sink);
        
        j.trace << " ";
        expect(sink.count() == 0);
        j.debug << " ";
        expect(sink.count() == 0);
        j.info << " ";
        expect(sink.count() == 1);
        j.warning << " ";
        expect(sink.count() == 2);
        j.error << " ";
        expect(sink.count() == 3);
        j.fatal << " ";
        expect(sink.count() == 4);

        sink.reset();

        sink.severity(Journal::kDebug);

        j.trace << " ";
        expect(sink.count() == 0);
        j.debug << " ";
        expect(sink.count() == 1);
        j.info << " ";
        expect(sink.count() == 2);
        j.warning << " ";
        expect(sink.count() == 3);
        j.error << " ";
        expect(sink.count() == 4);
        j.fatal << " ";
        expect(sink.count() == 5);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(Journal,utility,beast);

} // beast
