//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TIMEDDESTROY_H_INCLUDED
#define RIPPLE_TIMEDDESTROY_H_INCLUDED

namespace detail
{

/** Template class that performs destruction of an object.
    Default implementation simply calls delete
*/
template <typename Object>
struct Destroyer
{
    static void destroy (Object& object)
    {
        delete &object;
    }
};

/** Specialization for boost::shared_ptr.
*/
template <typename Object>
struct Destroyer <boost::shared_ptr <Object> >
{
    static void destroy (boost::shared_ptr <Object>& p)
    {
        p.reset ();
    }
};

/** Specialization for boost::unordered_map
*/
template <typename Key, typename Value>
struct Destroyer <boost::unordered_map <Key, Value> >
{
    static void destroy (boost::unordered_map <Key, Value>& v)
    {
        v.clear ();
    }
};


}

//------------------------------------------------------------------------------

/** Measure the time required to destroy an object.
*/

template <typename Object>
double timedDestroy (Object& object)
{
    int64 const startTime (Time::getHighResolutionTicks ());

    detail::Destroyer <Object>::destroy (object);

    return Time::highResolutionTicksToSeconds (
            Time::getHighResolutionTicks () - startTime);
}

/** Log the destruction of an object if the time exceeds a threshold.
*/
template <typename PartitionKey, typename Object>
void logTimedDestroy (
    Object& object, String objectDescription, double thresholdSeconds = 1)
{
    double seconds = timedDestroy (object);

    if (seconds > thresholdSeconds)
    {
        LogSeverity const severity = lsWARNING;

        if (seconds >= 10)
            seconds = std::floor (seconds + 0.5);
        else
            seconds = static_cast <int> ((seconds * 10 + 0.5) / 10);

        Log (severity, LogPartition::get <PartitionKey> ()) <<
            objectDescription << " took "<<
            String (seconds) <<
            " seconds to destroy";
    }
}

#endif
