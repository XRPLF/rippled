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

#ifndef RIPPLE_SITEFILES_LOGIC_H_INCLUDED
#define RIPPLE_SITEFILES_LOGIC_H_INCLUDED

#include <ripple/common/UnorderedContainers.h>

#include <memory>

namespace ripple {
namespace SiteFiles {

/*
Config file format:

    Syntactically a series of lines, where line has this format:
        [ <vertical whitespace> ] <anything> ( <vertical-whitespace> OR <end-of-file> )

    Semantically a series of of zero or more sections, where each section
    has a name and optional data. Specifically, the format:
        ( <start-of-file> OR <header> ) <data>

    Data appearing before the first header goes into the section whose
    name is the empty string "".

    All lines are valid, errors are not possible. Each line matches one of
    the Comment, Header, or Data format:

    Comment:
        [ <horizontal whitespace> ] [ '#' <anything> ]

        Comment lines are ignored; The file is treated as if
        the comment lines do not exist.

    Header:
        [ <horizontal whitespace> ] '[' <anything> ']' [ <anything> ]

    Data:
        Anything not matching a comment or header.

    Lines in a data block are added to the section with the last name parsed,
    or the empty string if no header line has been seen yet.
*/
class Logic
{
public:
    typedef std::set <Listener*> Listeners;
    typedef ripple::unordered_map <beast::URL, SiteFile> SiteFiles;

    struct State
    {
        State()
        {
        }

        Listeners listeners;
        SiteFiles files;
    };

    typedef beast::SharedData <State> SharedState;

    SharedState m_state;
    beast::Journal m_journal;
    std::unique_ptr <beast::asio::HTTPClientBase> m_client;

    explicit Logic (beast::Journal journal)
        : m_journal (journal)
        , m_client (beast::asio::HTTPClientBase::New (journal))
    {
    }

    ~Logic ()
    {
    }

    //--------------------------------------------------------------------------
    //
    // Logic
    //
    //--------------------------------------------------------------------------

    void addListener (Listener& listener)
    {
        SharedState::Access state (m_state);

        // Notify the listener for each site file already added
        for (SiteFiles::const_iterator iter (state->files.begin());
            iter != state->files.end(); ++iter)
        {
            listener.onSiteFileFetch (iter->first.to_string(), iter->second);
        }

        state->listeners.insert (&listener);
    }

    void removeListener (Listener& listener)
    {
        SharedState::Access state (m_state);
        state->listeners.erase (&listener);
    }

    void addURL (std::string const& urlstr)
    {
        beast::ParsedURL const p (urlstr);

        if (p.error())
        {
            m_journal.error <<
                "Error parsing '" << urlstr << "'";
            return;
        }

        beast::URL const& url (p.url());

        auto const result (m_client->get (url));

        //---

        boost::system::error_code const error (result.first);

        if (error)
        {
            m_journal.error
                << "HTTP GET '" << url <<
                "' failed: " << error.message();
            return;
        }

        beast::HTTPResponse const& response (*result.second);

        processResponse (url, response);
    }

    //--------------------------------------------------------------------------
    //
    // Implementation
    //
    //--------------------------------------------------------------------------

    void processResponse (beast::URL const& url, beast::HTTPResponse const& response)
    {
        SharedState::Access state (m_state);

        std::pair <SiteFiles::iterator, bool> result (
            state->files.emplace (url, 0));

        if (! result.second)
        {
            m_journal.error <<
                "Duplicate URL '" << url << "' ignored";
            return;
        }

        SiteFile& siteFile (result.first->second);
        parse (siteFile, response);

        for (Listeners::iterator iter (state->listeners.begin());
            iter != state->listeners.end(); ++iter)
        {
            Listener* const listener (*iter);
            listener->onSiteFileFetch (url.to_string(), siteFile);
        }
    }

#if 0
    static boost::regex const& reHeader ()
    {
        static boost::regex re (
            "(?:\\v*)"      //      Line break (optional)
            "(?:\\h*)"      //      Horizontal whitespace (optional)
            "(?:\\[)"       //      Open bracket
            "([^\\]]*)"     // [1]  Everything between the brackets
            "(?:\\])"       //      Close bracket
            "(?:\\V*)"      //      Rest of the line
            , boost::regex::perl |
              boost::regex_constants::match_not_null
        );

        return re;
    }

    static boost::regex const& reComment ()
    {
        static boost::regex re (
            "(?:\\v)*"      //      Line break (optional)
            "(?:\\h*)"      //      Horizontal whitespace (optional)
            "(?:#\\V*)*"    //      Comment
            "(?:\\v*)"      //      Line break (optional)
            ,   boost::regex::perl
              | boost::regex_constants::match_not_null
        );

        return re;
    }

    static boost::regex const& reData ()
    {
        static boost::regex re (
            "(?:\\v|\\h)*"      //      Whitespace
            "(\\V*)"            // [1]  Rest of the line
            ,   boost::regex::perl
              | boost::regex_constants::match_not_null
        );

        return re;
    }
#else

    // regex debugger:
    //
    // https://www.debuggex.com/r/jwZFkNrqsouaTPHf
    //
    // (Thanks to J Lynn)
    //
    static boost::regex const& reHeader ()
    {
        static boost::regex re (
            "(?:\\h*(?:#\\V*)?\\v)*"    //      Zero or more comments
            "(?:\\v*)"                  //      Line break (optional)
            "(?:\\h*)"                  //      Horizontal whitespace (optional)
            "(?:\\[)"                   //      Open bracket
            "([^\\]]*)"                 // [1]  Everything between the brackets
            "(?:\\])"                   //      Close bracket
            "(?:\\V*)"                  //      Rest of the line
            "(?:\\h*(?:#\\V*)?\\v)*"    //      Zero or more comments
            , boost::regex::perl
        );

        return re;
    }

    static boost::regex const& reData ()
    {
        static boost::regex re (
            "(?:\\h*(?:#\\V*)?\\v)*"    //      Zero or more comments
            "(\\V*)"                    // [1]  Rest of the line
            "(?:\\h*(?:#\\V*)?\\v)*"    //      Zero or more comments
            ,   boost::regex::perl
        );

        return re;
    }
#endif
    template <typename BidirectionalIterator>
    void parse (
        SiteFile& siteFile,
        BidirectionalIterator start,
        BidirectionalIterator end)
    {
        Section* section (&siteFile.insert (""));

        boost::match_results <BidirectionalIterator> m;
        for (;start != end;)
        {
            if (boost::regex_search (start, end, m, reHeader(),
                boost::regex_constants::match_continuous))
            {
                std::string const& s (m[1]);
                section = &siteFile.insert (s);
            }
            else
            {
                boost::regex_search (start, end, m, reData(),
                                     boost::regex_constants::match_continuous);

                std::string const& s (m[1]);
                section->push_back (s);
            }

            start = m[0].second;
        }
    }

    void parse (SiteFile& siteFile, beast::HTTPResponse const& response)
    {
        std::string const s (response.body().to_string());
        parse (siteFile, s.begin(), s.end());
    }
};

}
}

#endif
