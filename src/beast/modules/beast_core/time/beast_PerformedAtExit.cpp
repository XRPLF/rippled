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

class PerformedAtExit::ExitHook
{
public:
    typedef Static::Storage <LockFreeStack <PerformedAtExit>, PerformedAtExit> StackType;

private:
    ~ExitHook ()
    {
        // Call all PerformedAtExit objects
        //
        PerformedAtExit* object = s_list->pop_front ();

        while (object != nullptr)
        {
            object->performAtExit ();

            object = s_list->pop_front ();
        }

        // Now do the leak checking
        //
        LeakCheckedBase::checkForLeaks ();
    }

public:
    static void push_front (PerformedAtExit* object)
    {
        s_list->push_front (object);
    }

private:
    friend class PerformedAtExit;

    static StackType s_list;

    static ExitHook s_performer;
};

PerformedAtExit::ExitHook PerformedAtExit::ExitHook::s_performer;
PerformedAtExit::ExitHook::StackType PerformedAtExit::ExitHook::s_list;

PerformedAtExit::PerformedAtExit ()
{
#if BEAST_IOS
    // Patrick Dehne:
    //      PerformedAtExit::ExitHook::push_front crashes on iOS
    //      if s_storage is not accessed before used
    //
    // VFALCO TODO Figure out why and fix it cleanly if needed.
    //
    char* hack = PerformedAtExit::ExitHook::s_list.s_storage;
#endif

    ExitHook::push_front (this);
}
