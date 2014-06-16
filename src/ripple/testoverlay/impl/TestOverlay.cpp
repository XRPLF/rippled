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

#include <ripple/common/UnorderedContainers.h>

#include <beast/unit_test/suite.h>

namespace ripple {
namespace TestOverlay {

class Network1_test : public beast::unit_test::suite
{
public:
    template <class Config>
    class SeenState : public StateBase <Config>
    {
    public:
        SeenState ()
            : m_seen (0)
        {
        }

        void increment ()
        {
            ++m_seen;
        }

        int seen () const
        {
            return m_seen;
        }

    private:
        int m_seen;
    };

    //--------------------------------------------------------------------------

    template <class Config>
    class PeerLogic : public PeerLogicBase <Config>
    {
    public:
        typedef PeerLogicBase <Config> Base;
        typedef typename Config::Payload    Payload;
        typedef typename Base::Connection   Connection;
        typedef typename Base::Peer         Peer;
        typedef typename Base::Message      Message;
        typedef typename Config::SizeType   SizeType;

        explicit PeerLogic (Peer& peer)
            : PeerLogicBase <Config> (peer)
        {
        }

        ~PeerLogic ()
        {
        }

        void step ()
        {
            if (this->peer().id () == 1)
            {
                if (this->peer().network().steps() == 0)
                {
                    this->peer().network().state().increment();
                    this->peer().send_all (Payload (1));
                }
            }
        }

        void receive (Connection const& c, Message const& m)
        {
            if (this->peer().id () != 1)
            {
                this->peer().network().state().increment();
                this->peer().send_all_if (Message (m.id(),
                    m.payload().withHop ()),
                        typename Connection::IsNotPeer (c.peer()));
            }
        }
    };

    //--------------------------------------------------------------------------

    struct Params : ConfigType <
        Params,
        SeenState,
        PeerLogic
    >
    {
        typedef PremadeInitPolicy <250, 3> InitPolicy;
    };

    typedef Params::Network Network;

    //--------------------------------------------------------------------------

    void testCreation ()
    {
        Network network;

        Results result;
        for (int i = 0; result.received < 249 && i < 100; ++i)
        {
            using beast::String;
            String s =
                String ("step #") + String::fromNumber (
                network.steps()) + " ";
            result += network.step ();
            s << result.toString ();
            log << s.toStdString();
        }

        int const seen (network.state().seen());

        beast::String s = "Seen = " + beast::String::fromNumber (seen);
        log <<
            s.toStdString();
        pass ();
    }

    void run ()
    {
        testCreation ();
    }
};

//------------------------------------------------------------------------------
//
//
//
//------------------------------------------------------------------------------
/*

Concepts:

    Link
    Logic
    Message
    Network
    Peer

*/

/** UnaryPredicate, returns `true` if the 'to' peer on a Link matches. */
template <typename PeerType>
class is_to_pred
{
public:
    typedef PeerType Peer;

    is_to_pred (Peer const& to)
        : m_to (to)
    {
    }

    template <typename Link>
    bool operator() (Link const& l) const
    {
        return &m_to == &l.to();
    }

private:
    Peer const& m_to;
};

/** Returns a new is_to_pred for the specified peer. */
template <typename PeerType>
is_to_pred <PeerType> is_to (PeerType const& to)
{
    return is_to_pred <PeerType> (to);
}

/** Returns `true` if the peers are connected. */
template <typename PeerType>
bool is_connected (PeerType const& from, PeerType const& to)
{
    return std::find_if (from.links().begin(), from.links().end(),
        is_to (to)) != from.links().end();
}

//------------------------------------------------------------------------------

class BasicMessage
{
public:
    typedef std::size_t UniqueID;

    BasicMessage ()
        : m_id (0)
    {
    }

    explicit BasicMessage (UniqueID id)
        : m_id (id)
    {
    }

private:
    UniqueID m_id;
};

//------------------------------------------------------------------------------

template <typename PeerType, typename MessageType>
class BasicLink
{
public:
    typedef PeerType Peer;
    typedef MessageType Message;
    typedef std::vector <Message> Messages;

    BasicLink (Peer& to, Peer& from, bool inbound)
        : m_to (&to)
        , m_from (&from)
        , m_inbound (inbound)
    {
    }

    Peer const& to () const
    {
        return *m_to;
    }

    Peer& to ()
    {
        return *m_to;
    }

    Peer& from ()
    {
        return *m_from;
    }

    Peer const& from () const
    {
        return *m_from;
    }

    bool inbound() const
    {
        return m_inbound;
    }

    bool outbound() const
    {
        return ! m_inbound;
    }

    void send (MessageType const& m)
    {
        m_later.push_back (m);
    }

    void pre_step ()
    {
        std::swap (m_now, m_later);
    }

    void step ()
    {
        for (typename Messages::const_iterator iter (m_now.begin());
            iter != m_now.end(); ++iter)
            m_to->receive (*iter);
        m_now.clear();
    }

private:
    Peer* m_to;
    Peer* m_from;
    bool m_inbound;
    Messages m_now;
    Messages m_later;
};

//------------------------------------------------------------------------------

/** Models a peer. */
template <typename PeerType, typename MessageType>
class BasicPeer
{
public:
    typedef PeerType Peer;
    typedef MessageType Message;
    typedef BasicLink <Peer, Message> Link;
    typedef std::vector <Link> Links;

