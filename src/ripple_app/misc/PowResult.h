//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_POWRESULT_H_INCLUDED
#define RIPPLE_POWRESULT_H_INCLUDED

enum PowResult
{
    powOK       = 0,
    powREUSED   = 1, // already submitted
    powBADNONCE = 2, // you didn't solve it
    powEXPIRED  = 3, // time is up
    powCORRUPT  = 4,
    powTOOEASY  = 5, // the difficulty increased too much while you solved it
};

#endif
