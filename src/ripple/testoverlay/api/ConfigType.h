//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TESTOVERLAY_CONFIGTYPE_H_INCLUDED
#define RIPPLE_TESTOVERLAY_CONFIGTYPE_H_INCLUDED

/** A simulated peer to peer network for unit tests. */
namespace TestOverlay
{

/** Combines Params with standard Config requirements for test objects. */
template <
    class Params,
    template <class> class StateType = StateBase,
    template <class> class PeerLogicType = PeerLogicBase
>
class ConfigType
{
public:
    // These defaults can be overridden in
    // Params simply by adding declarations to it.

    static int64 const                      randomSeedValue  = 42;

    typedef std::size_t                     SizeType;

    typedef SimplePayload                   Payload;

    typedef StateType <Params>              State;
    typedef MessageType <Params>            Message;
    typedef NetworkType <Params>            Network;

    typedef PeerType <Params>               Peer;
    typedef PeerLogicType <Params>          PeerLogic;

    typedef NoInitPolicy                    InitPolicy;
};

}

#endif
