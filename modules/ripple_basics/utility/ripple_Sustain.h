//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_SUSTAIN_H
#define RIPPLE_SUSTAIN_H

// "Sustain" is a system for a buddy process that monitors the main process
// and relaunches it on a fault.
//
// VFALCO TODO Rename this and put it in a class.
// VFALCO TODO Reimplement cross-platform using beast::Process and its ilk
//
extern bool HaveSustain ();
extern std::string StopSustain ();
extern std::string DoSustain ();

#endif
