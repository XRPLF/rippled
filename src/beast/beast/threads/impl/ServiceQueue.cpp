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

#include <beast/threads/ServiceQueue.h>

namespace beast {

class ServiceQueueBase::ScopedServiceThread : public List <ScopedServiceThread>::Node
{
public:
    explicit ScopedServiceThread (ServiceQueueBase* queue)
        : m_saved (ServiceQueueBase::s_service.get())
    {
        ServiceQueueBase::s_service.get() = queue;
    }

    ~ScopedServiceThread()
    {
        ServiceQueueBase::s_service.get() = m_saved;
    }

private:
    ServiceQueueBase* m_saved;
};

//------------------------------------------------------------------------------

ServiceQueueBase::ServiceQueueBase()
{
}

ServiceQueueBase::~ServiceQueueBase()
{
}

std::size_t ServiceQueueBase::poll ()
{
    std::size_t total (0);
    ScopedServiceThread thread (this);
    for (;;)
    {
        std::size_t const n (dequeue());
        if (! n)
            break;
        total += n;
    }
    return total;
}

std::size_t ServiceQueueBase::poll_one ()
{
    ScopedServiceThread thread (this);
    return dequeue();
}

std::size_t ServiceQueueBase::run ()
{
    std::size_t total (0);
    ScopedServiceThread thread (this);
    while (! stopped())
    {
        total += poll ();
        wait ();
    }
    return total;
}

std::size_t ServiceQueueBase::run_one ()
{
    std::size_t n;
    ScopedServiceThread (this);
    for (;;)
    {
        n = poll_one();
        if (n != 0)
            break;
        wait();
    }
    return n;
}

void ServiceQueueBase::stop ()
{
    SharedState::Access state (m_state);
    m_stopped.set (1);
    while (! state->waiting.empty ())
    {
        Waiter& waiting (state->waiting.front());
        state->waiting.pop_front ();
        waiting.signal ();
    }
}

void ServiceQueueBase::reset()
{
    // Must be stopped
    bassert (m_stopped.get () != 0);
    m_stopped.set (0);
}

// Block on the event if there are no items
// in the queue and we are not stopped.
//
void ServiceQueueBase::wait ()
{
    Waiter* waiter (nullptr);

    {
        SharedState::Access state (m_state);

        if (stopped ())
            return;

        if (! state->handlers.empty())
            return;

        if (state->unused.empty ())
        {
            waiter = new_waiter();
        }
        else
        {
            waiter = &state->unused.front ();
            state->unused.pop_front ();
        }

        state->waiting.push_front (*waiter);
    }

    waiter->wait();

    // Waiter got taken off the waiting list

    {
        SharedState::Access state (m_state);
        state->unused.push_front (*waiter);
    }
}

void ServiceQueueBase::enqueue (Item* item)
{
    Waiter* waiter (nullptr);

    {
        SharedState::Access state (m_state);
        state->handlers.push_back (*item);
        if (! state->waiting.empty ())
        {
            waiter = &state->waiting.front ();
            state->waiting.pop_front ();
        }
    }

    if (waiter != nullptr)
        waiter->signal();
}

bool ServiceQueueBase::empty()
{
    SharedState::Access state (m_state);
    return state->handlers.empty();
}

// A thread can only be blocked on one ServiceQueue so we store the pointer
// to which ServiceQueue it is blocked on to determine if the thread belongs
// to that queue.
//
ThreadLocalValue <ServiceQueueBase*> ServiceQueueBase::s_service;

}
