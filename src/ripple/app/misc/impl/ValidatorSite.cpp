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

#include <ripple/app/misc/ValidatorList.h>
#include <ripple/app/misc/ValidatorSite.h>
#include <ripple/app/misc/detail/WorkPlain.h>
#include <ripple/app/misc/detail/WorkSSL.h>
#include <ripple/basics/base64.h>
#include <ripple/basics/Slice.h>
#include <ripple/json/json_reader.h>
#include <ripple/protocol/JsonFields.h>
#include <boost/regex.hpp>

namespace ripple {

// default site query frequency - 5 minutes
auto constexpr DEFAULT_REFRESH_INTERVAL = std::chrono::minutes{5};
auto constexpr ERROR_RETRY_INTERVAL = std::chrono::seconds{30};
unsigned short constexpr MAX_REDIRECTS = 3;

ValidatorSite::Site::Resource::Resource (std::string u)
    : uri {std::move(u)}
{
    if (! parseUrl (pUrl, uri) ||
        (pUrl.scheme != "http" && pUrl.scheme != "https"))
    {
        throw std::runtime_error {"invalid url"};
    }

    if (! pUrl.port)
        pUrl.port = (pUrl.scheme == "https") ? 443 : 80;
}

ValidatorSite::Site::Site (std::string uri)
    : loadedResource {std::make_shared<Resource>(std::move(uri))}
    , startingResource {loadedResource}
    , redirCount {0}
    , refreshInterval {DEFAULT_REFRESH_INTERVAL}
    , nextRefresh {clock_type::now()}
{
}

ValidatorSite::ValidatorSite (
    boost::asio::io_service& ios,
    ValidatorList& validators,
    beast::Journal j)
    : ios_ (ios)
    , validators_ (validators)
    , j_ (j)
    , timer_ (ios_)
    , fetching_ (false)
    , pending_ (false)
    , stopping_ (false)
{
}

ValidatorSite::~ValidatorSite()
{
    std::unique_lock<std::mutex> lock{state_mutex_};
    if (timer_.expires_at() > clock_type::time_point{})
    {
        if (! stopping_)
        {
            lock.unlock();
            stop();
        }
        else
        {
            cv_.wait(lock, [&]{ return ! fetching_; });
        }
    }
}

bool
ValidatorSite::load (
    std::vector<std::string> const& siteURIs)
{
    JLOG (j_.debug()) <<
        "Loading configured validator list sites";

    std::lock_guard <std::mutex> lock{sites_mutex_};

    for (auto uri : siteURIs)
    {
        try
        {
            sites_.emplace_back (uri);
        }
        catch (std::exception &)
        {
            JLOG (j_.error()) <<
                "Invalid validator site uri: " << uri;
            return false;
        }
    }

    JLOG (j_.debug()) <<
        "Loaded " << siteURIs.size() << " sites";

    return true;
}

void
ValidatorSite::start ()
{
    std::lock_guard <std::mutex> lock{state_mutex_};
    if (timer_.expires_at() == clock_type::time_point{})
        setTimer ();
}

void
ValidatorSite::join ()
{
    std::unique_lock<std::mutex> lock{state_mutex_};
    cv_.wait(lock, [&]{ return ! pending_; });
}

void
ValidatorSite::stop()
{
    std::unique_lock<std::mutex> lock{state_mutex_};
    stopping_ = true;
    cv_.wait(lock, [&]{ return ! fetching_; });

    if(auto sp = work_.lock())
        sp->cancel();

    error_code ec;
    timer_.cancel(ec);
    stopping_ = false;
    pending_ = false;
    cv_.notify_all();
}

void
ValidatorSite::setTimer ()
{
    std::lock_guard <std::mutex> lock{sites_mutex_};
    auto next = sites_.end();

    for (auto it = sites_.begin (); it != sites_.end (); ++it)
        if (next == sites_.end () || it->nextRefresh < next->nextRefresh)
            next = it;

    if (next != sites_.end ())
    {
        pending_ = next->nextRefresh <= clock_type::now();
        cv_.notify_all();
        timer_.expires_at (next->nextRefresh);
        timer_.async_wait (std::bind (&ValidatorSite::onTimer, this,
            std::distance (sites_.begin (), next),
                std::placeholders::_1));
    }
}

void
ValidatorSite::makeRequest (
    Site::ResourcePtr resource,
    std::size_t siteIdx,
    std::lock_guard<std::mutex>& lock)
{
    fetching_ = true;
    sites_[siteIdx].activeResource = resource;
    std::shared_ptr<detail::Work> sp;
    auto onFetch =
        [this, siteIdx] (error_code const& err, detail::response_type&& resp)
        {
            onSiteFetch (err, std::move(resp), siteIdx);
        };

    if (resource->pUrl.scheme == "https")
    {
        sp = std::make_shared<detail::WorkSSL>(
            resource->pUrl.domain,
            resource->pUrl.path,
            std::to_string(*resource->pUrl.port),
            ios_,
            j_,
            onFetch);
    }
    else
    {
        sp = std::make_shared<detail::WorkPlain>(
            resource->pUrl.domain,
            resource->pUrl.path,
            std::to_string(*resource->pUrl.port),
            ios_,
            onFetch);
    }

    work_ = sp;
    sp->run ();
}

void
ValidatorSite::onTimer (
    std::size_t siteIdx,
    error_code const& ec)
{
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec)
    {
        JLOG(j_.error()) <<
            "ValidatorSite::onTimer: " << ec.message();
        return;
    }

