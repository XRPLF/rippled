//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_FUNCTIONAL_LOADSOURCE_H_INCLUDED
#define RIPPLE_CORE_FUNCTIONAL_LOADSOURCE_H_INCLUDED

/** Tracks the consumption of resources at an endpoint.

    To prevent monopolization of server resources or attacks on servers,
    resource consumption is monitored at each endpoint. When consumption
    exceeds certain thresholds, costs are imposed. Costs include charging
    additional XRP for transactions, requiring a proof of work to be
    performed, or simply disconnecting the endpoint.

    Currently, consumption endpoints include websocket connections used to
    service clients, and peer connections used to create the peer to peer
    overlay network implementing the Ripple protcool.

    The current "balance" of a LoadSource represents resource consumption
    debt or credit. Debt is accrued when bad loads are imposed. Credit is
    granted when good loads are imposed. When the balance crosses heuristic
    thresholds, costs are increased on the endpoint.

    The balance is represented as a unitless relative quantity.

    @note Although RPC connections consume resources, they are transient and
          cannot be rate limited. It is advised not to expose RPC interfaces
          to the general public.
*/
class LoadSource
{
public:
    // VFALCO TODO Use these dispositions
    /*
    enum Disposition
    {
        none,
        shouldWarn,
        shouldDrop,
    };
    */

    /** Construct a load source.

        Sources with admin privileges have relaxed or no restrictions
        on resource consumption.

        @param admin    `true` if the source should have admin privileges.
    */
    // VFALCO TODO See who is constructing this with a parameter
    explicit LoadSource (bool admin)
        : mBalance (0)
        , mFlags (admin ? lsfPrivileged : 0)
        , mLastUpdate (UptimeTimer::getInstance ().getElapsedSeconds ())
        , mLastWarning (0)
        , mLogged (false)
    {
    }

    /** Construct a load source with a given name.
        The endpoint is considered non-privileged.
    */
    explicit LoadSource (std::string const& name)
        : mName (name)
        , mBalance (0)
        , mFlags (0)
        , mLastUpdate (UptimeTimer::getInstance ().getElapsedSeconds ())
        , mLastWarning (0)
        , mLogged (false)
    {
    }

    /** Change the name of the source.

        An endpoint can be created before it's name is known. For example,
        on an incoming connection before the IP and port have been determined.
    */
    // VFALCO TODO Figure out a way to construct the LoadSource object with
    //             the proper name instead of renaming it later.
    //
    void rename (std::string const& name) noexcept
    {
        mName = name;
    }

    /** Retrieve the name of this endpoint.
    */
    std::string const& getName () const noexcept
    {
        return mName;
    }

    /** Determine if this endpoint is privileged.
    */
    bool isPrivileged () const noexcept
    {
        return (mFlags & lsfPrivileged) != 0;
    }

    /** Grant the privileged attribute on this endpoint.
    */
    void setPrivileged () noexcept
    {
        mFlags |= lsfPrivileged;
    }

    /** Retrieve the load debit or credit associated with the endpoint.

        The balance is represented in a unitless relative quantity
        indicating the heuristically weighted amount of resource consumption.
    */
    int getBalance () const noexcept
    {
        return mBalance;
    }

    /** Returns true if the endpoint received a log warning.
    */
    bool isLogged () const noexcept
    {
        return mLogged;
    }

    /** Reset the flag indicating the endpoint received a log warning.
    */
    void clearLogged () noexcept
    {
        mLogged = false;
    }

    /** Indicate that this endpoint is an outgoing connection.
    */
    void setOutbound () noexcept
    {
        mFlags |= lsfOutbound;
    }

    /** Returns true if this endpoint is an outgoing connection.
    */
    bool isOutbound () const
    {
        return (mFlags & lsfOutbound) != 0;
    }

private:
    // VFALCO Make this not a friend
    friend class LoadManagerImp;

    // VFALCO TODO Rename these for clarity
    static const int lsfPrivileged  = 1;
    static const int lsfOutbound    = 2;

private:
    std::string mName;
    int         mBalance;
    int         mFlags;
    int         mLastUpdate;
    int         mLastWarning;
    bool        mLogged;
};

#endif
