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

PerformanceCounter::PerformanceCounter (const String& name_,
                                        const int runsPerPrintout,
                                        const File& loggingFile)
    : name (name_),
      numRuns (0),
      runsPerPrint (runsPerPrintout),
      totalTime (0),
      outputFile (loggingFile)
{
    if (outputFile != File::nonexistent ())
    {
        String s ("**** Counter for \"");
        s << name_ << "\" started at: "
          << Time::getCurrentTime().toString (true, true)
          << newLine;

        outputFile.appendText (s, false, false);
    }
}

PerformanceCounter::~PerformanceCounter()
{
    printStatistics();
}

void PerformanceCounter::start()
{
    started = Time::getHighResolutionTicks();
}

void PerformanceCounter::stop()
{
    const int64 now = Time::getHighResolutionTicks();

    totalTime += 1000.0 * Time::highResolutionTicksToSeconds (now - started);

    if (++numRuns == runsPerPrint)
        printStatistics();
}

void PerformanceCounter::printStatistics()
{
    if (numRuns > 0)
    {
        String s ("Performance count for \"");
        s << name << "\" - average over " << numRuns << " run(s) = ";

        const int micros = (int) (totalTime * (1000.0 / numRuns));

        if (micros > 10000)
            s << (micros/1000) << " millisecs";
        else
            s << micros << " microsecs";

        s << ", total = " << String (totalTime / 1000, 5) << " seconds";

        Logger::outputDebugString (s);

        s << newLine;

        if (outputFile != File::nonexistent ())
            outputFile.appendText (s, false, false);

        numRuns = 0;
        totalTime = 0;
    }
}

}  // namespace beast