    std::lock_guard <std::mutex> lock{sites_mutex_};
    sites_[siteIdx].nextRefresh =
        clock_type::now() + sites_[siteIdx].refreshInterval;

    assert(! fetching_);
    sites_[siteIdx].redirCount = 0;
    makeRequest(sites_[siteIdx].startingResource, siteIdx, lock);
}

void
ValidatorSite::parseJsonResponse (
    detail::response_type& res,
    std::size_t siteIdx,
    std::lock_guard<std::mutex>& lock)
{
    Json::Reader r;
    Json::Value body;
    if (! r.parse(res.body().data(), body))
    {
        JLOG (j_.warn()) <<
            "Unable to parse JSON response from  " <<
            sites_[siteIdx].activeResource->uri;
        throw std::runtime_error{"bad json"};
    }

    if( ! body.isObject () ||
        ! body.isMember("blob")      || ! body["blob"].isString ()     ||
        ! body.isMember("manifest")  || ! body["manifest"].isString () ||
        ! body.isMember("signature") || ! body["signature"].isString() ||
        ! body.isMember("version")   || ! body["version"].isInt())
    {
        JLOG (j_.warn()) <<
            "Missing fields in JSON response from  " <<
            sites_[siteIdx].activeResource->uri;
        throw std::runtime_error{"missing fields"};
    }

    auto const disp = validators_.applyList (
        body["manifest"].asString (),
        body["blob"].asString (),
        body["signature"].asString(),
        body["version"].asUInt());

    sites_[siteIdx].lastRefreshStatus.emplace(
        Site::Status{clock_type::now(), disp, ""});

    if (ListDisposition::accepted == disp)
    {
        JLOG (j_.debug()) <<
            "Applied new validator list from " <<
            sites_[siteIdx].activeResource->uri;
    }
    else if (ListDisposition::same_sequence == disp)
    {
        JLOG (j_.debug()) <<
            "Validator list with current sequence from " <<
            sites_[siteIdx].activeResource->uri;
    }
    else if (ListDisposition::stale == disp)
    {
        JLOG (j_.warn()) <<
            "Stale validator list from " <<
            sites_[siteIdx].activeResource->uri;
    }
    else if (ListDisposition::untrusted == disp)
    {
        JLOG (j_.warn()) <<
            "Untrusted validator list from " <<
            sites_[siteIdx].activeResource->uri;
    }
    else if (ListDisposition::invalid == disp)
    {
        JLOG (j_.warn()) <<
            "Invalid validator list from " <<
            sites_[siteIdx].activeResource->uri;
    }
    else if (ListDisposition::unsupported_version == disp)
    {
        JLOG (j_.warn()) <<
            "Unsupported version validator list from " <<
            sites_[siteIdx].activeResource->uri;
    }
    else
    {
        BOOST_ASSERT(false);
    }

    if (body.isMember ("refresh_interval") &&
        body["refresh_interval"].isNumeric ())
    {
        // TODO: should we sanity check/clamp this value
        // to something reasonable?
        sites_[siteIdx].refreshInterval =
            std::chrono::minutes{body["refresh_interval"].asUInt ()};
    }
}