    ~BasicPeer ()
    {
        for (typename Links::iterator iter (links().begin());
            iter != links().end(); ++iter)
            iter->to().links().erase (std::find_if (
                iter->to().links().begin(), iter->to().links().end(),
                    is_to (peer())));
    }

    bool operator== (Peer const& rhs) const
    {
        return this == &rhs;
    }

    Peer& peer()
    {
        return *static_cast<Peer*>(this);
    }

    Peer const& peer() const
    {
        return *static_cast<Peer const*>(this);
    }

    Links& links()
    {
        return m_links;
    }

    Links const& links() const
    {
        return m_links;
    }

    void connect (Peer& to)
    {
        m_links.emplace_back (to, peer(), false);
        to.m_links.emplace_back (peer(), to, true);
    }

    void disconnect (Peer& to)
    {
        typename Links::iterator const iter (std::find_if (
            links().begin(), links().end(), is_to (to)));
        iter->to().links().erase (std::find_if (
            iter->to().links().begin(), iter->to().links.end(),
                is_to (peer())));
    }

    void send (Message const& m)
    {
        for (typename Links::iterator iter (links().begin());
            iter != links().end(); ++iter)
            iter->send (m);
    }

    void pre_step ()
    {
        for (typename Links::iterator iter (links().begin());
            iter != links().end(); ++iter)
            iter->pre_step ();
    }

    void step ()
    {
        for (typename Links::iterator iter (links().begin());
            iter != links().end(); ++iter)
            iter->step ();
    }

private:
    Links m_links;
};

//------------------------------------------------------------------------------

template <typename PeerContainer>
void iterate (PeerContainer& s)
{
    for (typename PeerContainer::iterator iter (s.begin()); iter != s.end();)
    {
        typename PeerContainer::iterator const cur (iter++);
        cur->pre_step();
    }

    for (typename PeerContainer::iterator iter (s.begin()); iter != s.end();)
    {
        typename PeerContainer::iterator const cur (iter++);
        cur->step();
    }
}

//------------------------------------------------------------------------------

template <
    typename LogicType
>
class BasicNetwork
{
public:
    typedef LogicType Logic;

    class Peer
    {
    public:
        explicit Peer (int)
        {
        }
    };

    typedef ripple::unordered_map <Peer, Logic> PeerMap;

    BasicNetwork()
    {
    }

    typename PeerMap::iterator emplace ()
    {
        m_map.emplace (1);
    }

private:
    PeerMap m_map;
};

//------------------------------------------------------------------------------

class Network2_test : public beast::unit_test::suite
{
public:
    class Message : public BasicMessage
    {
    public:
    };

    class Peer : public BasicPeer <Peer, Message>
    {
    public:
        bool m_received;
        int* m_count;

        Peer (int* count)
            : m_received (false)
            , m_count (count)
        {
        }

        ~Peer ()
        {
        }

        void receive (Message const& m)
        {
            if (m_received)
                return;
            ++*m_count;
            m_received = true;
            send (m);
        }
    };

    struct PeerLogic
    {
    };

    //--------------------------------------------------------------------------

    template <typename PeerContainer>
    void make_peers (PeerContainer& s,
        typename PeerContainer::size_type n,
            int* count)
    {
        while (n--)
            s.emplace_back(count);
    }

    template <typename RandomType, typename PeerContainer>
    void make_connections (
        PeerContainer& s,
        typename PeerContainer::size_type outDegree,
        RandomType r = RandomType())
    {
        // Turn the PeerContainer into a PeerSequence
        typedef typename PeerContainer::pointer pointer;
        typedef std::vector <pointer> PeerSequence;
        PeerSequence v;
        v.reserve (s.size());
        for (typename PeerContainer::iterator iter (s.begin());
            iter != s.end(); ++iter)
            v.push_back (&(*iter));

        // Make random connections
        typedef typename PeerSequence::size_type size_type;
        size_type const n (v.size());
        for (typename PeerSequence::iterator iter (v.begin());
            iter != v.end(); ++iter)
        {
            for (size_type i (0); i < outDegree; ++i)
            {
                for(;;)
                {
                    size_type const j (r.nextInt (n));

                    // prohibit self connection
                    if (*iter == v[j])
                        continue;

                    // prohibit duplicate connection
                    if (is_connected (**iter, *v[j]))
                        continue;

                    (*iter)->connect (*v [j]);
                    break;
                }
            }
        }
    }

    void test1 ()
    {
        int count (0);
        std::vector <Peer> peers;
        make_peers (peers, 10000, &count);
        make_connections (peers, 3, beast::Random());
        peers[0].send (Message ());
        for (int i = 0; i < 10; ++i)
        {
            iterate (peers);
            log <<
                "count = " << count;
        }
        pass();
    }

    void run ()
    {
        test1 ();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(Network1,overlay,ripple);
BEAST_DEFINE_TESTSUITE_MANUAL(Network2,overlay,ripple);

}
}
