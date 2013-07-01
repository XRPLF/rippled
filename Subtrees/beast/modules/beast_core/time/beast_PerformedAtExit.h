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

#ifndef BEAST_PERFORMEDATEXIT_BEASTHEADER
#define BEAST_PERFORMEDATEXIT_BEASTHEADER

/*============================================================================*/
/**
  Perform an action at program exit

  To use, derive your class from PerformedAtExit, and override `performAtExit()`.
  The call will be made during the destruction of objects with static storage
  duration, before LeakChecked performs its diagnostics.

  @ingroup beast_core
*/
// VFALCO TODO Make the linked list element a private type and use composition
//             instead of inheritance, so that PerformedAtExit doesn't expose
//             lock free stack node interfaces.
//
class BEAST_API PerformedAtExit : public LockFreeStack <PerformedAtExit>::Node
{
public:
    class ExitHook;

protected:
    PerformedAtExit ();
    virtual ~PerformedAtExit () { }

protected:
    /** Called at program exit.
    */
    virtual void performAtExit () = 0;
};

#endif
