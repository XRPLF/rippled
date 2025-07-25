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

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/app/misc/detail/Work.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/json/json_value.h>

#include <boost/asio.hpp>

#include <mutex>
#include <optional>

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
        "validUntil", and @c "validators" field. @c "validUntil" contains the
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

    @li @c "refreshInterval" (optional, integer minutes).
        This value is clamped internally to [1,1440] (1 min - 1 day)
*/
class ValidatorSite
{
    friend class Work;

private:
    using error_code = boost::system::error_code;
    using clock_type = std::chrono::system_clock;
    using endpoint_type = boost::asio::ip::tcp::endpoint;

    struct Site
    {
        struct Status
        {
            clock_type::time_point refreshed;
            ListDisposition disposition;
            std::string message;
        };

        struct Resource
        {
            explicit Resource(std::string uri_);
            std::string const uri;
            parsedURL pUrl;
        };

        explicit Site(std::string uri);

        /// the original uri as loaded from config
        std::shared_ptr<Resource> loadedResource;

        /// the resource to request at <timer>
        /// intervals. same as loadedResource
        /// except in the case of a permanent redir.
        std::shared_ptr<Resource> startingResource;

        /// the active resource being requested.
        /// same as startingResource except
        /// when we've gotten a temp redirect
        std::shared_ptr<Resource> activeResource;

        unsigned short redirCount;
        std::chrono::minutes refreshInterval;
        clock_type::time_point nextRefresh;
        std::optional<Status> lastRefreshStatus;
        endpoint_type lastRequestEndpoint;
        bool lastRequestSuccessful;
    };

    Application& app_;
    beast::Journal const j_;

    // If both mutex are to be locked at the same time, `sites_mutex_` must be
    // locked before `state_mutex_` or we may deadlock.
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

    // time to allow for requests to complete
    std::chrono::seconds const requestTimeout_;

public:
    ValidatorSite(
        Application& app,
        std::optional<beast::Journal> j = std::nullopt,
        std::chrono::seconds timeout = std::chrono::seconds{20});
    ~ValidatorSite();

    /** Load configured site URIs.

        @param siteURIs List of URIs to fetch published validator lists

        @par Thread Safety

        May be called concurrently

        @return `false` if an entry is invalid or unparsable
    */
    bool
    load(std::vector<std::string> const& siteURIs);

    /** Start fetching lists from sites

        This does nothing if list fetching has already started

        @par Thread Safety

        May be called concurrently
    */
    void
    start();

    /** Wait for current fetches from sites to complete

        @par Thread Safety

        May be called concurrently
    */
    void
    join();

    /** Stop fetching lists from sites

        This blocks until list fetching has stopped

        @par Thread Safety

        May be called concurrently
    */
    void
    stop();

    /** Return JSON representation of configured validator sites
     */
    Json::Value
    getJson() const;

private:
    /// Load configured site URIs.
    bool
    load(
        std::vector<std::string> const& siteURIs,
        std::lock_guard<std::mutex> const&);

    /// Queue next site to be fetched
    /// lock over site_mutex_ and state_mutex_ required
    void
    setTimer(
        std::lock_guard<std::mutex> const&,
        std::lock_guard<std::mutex> const&);

    /// request took too long
    void
    onRequestTimeout(std::size_t siteIdx, error_code const& ec);

    /// Fetch site whose time has come
    void
    onTimer(std::size_t siteIdx, error_code const& ec);

    /// Store latest list fetched from site
    void
    onSiteFetch(
        boost::system::error_code const& ec,
        endpoint_type const& endpoint,
        detail::response_type&& res,
        std::size_t siteIdx);

    /// Store latest list fetched from anywhere
    void
    onTextFetch(
        boost::system::error_code const& ec,
        std::string const& res,
        std::size_t siteIdx);

    /// Initiate request to given resource.
    /// lock over sites_mutex_ required
    void
    makeRequest(
        std::shared_ptr<Site::Resource> resource,
        std::size_t siteIdx,
        std::lock_guard<std::mutex> const&);

    /// Parse json response from validator list site.
    /// lock over sites_mutex_ required
    void
    parseJsonResponse(
        std::string const& res,
        std::size_t siteIdx,
        std::lock_guard<std::mutex> const&);

    /// Interpret a redirect response.
    /// lock over sites_mutex_ required
    std::shared_ptr<Site::Resource>
    processRedirect(
        detail::response_type& res,
        std::size_t siteIdx,
        std::lock_guard<std::mutex> const&);

    /// If no sites are provided, or a site fails to load,
    /// get a list of local cache files from the ValidatorList.
    bool
    missingSite(std::lock_guard<std::mutex> const&);
};

}  // namespace ripple

#endif
