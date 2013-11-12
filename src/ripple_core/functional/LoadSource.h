//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
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
    explicit LoadSource (std::string const& name, std::string const& costName)
        : mName (name)
        , mCostName (costName)
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
    void rename (std::string const& name, std::string const& costName) noexcept
    {
        mName = name;
        mCostName = costName;
    }

    /** Retrieve the name of this endpoint.
    */
    std::string const& getName () const noexcept
    {
        return mName;
    }

    std::string const& getCostName () const noexcept
    {
        return mCostName;
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
    std::string mName;         // Name of this particular load source, can include details like ports
    std::string mCostName;     // The name to "charge" for load from this connection
    int         mBalance;
    int         mFlags;
    int         mLastUpdate;
    int         mLastWarning;
    bool        mLogged;
};

#endif
