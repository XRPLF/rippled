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

#include <functional>

namespace ripple {
namespace SiteFiles {

typedef beast::ScopedWrapperContext <
    beast::RecursiveMutex, beast::RecursiveMutex::ScopedLockType> SerializedContext;

class ManagerImp
    : public Manager
    , public beast::Thread
    , public beast::DeadlineTimer::Listener
    , public beast::LeakChecked <ManagerImp>
{
public:
    Logic m_logic;
    beast::Journal m_journal;
    beast::ServiceQueue m_queue;

    //--------------------------------------------------------------------------

    ManagerImp (Stoppable& stoppable, beast::Journal journal)
        : Manager (stoppable)
        , Thread ("SiteFiles")
        , m_logic (journal)
        , m_journal (journal)
    {
        // Turned off for now, this is for testing
        //addURL ("https://ripple.com/ripple.txt");
    }

    ~ManagerImp ()
    {
        stopThread ();
    }

    //--------------------------------------------------------------------------
    //
    // Manager
    //
    //--------------------------------------------------------------------------

    void addListener (SiteFiles::Listener& listener)
    {
        m_queue.post (std::bind (
            &Logic::addListener, &m_logic, std::ref (listener)));
    }

    void removeListener (SiteFiles::Listener& listener)
    {
        m_queue.post (std::bind (
            &Logic::removeListener, &m_logic, std::ref (listener)));
    }

    void addURL (std::string const& urlstr)
    {
        m_queue.post (std::bind (&Logic::addURL, &m_logic, urlstr));
    }

    //--------------------------------------------------------------------------
    //
    // Stoppable
    //
    //--------------------------------------------------------------------------

    void onPrepare ()
    {
    }

    void onStart ()
    {
        startThread();
    }

    void onStop ()
    {
        m_journal.debug << "Stopping";
        m_queue.stop ();
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //
    //--------------------------------------------------------------------------

    void onWrite (beast::PropertyStream::Map& map)
    {
        //SerializedContext::Scope scope (m_context);

        // ...
    }

    //--------------------------------------------------------------------------

    void onDeadlineTimer (beast::DeadlineTimer& timer)
    {
    }

    void run ()
    {
        m_journal.debug << "Started";
        m_queue.run();
        m_queue.reset();
        m_queue.poll();
        stopped();
    }
};

//------------------------------------------------------------------------------

Manager::Manager (Stoppable& parent)
    : Stoppable ("SiteFiles", parent)
    , beast::PropertyStream::Source ("sitefiles")
{
}

Manager* Manager::New (Stoppable& parent, beast::Journal journal)
{
    return new ManagerImp (parent, journal);
}

}
}
