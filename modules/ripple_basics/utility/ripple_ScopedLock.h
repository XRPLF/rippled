//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_SCOPEDLOCK_H
#define RIPPLE_SCOPEDLOCK_H

typedef boost::recursive_mutex::scoped_lock ScopedLock;

// A lock holder that can be returned and copied by value
// When the last reference goes away, the lock is released

// VFALCO TODO replace these with a more generic template, and not use boost
//
class SharedScopedLock
{
protected:
    mutable boost::shared_ptr<boost::recursive_mutex::scoped_lock> mHolder;

public:
    SharedScopedLock (boost::recursive_mutex& mutex) :
        mHolder (boost::make_shared<boost::recursive_mutex::scoped_lock> (boost::ref (mutex)))
    {
        ;
    }

    void lock () const
    {
        mHolder->lock ();
    }
    void unlock () const
    {
        mHolder->unlock ();
    }
};

// A class that unlocks on construction and locks on destruction

class ScopedUnlock
{
protected:
    bool mUnlocked;
    boost::recursive_mutex& mMutex;

public:
    // VFALCO TODO get rid of this unlock parameter to restore sanity
    ScopedUnlock (boost::recursive_mutex& mutex, bool unlock = true) : mUnlocked (unlock), mMutex (mutex)
    {
        if (unlock)
            mMutex.unlock ();
    }

    ~ScopedUnlock ()
    {
        if (mUnlocked)
            mMutex.lock ();
    }

    void lock ()
    {
        if (mUnlocked)
        {
            mMutex.lock ();
            mUnlocked = false;
        }
    }

    void unlock ()
    {
        if (!mUnlocked)
        {
            mUnlocked = true;
            mMutex.unlock ();
        }
    }

private:
    ScopedUnlock (const ScopedUnlock&); // no implementation
    ScopedUnlock& operator= (const ScopedUnlock&); // no implementation
};

#endif
