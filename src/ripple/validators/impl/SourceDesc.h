//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORS_SOURCEDESC_H_INCLUDED
#define RIPPLE_VALIDATORS_SOURCEDESC_H_INCLUDED

namespace ripple {
namespace Validators {

/** Additional state information associated with a Source. */
struct SourceDesc
{
    enum Status
    {
        statusNone,
        statusFetched,
        statusFailed
    };

    ScopedPointer <Source> source;
    Status status;
    Time whenToFetch;
    int numberOfFailures;

    // The result of the last fetch
    Source::Result result;

    //------------------------------------------------------------------

    /** The time of the last successful fetch. */
    Time lastFetchTime;

    /** When to expire this source's list of cached results (if any) */
    Time expirationTime;

    //------------------------------------------------------------------

    SourceDesc () noexcept
        : status (statusNone)
        , whenToFetch (Time::getCurrentTime ())
        , numberOfFailures (0)
    {
    }

    ~SourceDesc ()
    {
    }
};

typedef DynamicList <SourceDesc> SourcesType;

}
}

#endif
