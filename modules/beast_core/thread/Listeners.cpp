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

// CallQueue item to process a Call for a particular listener.
// This is used to avoid bind overhead.
//
class ListenersBase::CallWork : public CallQueue::Work
{
public:
    inline CallWork (ListenersBase::Call* const c, void* const listener)
        : m_call (c), m_listener (listener)
    {
    }

    void operator () ()
    {
        m_call->operator () (m_listener);
    }

private:
    ListenersBase::Call::Ptr m_call;
    void* const m_listener;
};

//------------------------------------------------------------------------------

// CallQueue item to process a Call for a group.
// This is used to avoid bind overhead.
//
class ListenersBase::GroupWork : public CallQueue::Work
{
public:
    inline GroupWork (Group* group,
                      ListenersBase::Call* c,
                      const timestamp_t timestamp)
        : m_group (group)
        , m_call (c)
        , m_timestamp (timestamp)
    {
    }

    void operator () ()
    {
        m_group->do_call (m_call, m_timestamp);
    }

private:
    Group::Ptr m_group;
    ListenersBase::Call::Ptr m_call;
    const timestamp_t m_timestamp;
};

//------------------------------------------------------------------------------

// CallQueue item to process a call for a particular listener.
// This is used to avoid bind overhead.
//
class ListenersBase::GroupWork1 : public CallQueue::Work
{
public:
    inline GroupWork1 (Group* group,
                       ListenersBase::Call* c,
                       const timestamp_t timestamp,
                       void* const listener)
        : m_group (group)
        , m_call (c)
        , m_timestamp (timestamp)
        , m_listener (listener)
    {
    }

    void operator () ()
    {
        m_group->do_call1 (m_call, m_timestamp, m_listener);
    }

private:
    Group::Ptr m_group;
    ListenersBase::Call::Ptr m_call;
    const timestamp_t m_timestamp;
    void* const m_listener;
};

//------------------------------------------------------------------------------

// A Proxy maintains a list of Entry.
// Each Entry holds a group and the current Call (which can be updated).
//
struct ListenersBase::Proxy::Entry : Entries::Node,
        SharedObject,
        AllocatedBy <AllocatorType>
{
    typedef SharedPtr <Entry> Ptr;

    explicit Entry (Group* g)
        : group (g)
    {
    }

    ~Entry ()
    {
        bassert (call.get () == 0);
    }

    Group::Ptr group;
    AtomicPointer <Call> call;
};

//------------------------------------------------------------------------------

// A Group maintains a list of Entry.
//
struct ListenersBase::Group::Entry : List <Entry>::Node,
        AllocatedBy <AllocatorType>
{
    Entry (void* const l, const timestamp_t t)
        : listener (l)
        , timestamp (t)
    {
    }

    void* const listener;
    const timestamp_t timestamp;
};

//------------------------------------------------------------------------------
//
// Group
//
//------------------------------------------------------------------------------

// - A list of listeners associated with the same CallQueue.
//
// - The list is only iterated on the CallQueue's thread.
//
// - It is safe to add or remove listeners from the group
//   at any time.
//

ListenersBase::Group::Group (CallQueue& callQueue)
    : m_fifo (callQueue)
    , m_listener (0)
{
}

ListenersBase::Group::~Group ()
{
    // If this goes off it means a Listener forgot to remove itself.
    bassert (m_list.empty ());

    // shouldn't be deleting group during a call
    bassert (m_listener == 0);
}

// Add the listener with the given timestamp.
// The listener will only get calls with higher timestamps.
// The caller must prevent duplicates.
//
void ListenersBase::Group::add (void* listener,
                                const timestamp_t timestamp,
                                AllocatorType& allocator)
{
    ReadWriteMutex::ScopedWriteLockType lock (m_mutex);

    bassert (!contains (listener));

    // Should never be able to get here while in call()
    bassert (m_listener == 0);

    // Add the listener and remember the time stamp so we don't
    // send it calls that were queued earlier than the add().
    m_list.push_back (*new (allocator) Entry (listener, timestamp));
}

