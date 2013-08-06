//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_BUILDVERSION_RIPPLEHEADER
#define RIPPLE_BUILDVERSION_RIPPLEHEADER

/** Versioning information for the build.
*/

class BuildVersion
{
public:
    /** Retrieve the build version number.

        This is typically incremented when an official version is publshed
        with a list of changes.

        Format is:

            <major>.<minor>.<bugfix>
    */
    static char const* getBuildVersion ()
    {
        return "0.0.1";
    }

    /** Retrieve the client API version number.

        The client API version is incremented whenever a new feature
        or breaking change is made to the websocket / RPC interface.

        Format is:

            <version-number>
    */
    static char const* getClientVersion ()
    {
        return "1";
    }
};

#endif
