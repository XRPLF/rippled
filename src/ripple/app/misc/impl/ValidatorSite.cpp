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
#include <ripple/basics/Slice.h>
#include <ripple/json/json_reader.h>
#include <ripple/protocol/JsonFields.h>
#include <beast/core/detail/base64.hpp>
#include <boost/regex.hpp>

namespace ripple {

// default site query frequency - 5 minutes
auto constexpr DEFAULT_REFRESH_INTERVAL = std::chrono::minutes{5};

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
    if (timer_.expires_at().time_since_epoch().count())
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
        parsedURL pUrl;
        if (! parseUrl (pUrl, uri) ||
            (pUrl.scheme != "http" && pUrl.scheme != "https"))
        {
            JLOG (j_.error()) <<
                "Invalid validator site uri: " << uri;
            return false;
        }

        if (! pUrl.port)
            pUrl.port = (pUrl.scheme == "https") ? 443 : 80;

        sites_.push_back ({
            uri, pUrl, DEFAULT_REFRESH_INTERVAL, clock_type::now()});
    }

    JLOG (j_.debug()) <<
        "Loaded " << siteURIs.size() << " sites";

    return true;
}

void
ValidatorSite::start ()
{
    std::lock_guard <std::mutex> lock{state_mutex_};
    if (! timer_.expires_at().time_since_epoch().count())
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
        clock_type::now() + DEFAULT_REFRESH_INTERVAL;

    assert(! fetching_);
    fetching_ = true;

    std::shared_ptr<detail::Work> sp;
    if (sites_[siteIdx].pUrl.scheme == "https")
    {
        sp = std::make_shared<detail::WorkSSL>(
            sites_[siteIdx].pUrl.domain,
            sites_[siteIdx].pUrl.path,
            std::to_string(*sites_[siteIdx].pUrl.port),
            ios_,
            j_,
            [this, siteIdx](error_code const& err, detail::response_type&& resp)
            {
                onSiteFetch (err, std::move(resp), siteIdx);
            });
    }
    else
    {
        sp = std::make_shared<detail::WorkPlain>(
            sites_[siteIdx].pUrl.domain,
            sites_[siteIdx].pUrl.path,
            std::to_string(*sites_[siteIdx].pUrl.port),
            ios_,
            [this, siteIdx](error_code const& err, detail::response_type&& resp)
            {
                onSiteFetch (err, std::move(resp), siteIdx);
            });
    }

    work_ = sp;
    sp->run ();
}

void
ValidatorSite::onSiteFetch(
    boost::system::error_code const& ec,
    detail::response_type&& res,
    std::size_t siteIdx)
{
    if (! ec && res.result() != beast::http::status::ok)
    {
        std::lock_guard <std::mutex> lock{sites_mutex_};
        JLOG (j_.warn()) <<
            "Request for validator list at " <<
            sites_[siteIdx].uri << " returned " << res.result_int();

        sites_[siteIdx].lastRefreshStatus.emplace(
                Site::Status{clock_type::now(), ListDisposition::invalid});
    }
    else if (! ec)
    {
        std::lock_guard <std::mutex> lock{sites_mutex_};
        Json::Reader r;
        Json::Value body;
        if (r.parse(res.body.data(), body) &&
            body.isObject () &&
            body.isMember("blob") && body["blob"].isString () &&
            body.isMember("manifest") && body["manifest"].isString () &&
            body.isMember("signature") && body["signature"].isString() &&
            body.isMember("version") && body["version"].isInt())
        {

            auto const disp = validators_.applyList (
                body["manifest"].asString (),
                body["blob"].asString (),
                body["signature"].asString(),
                body["version"].asUInt());

            sites_[siteIdx].lastRefreshStatus.emplace(
                Site::Status{clock_type::now(), disp});

            if (ListDisposition::accepted == disp)
            {
                JLOG (j_.debug()) <<
                    "Applied new validator list from " <<
                    sites_[siteIdx].uri;
            }
            else if (ListDisposition::same_sequence == disp)
            {
                JLOG (j_.debug()) <<
                    "Validator list with current sequence from " <<
                    sites_[siteIdx].uri;
            }
            else if (ListDisposition::stale == disp)
            {
                JLOG (j_.warn()) <<
                    "Stale validator list from " << sites_[siteIdx].uri;
            }
            else if (ListDisposition::untrusted == disp)
            {
                JLOG (j_.warn()) <<
                    "Untrusted validator list from " <<
                    sites_[siteIdx].uri;
            }
            else if (ListDisposition::invalid == disp)
            {
                JLOG (j_.warn()) <<
                    "Invalid validator list from " <<
                    sites_[siteIdx].uri;
            }
            else if (ListDisposition::unsupported_version == disp)
            {
                JLOG (j_.warn()) <<
                    "Unsupported version validator list from " <<
                    sites_[siteIdx].uri;
            }
            else
            {
                BOOST_ASSERT(false);
            }

            if (body.isMember ("refresh_interval") &&
                body["refresh_interval"].isNumeric ())
            {
                sites_[siteIdx].refreshInterval =
                    std::chrono::minutes{body["refresh_interval"].asUInt ()};
            }
        }
        else
        {
            JLOG (j_.warn()) <<
                "Unable to parse JSON response from  " <<
                sites_[siteIdx].uri;

            sites_[siteIdx].lastRefreshStatus.emplace(
                Site::Status{clock_type::now(), ListDisposition::invalid});
        }
    }
    else
    {
        std::lock_guard <std::mutex> lock{sites_mutex_};
        sites_[siteIdx].lastRefreshStatus.emplace(
                Site::Status{clock_type::now(), ListDisposition::invalid});

        JLOG (j_.warn()) <<
            "Problem retrieving from " <<
            sites_[siteIdx].uri <<
            " " <<
            ec.value() <<
            ":" <<
            ec.message();
    }

    std::lock_guard <std::mutex> lock{state_mutex_};
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
            v[jss::uri] = site.uri;
            if (site.lastRefreshStatus)
            {
                v[jss::last_refresh_time] =
                    to_string(site.lastRefreshStatus->refreshed);
                v[jss::last_refresh_status] =
                    to_string(site.lastRefreshStatus->disposition);
            }

            v[jss::refresh_interval_min] =
                static_cast<Int>(site.refreshInterval.count());
        }
    }
    return jrr;
}
} // ripple
