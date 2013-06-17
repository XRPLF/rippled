/*============================================================================*/
/*
  VFLib: https://github.com/vinniefalco/VFLib

  Copyright (C) 2008 by Vinnie Falco <vinnie.falco@gmail.com>

  This library contains portions of other open source products covered by
  separate licenses. Please see the corresponding source files for specific
  terms.

  VFLib is provided under the terms of The MIT License (MIT):

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.
*/
/*============================================================================*/

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
