//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_INSTANCECOUNTER_H
#define RIPPLE_INSTANCECOUNTER_H

// VFALCO TODO Clean this up, remove the macros, replace
//         with a robust leak checker when we have atomics.
//

// VFALCO TODO swap these. Declaration means header, definition means .cpp!!!
#define DEFINE_INSTANCE(x)                              \
    extern InstanceType IT_##x;                         \
    class Instance_##x : private Instance               \
    {                                                   \
    protected:                                          \
        Instance_##x() : Instance(IT_##x) { ; }         \
        Instance_##x(const Instance_##x &) :            \
            Instance(IT_##x) { ; }                      \
        Instance_##x& operator=(const Instance_##x&)    \
        { return *this; }                               \
    }

#define DECLARE_INSTANCE(x)                             \
    InstanceType IT_##x(#x);

#define IS_INSTANCE(x) Instance_##x

// VFALCO NOTE that this is just a glorified leak checker with an awkward API
class InstanceType
{
protected:
    int                     mInstances;
    std::string             mName;
    boost::mutex            mLock;

    InstanceType*           mNextInstance;
    static InstanceType*    sHeadInstance;
    static bool             sMultiThreaded;

public:
    typedef std::pair<std::string, int> InstanceCount;

    explicit InstanceType (const char* n) : mInstances (0), mName (n)
    {
        mNextInstance = sHeadInstance;
        sHeadInstance = this;
    }

    static void multiThread ()
    {
        // We can support global objects and multi-threaded code, but not both
        // at the same time. Switch to multi-threaded.
        sMultiThreaded = true;
    }

    static void shutdown ()
    {
        sMultiThreaded = false;
    }

    static bool isMultiThread ()
    {
        return sMultiThreaded;
    }

    void addInstance ()
    {
        if (sMultiThreaded)
        {
            // VFALCO NOTE This will go away with atomics
            mLock.lock ();
            ++mInstances;
            mLock.unlock ();
        }
        else ++mInstances;
    }
    void decInstance ()
    {
        if (sMultiThreaded)
        {
            mLock.lock ();
            --mInstances;
            mLock.unlock ();
        }
        else --mInstances;
    }
    int getCount ()
    {
        boost::mutex::scoped_lock sl (mLock);
        return mInstances;
    }
    const std::string& getName ()
    {
        return mName;
    }

    static std::vector<InstanceCount> getInstanceCounts (int min = 1);
};

class Instance
{
protected:
    static bool running;
    InstanceType&   mType;

public:
    Instance (InstanceType& t) : mType (t)
    {
        mType.addInstance ();
    }
    ~Instance ()
    {
        if (running) mType.decInstance ();
    }
    static void shutdown ()
    {
        running = false;
    }
};

#endif
