//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_MISC_VALIDATORSITE_H_INCLUDED
#define RIPPLE_APP_MISC_VALIDATORSITE_H_INCLUDED

#include <ripple/app/misc/ValidatorList.h>
#include <ripple/app/misc/detail/Work.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/json/json_value.h>
#include <boost/asio.hpp>
#include <mutex>

namespace ripple {

/**
    Validator Sites
    ---------------

    This class manages the set of configured remote sites used to fetch the
    latest published recommended validator lists.

    Lists are fetched at a regular interval.
    Fetched lists are expected to be in JSON format and contain the following
    fields:

    @li @c "blob": Base64-encoded JSON string containing a @c "sequence", @c
        "expiration", and @c "validators" field. @c "expiration" contains the
        Ripple timestamp (seconds since January 1st, 2000 (00:00 UTC)) for when
        the list expires. @c "validators" contains an array of objects with a
        @c "validation_public_key" and optional @c "manifest" field.
        @c "validation_public_key" should be the hex-encoded master public key.
        @c "manifest" should be the base64-encoded validator manifest.

    @li @c "manifest": Base64-encoded serialization of a manifest containing the
        publisher's master and signing public keys.

    @li @c "signature": Hex-encoded signature of the blob using the publisher's
        signing key.

    @li @c "version": 1

    @li @c "refreshInterval" (optional)
*/
class ValidatorSite
{
    friend class Work;

private:
    using error_code = boost::system::error_code;
    using clock_type = std::chrono::system_clock;

    struct Site
    {
        struct Status
        {
            clock_type::time_point refreshed;
            ListDisposition disposition;
        };

        std::string uri;
        parsedURL pUrl;
        std::chrono::minutes refreshInterval;
        clock_type::time_point nextRefresh;
        boost::optional<Status> lastRefreshStatus;
    };

    boost::asio::io_service& ios_;
    ValidatorList& validators_;
    beast::Journal j_;
    std::mutex mutable sites_mutex_;
    std::mutex mutable state_mutex_;

    std::condition_variable cv_;
    std::weak_ptr<detail::Work> work_;
    boost::asio::basic_waitable_timer<clock_type> timer_;

    // A list is currently being fetched from a site
    std::atomic<bool> fetching_;

    // One or more lists are due to be fetched
    std::atomic<bool> pending_;
    std::atomic<bool> stopping_;

    // The configured list of URIs for fetching lists
    std::vector<Site> sites_;

public:
    ValidatorSite (
        boost::asio::io_service& ios,
        ValidatorList& validators,
        beast::Journal j);
    ~ValidatorSite ();

    /** Load configured site URIs.

        @param siteURIs List of URIs to fetch published validator lists

        @par Thread Safety

        May be called concurrently

        @return `false` if an entry is invalid or unparsable
    */
    bool
    load (
        std::vector<std::string> const& siteURIs);

    /** Start fetching lists from sites

        This does nothing if list fetching has already started

        @par Thread Safety

        May be called concurrently
    */
    void
    start ();

    /** Wait for current fetches from sites to complete

        @par Thread Safety

        May be called concurrently
    */
    void
    join ();

    /** Stop fetching lists from sites

        This blocks until list fetching has stopped

        @par Thread Safety

        May be called concurrently
    */
    void
    stop ();

    /** Return JSON representation of configured validator sites
     */
    Json::Value
    getJson() const;

private:
    /// Queue next site to be fetched
    void
    setTimer ();

    /// Fetch site whose time has come
    void
    onTimer (
        std::size_t siteIdx,
        error_code const& ec);

    /// Store latest list fetched from site
    void
    onSiteFetch (
        boost::system::error_code const& ec,
        detail::response_type&& res,
        std::size_t siteIdx);
};

} // ripple

#endif
