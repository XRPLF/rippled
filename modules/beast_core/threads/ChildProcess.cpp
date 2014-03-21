//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

namespace beast
{

ChildProcess::ChildProcess() {}
ChildProcess::~ChildProcess() {}

bool ChildProcess::waitForProcessToFinish (const int timeoutMs) const
{
    const uint32 timeoutTime = Time::getMillisecondCounter() + (uint32) timeoutMs;

    do
    {
        if (! isRunning())
            return true;
    }
    while (timeoutMs < 0 || Time::getMillisecondCounter() < timeoutTime);

    return false;
}

String ChildProcess::readAllProcessOutput()
{
    MemoryOutputStream result;

    for (;;)
    {
        char buffer [512];
        const int num = readProcessOutput (buffer, sizeof (buffer));

        if (num <= 0)
            break;

        result.write (buffer, (size_t) num);
    }

    return result.toString();
}

//==============================================================================

class ChildProcess_test  : public unit_test::suite
{
public:
    void run()
    {
      #if BEAST_WINDOWS || BEAST_MAC || BEAST_LINUX
        ChildProcess p;

       #if BEAST_WINDOWS
        expect (p.start ("tasklist"));
       #else
        expect (p.start ("ls /"));
       #endif

        if (! p.waitForProcessToFinish (10 * 1000))
        {
            if (p.kill ())
                p.waitForProcessToFinish (-1);
        }

        //String output (p.readAllProcessOutput());
        //expect (output.isNotEmpty());
      #endif
    }
};

// VFALCO NOTE I had to disable this test because it was leaving
//             behind a zombie process and making other unit tests fail.
//             It doesnt happen with a debugger attached, or if the
//             unit test is run individually.
//
BEAST_DEFINE_TESTSUITE_MANUAL(ChildProcess,beast_core,beast);

} // beast
