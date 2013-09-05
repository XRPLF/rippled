//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_TEST_MESSAGETYPE_H_INCLUDED
#define RIPPLE_CORE_TEST_MESSAGETYPE_H_INCLUDED

namespace TestOverlay
{

/** A message sent between peers. */
template <class Config>
class MessageType : public Config
{
public:
    typedef typename Config::State::UniqueID UniqueID;
    typedef typename Config::Payload Payload;

    MessageType ()
        : m_id (0)
    {
    }

    MessageType (UniqueID id, Payload payload)
        : m_id (id)
        , m_payload (payload)
    {
    }

    MessageType (MessageType const& other)
        : m_id (other.m_id)
        , m_payload (other.m_payload)
    {
    }

    MessageType& operator= (MessageType const& other)
    {
        m_id = other.m_id;
        m_payload = other.m_payload;
        return *this;
    }

    UniqueID id () const
    {
        return m_id;
    }

    Payload payload () const
    {
        return m_payload;
    }

private:
    UniqueID m_id;
    Payload m_payload;
};

}

#endif
