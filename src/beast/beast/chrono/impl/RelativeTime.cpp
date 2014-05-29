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

#include <beast/chrono/RelativeTime.h>

// VFALCO TODO Migrate the localizable strings interfaces for this file

#ifndef NEEDS_TRANS
#define NEEDS_TRANS(s) (s)
#endif

#ifndef TRANS
#define TRANS(s) (s)
#endif

namespace beast {

RelativeTime::RelativeTime (const RelativeTime::value_type secs) noexcept
    : numSeconds (secs)
{
}

RelativeTime::RelativeTime (const RelativeTime& other) noexcept
    : numSeconds (other.numSeconds)
{
}

RelativeTime::~RelativeTime() noexcept {}

//==============================================================================

RelativeTime RelativeTime::milliseconds (const int milliseconds) noexcept
{
    return RelativeTime (milliseconds * 0.001);
}

RelativeTime RelativeTime::milliseconds (const std::int64_t milliseconds) noexcept
{
    return RelativeTime (milliseconds * 0.001);
}

RelativeTime RelativeTime::seconds (RelativeTime::value_type s) noexcept
{
    return RelativeTime (s);
}

RelativeTime RelativeTime::minutes (const RelativeTime::value_type numberOfMinutes) noexcept
{
    return RelativeTime (numberOfMinutes * 60.0);
}

RelativeTime RelativeTime::hours (const RelativeTime::value_type numberOfHours) noexcept
{
    return RelativeTime (numberOfHours * (60.0 * 60.0));
}

RelativeTime RelativeTime::days (const RelativeTime::value_type numberOfDays) noexcept
{
    return RelativeTime (numberOfDays  * (60.0 * 60.0 * 24.0));
}

RelativeTime RelativeTime::weeks (const RelativeTime::value_type numberOfWeeks) noexcept
{
    return RelativeTime (numberOfWeeks * (60.0 * 60.0 * 24.0 * 7.0));
}

//==============================================================================

std::int64_t RelativeTime::inMilliseconds() const noexcept
{
    return (std::int64_t) (numSeconds * 1000.0);
}

RelativeTime::value_type RelativeTime::inMinutes() const noexcept
{
    return numSeconds / 60.0;
}

RelativeTime::value_type RelativeTime::inHours() const noexcept
{
    return numSeconds / (60.0 * 60.0);
}

RelativeTime::value_type RelativeTime::inDays() const noexcept
{
    return numSeconds / (60.0 * 60.0 * 24.0);
}

RelativeTime::value_type RelativeTime::inWeeks() const noexcept
{
    return numSeconds / (60.0 * 60.0 * 24.0 * 7.0);
}

//==============================================================================

RelativeTime& RelativeTime::operator= (const RelativeTime& other) noexcept      { numSeconds = other.numSeconds; return *this; }

RelativeTime RelativeTime::operator+= (RelativeTime t) noexcept
{
    numSeconds += t.numSeconds; return *this;
}

RelativeTime RelativeTime::operator-= (RelativeTime t) noexcept
{
    numSeconds -= t.numSeconds; return *this;
}

RelativeTime RelativeTime::operator+= (const RelativeTime::value_type secs) noexcept
{
    numSeconds += secs; return *this;
}

RelativeTime RelativeTime::operator-= (const RelativeTime::value_type secs) noexcept
{
    numSeconds -= secs; return *this;
}

RelativeTime operator+ (RelativeTime t1, RelativeTime t2) noexcept
{
    return t1 += t2;
}

RelativeTime operator- (RelativeTime t1, RelativeTime t2) noexcept
{
    return t1 -= t2;
}

bool operator== (RelativeTime t1, RelativeTime t2) noexcept
{
    return t1.inSeconds() == t2.inSeconds();
}

bool operator!= (RelativeTime t1, RelativeTime t2) noexcept
{
    return t1.inSeconds() != t2.inSeconds();
}

bool operator>  (RelativeTime t1, RelativeTime t2) noexcept
{
    return t1.inSeconds() >  t2.inSeconds();
}

bool operator<  (RelativeTime t1, RelativeTime t2) noexcept
{
    return t1.inSeconds() <  t2.inSeconds();
}

bool operator>= (RelativeTime t1, RelativeTime t2) noexcept
{
    return t1.inSeconds() >= t2.inSeconds();
}

bool operator<= (RelativeTime t1, RelativeTime t2) noexcept
{
    return t1.inSeconds() <= t2.inSeconds();
}

//==============================================================================

static void translateTimeField (String& result, int n, const char* singular, const char* plural)
{
    result << TRANS (String((n == 1) ? singular : plural))
                .replace (n == 1 ? "1" : "2", String (n))
           << ' ';
}

String RelativeTime::getDescription (const String& returnValueForZeroTime) const
{
    if (numSeconds < 0.001 && numSeconds > -0.001)
        return returnValueForZeroTime;

    String result;
    result.preallocateBytes (32);

    if (numSeconds < 0)
        result << '-';

    int fieldsShown = 0;
    int n = std::abs ((int) inWeeks());
    if (n > 0)
    {
        translateTimeField (result, n, NEEDS_TRANS("1 week"), NEEDS_TRANS("2 weeks"));
        ++fieldsShown;
    }

    n = std::abs ((int) inDays()) % 7;
    if (n > 0)
    {
        translateTimeField (result, n, NEEDS_TRANS("1 day"), NEEDS_TRANS("2 days"));
        ++fieldsShown;
    }

    if (fieldsShown < 2)
    {
        n = std::abs ((int) inHours()) % 24;
        if (n > 0)
        {
            translateTimeField (result, n, NEEDS_TRANS("1 hour"), NEEDS_TRANS("2 hours"));
            ++fieldsShown;
        }

        if (fieldsShown < 2)
        {
            n = std::abs ((int) inMinutes()) % 60;
            if (n > 0)
            {
                translateTimeField (result, n, NEEDS_TRANS("1 minute"), NEEDS_TRANS("2 minutes"));
                ++fieldsShown;
            }

            if (fieldsShown < 2)
            {
                n = std::abs ((int) inSeconds()) % 60;
                if (n > 0)
                {
                    translateTimeField (result, n, NEEDS_TRANS("1 seconds"), NEEDS_TRANS("2 seconds"));
                    ++fieldsShown;
                }

                if (fieldsShown == 0)
                {
                    n = std::abs ((int) inMilliseconds()) % 1000;
                    if (n > 0)
                        result << n << ' ' << TRANS ("ms");
                }
            }
        }
    }

    return result.trimEnd();
}

std::string RelativeTime::to_string () const
{
    return getDescription ().toStdString();
}

}
	
