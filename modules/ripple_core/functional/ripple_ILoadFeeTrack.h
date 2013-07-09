//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_ILOADFEETRACK_RIPPLEHEADER
#define RIPPLE_ILOADFEETRACK_RIPPLEHEADER

/** Manages the current fee schedule.

    The "base" fee is the cost to send a reference transaction under no load,
    expressed in millionths of one XRP.

    The "load" fee is how much the local server currently charges to send a
    reference transaction. This fee fluctuates based on the load of the
    server.
*/
// VFALCO TODO Rename "load" to "current".
class ILoadFeeTrack
{
public:
    /** Create a new tracker.
    */
    static ILoadFeeTrack* New ();

    virtual ~ILoadFeeTrack () { }

    // Scale from fee units to millionths of a ripple
    virtual uint64 scaleFeeBase (uint64 fee, uint64 baseFee, uint32 referenceFeeUnits) = 0;

    // Scale using load as well as base rate
    virtual uint64 scaleFeeLoad (uint64 fee, uint64 baseFee, uint32 referenceFeeUnits, bool bAdmin) = 0;

    // VFALCO NOTE These appear to be unused, so I'm hiding the declarations.
    //
    //virtual uint32 getRemoteFee () = 0;
    //virtual uint32 getLocalFee () = 0;
    //virtual void setRemoteFee (uint32) = 0;

    virtual uint32 getLoadBase () = 0;
    virtual uint32 getLoadFactor () = 0;

    virtual Json::Value getJson (uint64 baseFee, uint32 referenceFeeUnits) = 0;

    virtual void setClusterFee (uint32) = 0;
    virtual uint32 getClusterFee () = 0;
    virtual bool raiseLocalFee () = 0;
    virtual bool lowerLocalFee () = 0;
    virtual bool isLoadedLocal () = 0;
    virtual bool isLoadedCluster () = 0;
};

#endif
