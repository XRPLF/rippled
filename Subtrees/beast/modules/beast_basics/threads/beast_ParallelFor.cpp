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

ParallelFor::ParallelFor (ThreadGroup& pool)
    : m_pool (pool)
    , m_finishedEvent (false) // auto-reset
{
}

int ParallelFor::getNumberOfThreads () const
{
    return m_pool.getNumberOfThreads ();
}

void ParallelFor::doLoop (int numberOfIterations, Iteration& iteration)
{
    if (numberOfIterations > 1)
    {
        int const numberOfThreads = m_pool.getNumberOfThreads ();

        // The largest number of pool threads we need is one less than the number
        // of iterations, because we also run the loop body on the caller's thread.
        //
        int const maxThreads = numberOfIterations - 1;

        // Calculate the number of parallel instances as the smaller of the number
        // of threads available (including the caller's) and the number of iterations.
        //
        int const numberOfParallelInstances = std::min (
                numberOfThreads + 1, numberOfIterations);

        LoopState* loopState (new (m_pool.getAllocator ()) LoopState (
                                  iteration, m_finishedEvent, numberOfIterations, numberOfParallelInstances));

        m_pool.call (maxThreads, &LoopState::forLoopBody, loopState);

        // Also use the caller's thread to run the loop body.
        loopState->forLoopBody ();

        m_finishedEvent.wait ();
    }
    else if (numberOfIterations == 1)
    {
        // Just one iteration, so do it.
        iteration (0);
    }
}
