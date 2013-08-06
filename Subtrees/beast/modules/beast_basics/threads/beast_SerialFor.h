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

#ifndef BEAST_SERIALFOR_BEASTHEADER
#define BEAST_SERIALFOR_BEASTHEADER

/*============================================================================*/

/** Serial for loop.

    Iterates a for loop sequentially. This is a drop in replacement for
    ParallelFor.

    @see ParallelFor

    @ingroup beast_core
*/
class BEAST_API SerialFor : Uncopyable
{
public:
    /** Create a serial for loop.
    */
    inline SerialFor ()
    {
    }

    /** Determine the number of threads used to process loops.

        @return Always 1.
    */
    inline int getNumberOfThreads () const
    {
        return 1;
    }

    template <class F>
    inline void operator () (int numberOfIterations)
    {
        F f;

        for (int i = 0; i < numberOfIterations; ++i)
            f (i);
    }

    template <class F, class T1>
    inline void operator () (int numberOfIterations, T1 t1)
    {
        F f (t1);

        for (int i = 0; i < numberOfIterations; ++i)
            f (i);
    }
};

#endif
