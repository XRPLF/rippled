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

#ifndef BEAST_ASIO_ASYNCOBJECT_H_INCLUDED
#define BEAST_ASIO_ASYNCOBJECT_H_INCLUDED

/** Mix-in to track when all pending I/O is complete.
    Derived classes must be callable with this signature:
        void asyncHandlersComplete()
*/
template <class Derived>
class AsyncObject
{
public:
    ~AsyncObject ()
    {
        // Destroying the object with I/O pending? Not a clean exit!
        bassert (m_pending.get() == 0);
    }

    /** RAII container that maintains the count of pending I/O.
        Bind this into the argument list of every handler passed
        to an initiating function.
    */
    class CompletionCounter
    {
    public:
        explicit CompletionCounter (Derived* owner)
            : m_owner (owner)
        {
            ++m_owner->m_pending;
        }

        CompletionCounter (CompletionCounter const& other)
            : m_owner (other.m_owner)
        {
            ++m_owner->m_pending;
        }

        ~CompletionCounter ()
        {
            if (--m_owner->m_pending == 0)
                m_owner->asyncHandlersComplete ();
        }

    private:
        CompletionCounter& operator= (CompletionCounter const&);
        Derived* m_owner;
    };

private:
    // The number of handlers pending.
    Atomic <int> m_pending;
};

#endif