// Removes the listener from the group if it exists.
// Returns true if the listener was removed.
//
bool ListenersBase::Group::remove (void* listener)
{
    bool found = false;

    ReadWriteMutex::ScopedWriteLockType lock (m_mutex);

    // Should never be able to get here while in call()
    bassert (m_listener == 0);

    for (List <Entry>::iterator iter = m_list.begin (); iter != m_list.end (); ++iter)
    {
        Entry* entry = & (*iter);

        if (entry->listener == listener)
        {
            m_list.erase (m_list.iterator_to (*entry));
            delete entry;
            found = true;
            break;
        }
    }

    return found;
}

// Used for assertions.
// The caller must synchronize.
//
bool ListenersBase::Group::contains (void* const listener) /*const*/
{
    for (List <Entry>::iterator iter = m_list.begin (); iter != m_list.end (); iter++)
        if (iter->listener == listener)
            return true;

    return false;
}

void ListenersBase::Group::call (Call* const c, const timestamp_t timestamp)
{
    bassert (!empty ());
    m_fifo.callp (new (m_fifo.getAllocator ()) GroupWork (this, c, timestamp));
}

void ListenersBase::Group::queue (Call* const c, const timestamp_t timestamp)
{
    bassert (!empty ());
    m_fifo.queuep (new (m_fifo.getAllocator ()) GroupWork (this, c, timestamp));
}

void ListenersBase::Group::call1 (Call* const c,
                                  const timestamp_t timestamp,
                                  void* const listener)
{
    m_fifo.callp (new (m_fifo.getAllocator ()) GroupWork1 (
                      this, c, timestamp, listener));
}

void ListenersBase::Group::queue1 (Call* const c,
                                   const timestamp_t timestamp,
                                   void* const listener)
{
    m_fifo.queuep (new (m_fifo.getAllocator ()) GroupWork1 (
                       this, c, timestamp, listener));
}

// Queues a reference to the Call on the thread queue of each listener
// that is currently in our list. The thread queue must be in the
// stack's call chain, either directly from CallQueue::synchronize(),
// or from Proxy::do_call() called from CallQueue::synchronize().
//
void ListenersBase::Group::do_call (Call* const c, const timestamp_t timestamp)
{
    if (!empty ())
    {
        ReadWriteMutex::ScopedReadLockType lock (m_mutex);

        // Recursion not allowed.
        bassert (m_listener == 0);

        // The body of the loop MUST NOT cause listeners to get called.
        // Therefore, we don't have to worry about listeners removing
        // themselves while iterating the list.
        //
        for (List <Entry>::iterator iter = m_list.begin (); iter != m_list.end ();)
        {
            Entry* entry = & (*iter++);

            // Since it is possible for a listener to be added after a
            // Call gets queued but before it executes, this prevents listeners
            // from seeing Calls created before they were added.
            //
            if (timestamp > entry->timestamp)
            {
                m_listener = entry->listener;

                // The thread queue's synchronize() function MUST be in our call
                // stack to guarantee that these calls will not execute immediately.
                // They will be handled by the tail recusion unrolling in the
                // thread queue.
                bassert (m_fifo.isBeingSynchronized ());

                m_fifo.callp (new (m_fifo.getAllocator ()) CallWork (c, m_listener));

                m_listener = 0;
            }
        }
    }
    else
    {
        // last listener was removed before we got here,
        // and the parent listener list may have been deleted.
    }
}