#if BEAST_WINDOWS

#include <windows.h>

namespace beast {
namespace detail {

static double monotonicCurrentTimeInSeconds()
{
	return GetTickCount64() / 1000.0;
}
 
}
}
	
#elif BEAST_MAC || BEAST_IOS

#include <mach/mach_time.h>
#include <mach/mach.h>

namespace beast {
namespace detail {
	
static double monotonicCurrentTimeInSeconds()
{
	struct StaticInitializer
	{
		StaticInitializer ()
		{
			double numerator;
			double denominator;

			mach_timebase_info_data_t timebase;
			(void) mach_timebase_info (&timebase);
			
			if (timebase.numer % 1000000 == 0)
			{
				numerator   = timebase.numer / 1000000.0;
				denominator = timebase.denom * 1000.0;
			}
			else
			{
				numerator   = timebase.numer;
                // VFALCO NOTE I don't understand this code
				//denominator = timebase.denom * (std::uint64_t) 1000000 * 1000.0;
                denominator = timebase.denom * 1000000000.0;
			}
			
			ratio = numerator / denominator;
		}

		double ratio;
	};
	
	static StaticInitializer const data;

    return mach_absolute_time() * data.ratio;
}
}
}

#else
	
#include <time.h>

namespace beast {
namespace detail {

static double monotonicCurrentTimeInSeconds()
{
	timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1000000000.0;
}

}
}
	
#endif

namespace beast {
namespace detail {

// Records and returns the time from process startup
static double getStartupTime()
{
	struct StaticInitializer
	{
		StaticInitializer ()
		{
            when = detail::monotonicCurrentTimeInSeconds();
        }
		
        double when;
	};

	static StaticInitializer const data;

	return data.when;
}

// Used to call getStartupTime as early as possible
struct StartupTimeStaticInitializer
{
	StartupTimeStaticInitializer ()
	{
        getStartupTime();
    }
};

static StartupTimeStaticInitializer startupTimeStaticInitializer;
	
}

RelativeTime RelativeTime::fromStartup ()
{
	return RelativeTime (
        detail::monotonicCurrentTimeInSeconds() - detail::getStartupTime());
}

}

