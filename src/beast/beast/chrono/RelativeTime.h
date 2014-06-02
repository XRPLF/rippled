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

#ifndef BEAST_CHRONO_RELATIVETIME_H_INCLUDED
#define BEAST_CHRONO_RELATIVETIME_H_INCLUDED

#include <beast/Config.h>
#include <beast/strings/String.h>

#include <beast/utility/noexcept.h>
#include <string>
#include <sstream>

namespace beast {

//==============================================================================
/** A relative measure of time.

    The time is stored as a number of seconds, at double-precision floating
    point accuracy, and may be positive or negative.

    If you need an absolute time, (i.e. a date + time), see the Time class.
*/
class  RelativeTime
{
public:
    //==============================================================================
    /** The underlying data type used by RelativeTime.

        If you need to get to the underlying time and manipulate it
        you can use this to declare a type that is guaranteed to
        work cleanly.
    */
    typedef double value_type;

    //==============================================================================
    /** Creates a RelativeTime.

        @param seconds  the number of seconds, which may be +ve or -ve.
        @see milliseconds, minutes, hours, days, weeks
    */
    explicit RelativeTime (value_type seconds = 0.0) noexcept;

    /** Copies another relative time. */
    RelativeTime (const RelativeTime& other) noexcept;

    /** Copies another relative time. */
    RelativeTime& operator= (const RelativeTime& other) noexcept;

    /** Destructor. */
    ~RelativeTime() noexcept;

    bool isZero() const
        { return numSeconds == 0; }

    bool isNotZero() const
        { return numSeconds != 0; }

    /** Returns the amount of time since the process was started. */
    static RelativeTime fromStartup ();

    //==============================================================================
    /** Creates a new RelativeTime object representing a number of milliseconds.
        @see seconds, minutes, hours, days, weeks
    */
    static RelativeTime milliseconds (int milliseconds) noexcept;

    /** Creates a new RelativeTime object representing a number of milliseconds.
        @see seconds, minutes, hours, days, weeks
    */
    static RelativeTime milliseconds (std::int64_t milliseconds) noexcept;

    /** Creates a new RelativeTime object representing a number of seconds.
        @see milliseconds, minutes, hours, days, weeks
    */
    static RelativeTime seconds (value_type seconds) noexcept;

    /** Creates a new RelativeTime object representing a number of minutes.
        @see milliseconds, hours, days, weeks
    */
    static RelativeTime minutes (value_type numberOfMinutes) noexcept;

    /** Creates a new RelativeTime object representing a number of hours.
        @see milliseconds, minutes, days, weeks
    */
    static RelativeTime hours (value_type numberOfHours) noexcept;

    /** Creates a new RelativeTime object representing a number of days.
        @see milliseconds, minutes, hours, weeks
    */
    static RelativeTime days (value_type numberOfDays) noexcept;

    /** Creates a new RelativeTime object representing a number of weeks.
        @see milliseconds, minutes, hours, days
    */
    static RelativeTime weeks (value_type numberOfWeeks) noexcept;

    //==============================================================================
    /** Returns the number of milliseconds this time represents.
        @see milliseconds, inSeconds, inMinutes, inHours, inDays, inWeeks
    */
    std::int64_t inMilliseconds() const noexcept;

    /** Returns the number of seconds this time represents.
        @see inMilliseconds, inMinutes, inHours, inDays, inWeeks
    */
    value_type inSeconds() const noexcept       { return numSeconds; }

    /** Returns the number of minutes this time represents.
        @see inMilliseconds, inSeconds, inHours, inDays, inWeeks
    */
    value_type inMinutes() const noexcept;

    /** Returns the number of hours this time represents.
        @see inMilliseconds, inSeconds, inMinutes, inDays, inWeeks
    */
    value_type inHours() const noexcept;

    /** Returns the number of days this time represents.
        @see inMilliseconds, inSeconds, inMinutes, inHours, inWeeks
    */
    value_type inDays() const noexcept;

    /** Returns the number of weeks this time represents.
        @see inMilliseconds, inSeconds, inMinutes, inHours, inDays
    */
    value_type inWeeks() const noexcept;

    /** Returns a readable textual description of the time.

        The exact format of the string returned will depend on
        the magnitude of the time - e.g.

        "1 min 4 secs", "1 hr 45 mins", "2 weeks 5 days", "140 ms"

        so that only the two most significant units are printed.

        The returnValueForZeroTime value is the result that is returned if the
        length is zero. Depending on your application you might want to use this
        to return something more relevant like "empty" or "0 secs", etc.

        @see inMilliseconds, inSeconds, inMinutes, inHours, inDays, inWeeks
    */
    String getDescription (const String& returnValueForZeroTime = "0") const;
    std::string to_string () const;

    template <typename Number>
    RelativeTime operator+ (Number seconds) const noexcept
        { return RelativeTime (numSeconds + seconds); }

    template <typename Number>
    RelativeTime operator- (Number seconds) const noexcept
        { return RelativeTime (numSeconds - seconds); }

    /** Adds another RelativeTime to this one. */
    RelativeTime operator+= (RelativeTime timeToAdd) noexcept;

    /** Subtracts another RelativeTime from this one. */
    RelativeTime operator-= (RelativeTime timeToSubtract) noexcept;

    /** Adds a number of seconds to this time. */
    RelativeTime operator+= (value_type secondsToAdd) noexcept;

    /** Subtracts a number of seconds from this time. */
    RelativeTime operator-= (value_type secondsToSubtract) noexcept;

private:
    value_type numSeconds;
};

//------------------------------------------------------------------------------

bool operator== (RelativeTime t1, RelativeTime t2) noexcept;
bool operator!= (RelativeTime t1, RelativeTime t2) noexcept;
bool operator>  (RelativeTime t1, RelativeTime t2) noexcept;
bool operator<  (RelativeTime t1, RelativeTime t2) noexcept;
bool operator>= (RelativeTime t1, RelativeTime t2) noexcept;
bool operator<= (RelativeTime t1, RelativeTime t2) noexcept;

//------------------------------------------------------------------------------

/** Adds two RelativeTimes together. */
RelativeTime operator+ (RelativeTime t1, RelativeTime t2) noexcept;

/** Subtracts two RelativeTimes. */
RelativeTime operator- (RelativeTime t1, RelativeTime t2) noexcept;

inline std::ostream& operator<< (std::ostream& os, RelativeTime const& diff)
{
    os << diff.to_string();
    return os;
}

}

#endif
