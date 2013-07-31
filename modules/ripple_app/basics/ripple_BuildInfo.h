//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_BUILDINFO_H_INCLUDED
#define RIPPLE_BUILDINFO_H_INCLUDED

/** Versioning information for this build. */
struct BuildInfo
{
    /** Server version.

        The server version has three parts:

            <major>     A non negative integer.
            <minor>     An integer between 0 and 999 inclusive.
            <suffix>    An optional string. For example, "rc1"

        The version string is formatted thusly:

            <major> '.' <minor> ['-' <suffix>]

        The minor version number is always padded with leading zeroes
        to bring the number of characters up to exactly three. For example,
        the server version string "12.045-rc1" has major version 12, minor
        version 45, and suffix "rc1". A suffix may only consist of lowercase
        letters and digits, and must start with a letter. The suffix may
        be up to 4 characters. The major version may not be prefixed with
        extra leading zeroes.

        The suffix for a new official release is usually omitted. If hotfixes
        are added to official releases they get a single leter suffix.

        Release candidates are marked with suffixes starting with "rc" and
        followed by a number starting from 1 to indicate the first
        release candidate, with subsequent release candidates incrementing
        the number. A final release candidate which becomes an official
        release loses the suffix. The next release candidate will have a
        new major or minor version number, and start back at "rc1".
    */
    static String const& getVersionString ();

    /** Full server version string.

        This includes the name of the server. It is used in the peer
        protocol hello message and also the headers of some HTTP replies.
    */
    static char const* getFullVersionString ();

    /** The server version's components. */
    struct Version
    {
        int    vmajor;  // 0+
        int    vminor;  // 0-999
        String suffix;  // Can be empty

        //----

        Version ();

        /** Convert a string to components.
            @return `false` if the string is improperly formatted.
        */
        bool parse (String const& s);

        /** Convert the components to a string. */
        String print () const noexcept;
    };

    //--------------------------------------------------------------------------

    /** The wire protocol version.

        The version consists of two unsigned 16 bit integers representing
        major and minor version numbers. All values are permissible.
    */
    struct Protocol
    {
        unsigned short vmajor;
        unsigned short vminor;

        //----

        /** The serialized format of the protocol version. */
        typedef uint32 PackedFormat;

        Protocol ();
        Protocol (unsigned short vmajor, unsigned short vminor);
        explicit Protocol (PackedFormat packedVersion);

        PackedFormat toPacked () const noexcept;

        std::string toStdString () const noexcept;

        bool operator== (Protocol const& other) const noexcept { return toPacked () == other.toPacked (); }
        bool operator!= (Protocol const& other) const noexcept { return toPacked () != other.toPacked (); }
        bool operator>= (Protocol const& other) const noexcept { return toPacked () >= other.toPacked (); }
        bool operator<= (Protocol const& other) const noexcept { return toPacked () <= other.toPacked (); }
        bool operator>  (Protocol const& other) const noexcept { return toPacked () >  other.toPacked (); }
        bool operator<  (Protocol const& other) const noexcept { return toPacked () <  other.toPacked (); }
    };

    /** The protocol version we speak and prefer. */
    static Protocol const& getCurrentProtocol ();

    /** The oldest protocol version we will accept. */
    static Protocol const& getMinimumProtocol ();

    //--------------------------------------------------------------------------
    //
    // DEPRECATED STUFF
    //

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
