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

#ifndef BEAST_CORE_ATEXITHOOK_H_INCLUDED
#define BEAST_CORE_ATEXITHOOK_H_INCLUDED

#include <beast/intrusive/List.h>

namespace beast {

/** Hook for performing activity on program exit.

    These hooks execute when objects with static storage duration are
    destroyed. The hooks are called in the reverse order that they were
    created.

    To use, derive your class from AtExitHook and implement onExit.
    Alternatively, add AtExitMemberHook as a data member of your class and
    then provide your own onExit function with this signature:

    @code

    void onExit ()

    @endcode

    @see AtExitMemberHook
*/
/** @{ */
class AtExitHook
{
protected:
    AtExitHook ();
    virtual ~AtExitHook ();

protected:
    /** Called at program exit. */
    virtual void onExit () = 0;

private:
    class Manager;

    class Item : public List <Item>::Node
    {
    public:
        explicit Item (AtExitHook* hook);
        AtExitHook* hook ();

    private:
        AtExitHook* m_hook;
    };

    Item m_item;
};

/** Helper for utilizing the AtExitHook as a data member.
*/
template <class Object>
class AtExitMemberHook : public AtExitHook
{
public:
    explicit AtExitMemberHook (Object* owner) : m_owner (owner)
    {
    }

private:
    void onExit ()
    {
        m_owner->onExit ();
    }

    Object* m_owner;
};
/** @} */

} // beast

#endif