void ListenersBase::Group::do_call1 (Call* const c, const timestamp_t timestamp,
                                     void* const listener)
{
    if (!empty ())
    {
        ReadWriteMutex::ScopedReadLockType lock (m_mutex);

        // Recursion not allowed.
        bassert (m_listener == 0);

        for (List <Entry>::iterator iter = m_list.begin (); iter != m_list.end ();)
        {
            Entry* entry = & (*iter++);

            if (entry->listener == listener)
            {
                if (timestamp > entry->timestamp)
                {
                    m_listener = entry->listener;

                    bassert (m_fifo.isBeingSynchronized ());

                    m_fifo.callp (new (m_fifo.getAllocator ()) CallWork (c, m_listener));

                    m_listener = 0;
                }
            }
        }
    }
    else
    {
        // Listener was removed
    }
}

//------------------------------------------------------------------------------
//
// Proxy
//
//------------------------------------------------------------------------------

// CallQueue item for processing a an Entry for a Proxy.
// This is used to avoid bind overhead.
//
class ListenersBase::Proxy::Work : public CallQueue::Work
{
public:
    inline Work (Entry* const entry, const timestamp_t timestamp)
        : m_entry (entry)
        , m_timestamp (timestamp)
    {
    }

    void operator () ()
    {
        ListenersBase::Call* c = m_entry->call.exchange (0);
        Group* group = m_entry->group;
        if (!group->empty ())
            group->do_call (c, m_timestamp);
        c->decReferenceCount ();
    }

private:
    Entry::Ptr m_entry;
    const timestamp_t m_timestamp;
};

// Holds a Call, and gets put in the CallQueue in place of the Call.
// The Call may be replaced if it hasn't been processed yet.
// A Proxy exists for the lifetime of the Listeners.
//
ListenersBase::Proxy::Proxy (void const* const member, const size_t bytes)
    : m_bytes (bytes)
{
    if (bytes > maxMemberBytes)
        fatal_error ("the Proxy member is too large");

    memcpy (m_member, member, bytes);
}

ListenersBase::Proxy::~Proxy ()
{
    // If the proxy is getting destroyed it means:
    // - the listeners object is getting destroyed
    // - all listeners must have removed themselves
    // - all thread queues have been fully processed
    // Therefore, our entries should be gone.

    // NO it is possible for an empty Group, for which
    // the parent listeners object has been destroyed,
    // to still exist in a thread queue!!!

    // But all listeners should have removed themselves
    // so our list of groups should still be empty.
    bassert (m_entries.empty ());
}

// Adds the group to the Proxy.
// Caller must have the proxies mutex.
// Caller is responsible for preventing duplicates.
//
void ListenersBase::Proxy::add (Group* group, AllocatorType& allocator)
{
    Entry* entry (new (allocator) Entry (group));

    // Manual addref and put raw pointer in list
    entry->incReferenceCount ();
    m_entries.push_back (*entry);
}

// Removes the group from the Proxy.
// Caller must have the proxies mutex.
// Caller is responsible for making sure the group exists.
void ListenersBase::Proxy::remove (Group* group)
{
    for (Entries::iterator iter = m_entries.begin (); iter != m_entries.end ();)
    {
        Entry* entry = & (*iter++);

        if (entry->group == group)
        {
            // remove from list and manual release
            m_entries.erase (m_entries.iterator_to (*entry));
            entry->decReferenceCount ();

            // Entry might still be in the empty group's thread queue
            break;
        }
    }
}

// For each group, updates the call.
// Queues each group that isn't already queued.
// Caller must acquire the group read lock.
//
void ListenersBase::Proxy::update (Call* const c, const timestamp_t timestamp)
{
    // why would we even want to be called?
    bassert (!m_entries.empty ());

    // With the read lock, this list can't change on us unless someone
    // adds a listener to a new thread queue in response to a call.
    for (Entries::iterator iter = m_entries.begin (); iter != m_entries.end ();)
    {
        Entry* entry = & (*iter++);

        // Manually add a reference since we use a raw pointer
        c->incReferenceCount ();

        // Atomically exchange the new call for the old one
        Call* old = entry->call.exchange (c);

        // If no old call then they need to be queued
        if (!old)
        {
            CallQueue& callQueue = entry->group->getCallQueue ();
            callQueue.callp (new (callQueue.getAllocator ()) Work (entry, timestamp));
        }
        else
        {
            old->decReferenceCount ();
        }
    }
}