ValidatorSite::Site::ResourcePtr
ValidatorSite::processRedirect (
    detail::response_type& res,
    std::size_t siteIdx,
    std::lock_guard<std::mutex>& lock)
{
    using namespace boost::beast::http;
    Site::ResourcePtr newLocation;
    if (res.find(field::location) == res.end() ||
        res[field::location].empty())
    {
        JLOG (j_.warn()) <<
            "Request for validator list at " <<
            sites_[siteIdx].activeResource->uri <<
            " returned a redirect with no Location.";
        throw std::runtime_error{"missing location"};
    }

    if (sites_[siteIdx].redirCount == MAX_REDIRECTS)
    {
        JLOG (j_.warn()) <<
            "Exceeded max redirects for validator list at " <<
            sites_[siteIdx].loadedResource->uri ;
        throw std::runtime_error{"max redirects"};
    }

    JLOG (j_.debug()) <<
        "Got redirect for validator list from " <<
        sites_[siteIdx].activeResource->uri <<
        " to new location " << res[field::location];

    try
    {
        newLocation = std::make_shared<Site::Resource>(
            std::string(res[field::location]));
        sites_[siteIdx].redirCount++;
    }
    catch (std::exception &)
    {
        JLOG (j_.error()) <<
            "Invalid redirect location: " << res[field::location];
        throw;
    }
    return newLocation;
}

void
ValidatorSite::onSiteFetch(
    boost::system::error_code const& ec,
    detail::response_type&& res,
    std::size_t siteIdx)
{
    Site::ResourcePtr newLocation;
    bool shouldRetry = false;
    {
        std::lock_guard <std::mutex> lock_sites{sites_mutex_};
        try
        {
            if (ec)
            {
                JLOG (j_.warn()) <<
                    "Problem retrieving from " <<
                    sites_[siteIdx].activeResource->uri <<
                    " " <<
                    ec.value() <<
                    ":" <<
                    ec.message();
                shouldRetry = true;
                throw std::runtime_error{"fetch error"};
            }
            else
            {
                using namespace boost::beast::http;
                if (res.result() == status::ok)
                {
                    parseJsonResponse(res, siteIdx, lock_sites);
                }
                else if (res.result() == status::moved_permanently  ||
                         res.result() == status::permanent_redirect ||
                         res.result() == status::found              ||
                         res.result() == status::temporary_redirect)
                {
                    newLocation = processRedirect (res, siteIdx, lock_sites);
                    // for perm redirects, also update our starting URI
                    if (res.result() == status::moved_permanently ||
                        res.result() == status::permanent_redirect)
                    {
                        sites_[siteIdx].startingResource = newLocation;
                    }
                }
                else
                {
                    JLOG (j_.warn()) <<
                        "Request for validator list at " <<
                        sites_[siteIdx].activeResource->uri <<
                        " returned bad status: " <<
                        res.result_int();
                    shouldRetry = true;
                    throw std::runtime_error{"bad result code"};
                }

                if (newLocation)
                {
                    makeRequest(newLocation, siteIdx, lock_sites);
                    return; // we are still fetching, so skip
                            // state update/notify below
                }
            }
        }
        catch (std::exception& ex)
        {
            sites_[siteIdx].lastRefreshStatus.emplace(
                Site::Status{clock_type::now(),
                ListDisposition::invalid,
                ex.what()});
            if (shouldRetry)
                sites_[siteIdx].nextRefresh =
                    clock_type::now() + ERROR_RETRY_INTERVAL;
        }
        sites_[siteIdx].activeResource.reset();
    }

    std::lock_guard <std::mutex> lock_state{state_mutex_};
    fetching_ = false;
    if (! stopping_)
        setTimer ();
    cv_.notify_all();
}

Json::Value
ValidatorSite::getJson() const
{
    using namespace std::chrono;
    using Int = Json::Value::Int;

    Json::Value jrr(Json::objectValue);
    Json::Value& jSites = (jrr[jss::validator_sites] = Json::arrayValue);
    {
        std::lock_guard<std::mutex> lock{sites_mutex_};
        for (Site const& site : sites_)
        {
            Json::Value& v = jSites.append(Json::objectValue);
            std::stringstream uri;
            uri << site.loadedResource->uri;
            if (site.loadedResource != site.startingResource)
                uri << " (redirects to " << site.startingResource->uri + ")";
            v[jss::uri] = uri.str();
            v[jss::next_refresh_time] = to_string(site.nextRefresh);
            if (site.lastRefreshStatus)
            {
                v[jss::last_refresh_time] =
                    to_string(site.lastRefreshStatus->refreshed);
                v[jss::last_refresh_status] =
                    to_string(site.lastRefreshStatus->disposition);
                if (! site.lastRefreshStatus->message.empty())
                    v[jss::last_refresh_message] =
                        site.lastRefreshStatus->message;
            }
            v[jss::refresh_interval_min] =
                static_cast<Int>(site.refreshInterval.count());
        }
    }
    return jrr;
}
} // ripple
