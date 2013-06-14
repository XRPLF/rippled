#ifndef RIPPLE_ILOADFEETRACK_H
#define RIPPLE_ILOADFEETRACK_H

/** Tracks the current fee and load schedule.
*/
class ILoadFeeTrack
{
public:
    static ILoadFeeTrack* New ();

    virtual ~ILoadFeeTrack () { }

    // Scale from fee units to millionths of a ripple
    virtual uint64 scaleFeeBase (uint64 fee, uint64 baseFee, uint32 referenceFeeUnits) = 0;

    // Scale using load as well as base rate
    virtual uint64 scaleFeeLoad (uint64 fee, uint64 baseFee, uint32 referenceFeeUnits, bool bAdmin) = 0;

    virtual uint32 getRemoteFee () = 0;
    virtual uint32 getLocalFee () = 0;

    virtual uint32 getLoadBase () = 0;
    virtual uint32 getLoadFactor () = 0;

    virtual Json::Value getJson (uint64 baseFee, uint32 referenceFeeUnits) = 0;

    virtual void setRemoteFee (uint32) = 0;
    virtual bool raiseLocalFee () = 0;
    virtual bool lowerLocalFee () = 0;
    virtual bool isLoaded () = 0;
};

#endif

// vim:ts=4
