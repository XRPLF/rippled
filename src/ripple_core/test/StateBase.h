//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_TEST_STATEBASE_H_INCLUDED
#define RIPPLE_CORE_TEST_STATEBASE_H_INCLUDED

namespace TestOverlay
{

/* Base class for state information used by test objects. */
template <class Params>
class StateBase
{
public:
    // Identifies messages and peers.
    // Always starts at 1 and increases incrementally.
    //
    typedef uint64 UniqueID;

    StateBase ()
        : m_random (Params::randomSeedValue)
        , m_peerID (0)
        , m_messageID (0)
    {
    }

    Random& random ()
    {
        return m_random;
    }

    UniqueID nextPeerID ()
    {
        return ++m_peerID;
    }

    UniqueID nextMessageID ()
    {
        return ++m_messageID;
    }

private:
    Random m_random;
    UniqueID m_peerID;
    UniqueID m_messageID;
};

}

#endif
