//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

CountedObjects& CountedObjects::getInstance ()
{
    static CountedObjects instance;

    return instance;
}

CountedObjects::CountedObjects ()
{
}

CountedObjects::~CountedObjects ()
{
}

CountedObjects::List CountedObjects::getCounts (int minimumThreshold) const
{
    List counts;

    // When other operations are concurrent, the count
    // might be temporarily less than the actual count.
    int const count = m_count.get ();

    counts.reserve (count);

    CounterBase* counter = m_head.get ();

    while (counter != nullptr)
    {
        if (counter->getCount () >= minimumThreshold)
        {
            Entry entry;

            entry.first = counter->getName ();
            entry.second = counter->getCount ();

            counts.push_back (entry);
        }

        counter = counter->getNext ();
    }

    return counts;
}

//------------------------------------------------------------------------------

CountedObjects::CounterBase::CounterBase ()
{
    // Insert ourselves at the front of the lock-free linked list

    CountedObjects& instance = CountedObjects::getInstance ();
    CounterBase* head;

    do
    {
        head = instance.m_head.get ();
        m_next = head;
    }
    while (! instance.m_head.compareAndSetBool (this, head));

    ++instance.m_count;
}

CountedObjects::CounterBase::~CounterBase ()
{
    // VFALCO NOTE If the counters are destroyed before the singleton,
    //             undefined behavior will result if the singleton's member
    //             functions are called.
}

//------------------------------------------------------------------------------