bool ListenersBase::Proxy::match (void const* const member, const size_t bytes) const
{
    return m_bytes == bytes && memcmp (member, m_member, bytes) == 0;
}

//------------------------------------------------------------------------------
//
// ListenersBase
//
//------------------------------------------------------------------------------

ListenersBase::ListenersBase ()
    : m_timestamp (0)
    , m_allocator (AllocatorType::getInstance ())
    , m_callAllocator (CallAllocatorType::getInstance ())
{
}

ListenersBase::~ListenersBase ()
{
    for (Groups::iterator iter = m_groups.begin (); iter != m_groups.end ();)
    {
        Group* group = & (*iter++);

        // If this goes off it means a Listener forgot to remove.
        bassert (group->empty ());

        group->decReferenceCount ();
    }

    // Proxies are never deleted until here.
    for (Proxies::iterator iter = m_proxies.begin (); iter != m_proxies.end ();)
        delete & (*iter++);
}

void ListenersBase::add_void (void* const listener, CallQueue& callQueue)
{
    ReadWriteMutex::ScopedWriteLockType lock (m_groups_mutex);

#if BEAST_DEBUG

    // Make sure the listener has not already been added
    // SHOULD USE const_iterator!
    for (Groups::iterator iter = m_groups.begin (); iter != m_groups.end ();)
    {
        Group* group = & (*iter++);

        // We can be in do_call() on another thread now, but it
        // doesn't modify the list, and we have the write lock.
        bassert (!group->contains (listener));
    }

#endif

    // See if we already have a Group for this thread queue.
    Group::Ptr group;

    // SHOULD USE const_iterator
    for (Groups::iterator iter = m_groups.begin (); iter != m_groups.end ();)
    {
        Group::Ptr cur = & (*iter++);

        if (&cur->getCallQueue () == &callQueue)
        {
            group = cur;
            break;
        }
    }

    if (!group)
    {
        group = new (m_allocator) Group (callQueue);

        // Add it to the list, and give it a manual ref
        // since the list currently uses raw pointers.
        group->incReferenceCount ();
        m_groups.push_back (*group);

        // Tell existing proxies to add the group
        ReadWriteMutex::ScopedReadLockType lock (m_proxies_mutex);

        for (Proxies::iterator iter = m_proxies.begin (); iter != m_proxies.end ();)
            (iter++)->add (group, *m_allocator);
    }

    // Add the listener to the group with the current timestamp
    group->add (listener, m_timestamp, *m_allocator);

    // Increment the timestamp within the mutex so
    // future calls will be newer than this listener.
    ++m_timestamp;
}

void ListenersBase::remove_void (void* const listener)
{
    ReadWriteMutex::ScopedWriteLockType lock (m_groups_mutex);

    // Make sure the listener exists
#if BEAST_DEBUG
    {
        bool exists = false;

        for (Groups::iterator iter = m_groups.begin (); iter != m_groups.end ();)
        {
            Group* group = & (*iter++);

            // this should never happen while we hold the mutex
            bassert (!group->empty ());

            if (group->contains (listener))
            {
                bassert (!exists); // added twice?

                exists = true;
                // keep going to make sure there are no empty groups
            }
        }

        bassert (exists);
    }
#endif

    // Find the group and remove
    for (Groups::iterator iter = m_groups.begin (); iter != m_groups.end ();)
    {
        Group::Ptr group = & (*iter++);

        // If the listener is in there, take it out.
        if (group->remove (listener))
        {
            // Are we the last listener?
            if (group->empty ())
            {
                // Tell proxies to remove the group
                {
                    ReadWriteMutex::ScopedWriteLockType lock (m_proxies_mutex);

                    for (Proxies::iterator iter = m_proxies.begin (); iter != m_proxies.end ();)
                    {
                        Proxy* proxy = & (*iter++);
                        proxy->remove (group);
                    }
                }

                // Remove it from the list and manually release
                // the reference since the list uses raw pointers.
                m_groups.erase (m_groups.iterator_to (*group));
                group->decReferenceCount ();

                // It is still possible for the group to exist at this
                // point in a thread queue but it will get processed,
                // do nothing, and release its own final reference.
            }

            break;
        }
    }
}

