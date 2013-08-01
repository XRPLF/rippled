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

        Follows the Semantic Versioning Specification:

        http://semver.org/
    */
    static String const& getVersionString ();

    /** Full server version string.

        This includes the name of the server. It is used in the peer
        protocol hello message and also the headers of some HTTP replies.
    */
    static char const* getFullVersionString ();

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

private:
    friend class BuildInfoTests;

    static char const* getRawVersionString ();
};

#endif
