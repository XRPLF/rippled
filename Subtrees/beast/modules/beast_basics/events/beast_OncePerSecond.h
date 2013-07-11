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

#ifndef BEAST_ONCEPERSECOND_BEASTHEADER
#define BEAST_ONCEPERSECOND_BEASTHEADER

/*============================================================================*/
/**
    Provides a once per second notification.

    Derive your class from OncePerSecond and override doOncePerSecond(). Then,
    call startOncePerSecond() to begin receiving the notifications. No clean-up
    or other actions are required.

    @ingroup beast_core
*/
class BEAST_API OncePerSecond : Uncopyable
{
public:
    OncePerSecond ();
    virtual ~OncePerSecond ();

    /** Begin receiving notifications. */
    void startOncePerSecond ();

    /** Stop receiving notifications. */
    void endOncePerSecond ();

protected:
    /** Called once per second. */
    virtual void doOncePerSecond () = 0;

private:
    class TimerSingleton;
    typedef SharedObjectPtr <TimerSingleton> TimerPtr;

    struct Elem : List <Elem>::Node
    {
        TimerPtr instance;
        OncePerSecond* object;
    };

    Elem m_elem;
};

#endif