void ListenersBase::callp (Call::Ptr cp)
{
    Call* c = cp;

    ReadWriteMutex::ScopedReadLockType lock (m_groups_mutex);

    // can't be const iterator because queue() might cause called functors
    // to modify the list.
    for (Groups::iterator iter = m_groups.begin (); iter != m_groups.end ();)
        (iter++)->call (c, m_timestamp);
}

void ListenersBase::queuep (Call::Ptr cp)
{
    Call* c = cp;

    ReadWriteMutex::ScopedReadLockType lock (m_groups_mutex);

    // can't be const iterator because queue() might cause called functors
    // to modify the list.
    for (Groups::iterator iter = m_groups.begin (); iter != m_groups.end ();)
        (iter++)->queue (c, m_timestamp);
}

void ListenersBase::call1p_void (void* const listener, Call* c)
{
    ReadWriteMutex::ScopedReadLockType lock (m_groups_mutex);

    // can't be const iterator because queue() might cause called functors
    // to modify the list.
    for (Groups::iterator iter = m_groups.begin (); iter != m_groups.end ();)
    {
        Group* group = & (*iter++);

        if (group->contains (listener))
        {
            group->call1 (c, m_timestamp, listener);
            break;
        }
    }
}

void ListenersBase::queue1p_void (void* const listener, Call* c)
{
    ReadWriteMutex::ScopedReadLockType lock (m_groups_mutex);

    // can't be const iterator because queue() might cause called functors
    // to modify the list.
    for (Groups::iterator iter = m_groups.begin (); iter != m_groups.end ();)
    {
        Group* group = & (*iter++);

        if (group->contains (listener))
        {
            group->queue1 (c, m_timestamp, listener);
            break;
        }
    }
}

// Search for an existing Proxy that matches the pointer to
// member and replace it's Call, or create a new Proxy for it.
//
void ListenersBase::updatep (void const* const member,
                             const size_t bytes, Call::Ptr cp)
{
    Call* c = cp;

    ReadWriteMutex::ScopedReadLockType lock (m_groups_mutex);

    if (!m_groups.empty ())
    {
        Proxy* proxy;

        {
            ReadWriteMutex::ScopedReadLockType lock (m_proxies_mutex);

            // See if there's already a proxy
            proxy = find_proxy (member, bytes);
        }

        // Possibly create one
        if (!proxy)
        {
            ReadWriteMutex::ScopedWriteLockType lock (m_proxies_mutex);

            // Have to search for it again in case someone else added it
            proxy = find_proxy (member, bytes);

            if (!proxy)
            {
                // Create a new empty proxy
                proxy = new (m_allocator) Proxy (member, bytes);

                // Add all current groups to the Proxy.
                // We need the group read lock for this (caller provided).
                for (Groups::iterator iter = m_groups.begin (); iter != m_groups.end ();)
                {
                    Group* group = & (*iter++);
                    proxy->add (group, *m_allocator);
                }

                // Add it to the list.
                m_proxies.push_front (*proxy);
            }
        }

        // Requires the group read lock
        proxy->update (c, m_timestamp);
    }
}

// Searches for a proxy that matches the pointer to member.
// Caller synchronizes.
//
ListenersBase::Proxy* ListenersBase::find_proxy (const void* member, size_t bytes)
{
    for (Proxies::iterator iter = m_proxies.begin (); iter != m_proxies.end ();)
    {
        Proxy* proxy = & (*iter++);

        if (proxy->match (member, bytes))
            return proxy;
    }

    return 0;
}
