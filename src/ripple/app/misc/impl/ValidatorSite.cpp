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
#include <ripple/app/misc/detail/WorkFile.h>
#include <ripple/app/misc/detail/WorkPlain.h>
#include <ripple/app/misc/detail/WorkSSL.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/base64.h>
#include <ripple/json/json_reader.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/jss.h>
#include <boost/algorithm/clamp.hpp>
#include <boost/regex.hpp>
#include <algorithm>

namespace ripple {

auto constexpr default_refresh_interval = std::chrono::minutes{5};
auto constexpr error_retry_interval = std::chrono::seconds{30};
unsigned short constexpr max_redirects = 3;

ValidatorSite::Site::Resource::Resource(std::string uri_) : uri{std::move(uri_)}
{
    if (!parseUrl(pUrl, uri))
        throw std::runtime_error("URI '" + uri + "' cannot be parsed");

    if (pUrl.scheme == "file")
    {
        if (!pUrl.domain.empty())
            throw std::runtime_error("file URI cannot contain a hostname");

#if BOOST_OS_WINDOWS
        // Paths on Windows need the leading / removed
        if (pUrl.path[0] == '/')
            pUrl.path = pUrl.path.substr(1);
#endif

        if (pUrl.path.empty())
            throw std::runtime_error("file URI must contain a path");
    }
    else if (pUrl.scheme == "http")
    {
        if (pUrl.domain.empty())
            throw std::runtime_error("http URI must contain a hostname");

        if (!pUrl.port)
            pUrl.port = 80;
    }
    else if (pUrl.scheme == "https")
    {
        if (pUrl.domain.empty())
            throw std::runtime_error("https URI must contain a hostname");

        if (!pUrl.port)
            pUrl.port = 443;
    }
    else
        throw std::runtime_error("Unsupported scheme: '" + pUrl.scheme + "'");
}

ValidatorSite::Site::Site(std::string uri)
    : loadedResource{std::make_shared<Resource>(std::move(uri))}
    , startingResource{loadedResource}
    , redirCount{0}
    , refreshInterval{default_refresh_interval}
    , nextRefresh{clock_type::now()}
    , lastRequestEndpoint{}
    , lastRequestSuccessful{false}
{
}

ValidatorSite::ValidatorSite(
    Application& app,
    std::optional<beast::Journal> j,
    std::chrono::seconds timeout)
    : app_{app}
    , j_{j ? *j : app_.logs().journal("ValidatorSite")}
    , timer_{app_.getIOService()}
    , fetching_{false}
    , pending_{false}
    , stopping_{false}
    , requestTimeout_{timeout}
{
}

ValidatorSite::~ValidatorSite()
{
    std::unique_lock<std::mutex> lock{state_mutex_};
    if (timer_.expires_at() > clock_type::time_point{})
    {
        if (!stopping_)
        {
            lock.unlock();
            stop();
        }
        else
        {
            cv_.wait(lock, [&] { return !fetching_; });
        }
    }
}

bool
ValidatorSite::missingSite()
{
    auto const sites = app_.validators().loadLists();
    return sites.empty() || load(sites);
}

bool
ValidatorSite::load(std::vector<std::string> const& siteURIs)
{
    // If no sites are provided, act as if a site failed to load.
    if (siteURIs.empty())
    {
        return missingSite();
    }

    JLOG(j_.debug()) << "Loading configured validator list sites";

    std::lock_guard lock{sites_mutex_};

    for (auto const& uri : siteURIs)
    {
        try
        {
            sites_.emplace_back(uri);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.error())
                << "Invalid validator site uri: " << uri << ": " << e.what();
            return false;
        }
    }

    JLOG(j_.debug()) << "Loaded " << siteURIs.size() << " sites";

    return true;
}

void
ValidatorSite::start()
{
    std::lock_guard lock{state_mutex_};
    if (timer_.expires_at() == clock_type::time_point{})
        setTimer(lock);
}

void
ValidatorSite::join()
{
    std::unique_lock<std::mutex> lock{state_mutex_};
    cv_.wait(lock, [&] { return !pending_; });
}

void
ValidatorSite::stop()
{
    std::unique_lock<std::mutex> lock{state_mutex_};
    stopping_ = true;
    // work::cancel() must be called before the
    // cv wait in order to kick any asio async operations
    // that might be pending.
    if (auto sp = work_.lock())
        sp->cancel();
    cv_.wait(lock, [&] { return !fetching_; });

    // docs indicate cancel() can throw, but this should be
    // reconsidered if it changes to noexcept
    try
    {
        timer_.cancel();
    }
    catch (boost::system::system_error const&)
    {
    }
    stopping_ = false;
    pending_ = false;
    cv_.notify_all();
}

void
ValidatorSite::setTimer(std::lock_guard<std::mutex>& state_lock)
{
    std::lock_guard lock{sites_mutex_};

    auto next = std::min_element(
        sites_.begin(), sites_.end(), [](Site const& a, Site const& b) {
            return a.nextRefresh < b.nextRefresh;
        });

    if (next != sites_.end())
    {
        pending_ = next->nextRefresh <= clock_type::now();
        cv_.notify_all();
        timer_.expires_at(next->nextRefresh);
        auto idx = std::distance(sites_.begin(), next);
        timer_.async_wait([this, idx](boost::system::error_code const& ec) {
            this->onTimer(idx, ec);
        });
    }
}

void
ValidatorSite::makeRequest(
    std::shared_ptr<Site::Resource> resource,
    std::size_t siteIdx,
    std::lock_guard<std::mutex>& sites_lock)
{
    fetching_ = true;
    sites_[siteIdx].activeResource = resource;
    std::shared_ptr<detail::Work> sp;
    auto timeoutCancel = [this]() {
        std::lock_guard lock_state{state_mutex_};
        // docs indicate cancel_one() can throw, but this
        // should be reconsidered if it changes to noexcept
        try
        {
            timer_.cancel_one();
        }
        catch (boost::system::system_error const&)
        {
        }
    };
    auto onFetch = [this, siteIdx, timeoutCancel](
                       error_code const& err,
                       endpoint_type const& endpoint,
                       detail::response_type&& resp) {
        timeoutCancel();
        onSiteFetch(err, endpoint, std::move(resp), siteIdx);
    };

    auto onFetchFile = [this, siteIdx, timeoutCancel](
                           error_code const& err, std::string const& resp) {
        timeoutCancel();
        onTextFetch(err, resp, siteIdx);
    };

    JLOG(j_.debug()) << "Starting request for " << resource->uri;

    if (resource->pUrl.scheme == "https")
    {
        // can throw...
        sp = std::make_shared<detail::WorkSSL>(
            resource->pUrl.domain,
            resource->pUrl.path,
            std::to_string(*resource->pUrl.port),
            app_.getIOService(),
            j_,
            app_.config(),
            sites_[siteIdx].lastRequestEndpoint,
            sites_[siteIdx].lastRequestSuccessful,
            onFetch);
    }
    else if (resource->pUrl.scheme == "http")
    {
        sp = std::make_shared<detail::WorkPlain>(
            resource->pUrl.domain,
            resource->pUrl.path,
            std::to_string(*resource->pUrl.port),
            app_.getIOService(),
            sites_[siteIdx].lastRequestEndpoint,
            sites_[siteIdx].lastRequestSuccessful,
            onFetch);
    }
    else
    {
        BOOST_ASSERT(resource->pUrl.scheme == "file");
        sp = std::make_shared<detail::WorkFile>(
            resource->pUrl.path, app_.getIOService(), onFetchFile);
    }

    sites_[siteIdx].lastRequestSuccessful = false;
    work_ = sp;
    sp->run();
    // start a timer for the request, which shouldn't take more
    // than requestTimeout_ to complete
    std::lock_guard lock_state{state_mutex_};
    timer_.expires_after(requestTimeout_);
    timer_.async_wait([this, siteIdx](boost::system::error_code const& ec) {
        this->onRequestTimeout(siteIdx, ec);
    });
}

void
ValidatorSite::onRequestTimeout(std::size_t siteIdx, error_code const& ec)
{
    if (ec)
        return;

    {
        std::lock_guard lock_site{sites_mutex_};
        JLOG(j_.warn()) << "Request for " << sites_[siteIdx].activeResource->uri
                        << " took too long";
    }

    std::lock_guard lock_state{state_mutex_};
    if (auto sp = work_.lock())
        sp->cancel();
}

void
ValidatorSite::onTimer(std::size_t siteIdx, error_code const& ec)
{
    if (ec)
    {
        // Restart the timer if any errors are encountered, unless the error
        // is from the wait operation being aborted due to a shutdown request.
        if (ec != boost::asio::error::operation_aborted)
            onSiteFetch(ec, {}, detail::response_type{}, siteIdx);
        return;
    }

    try
    {
        std::lock_guard lock{sites_mutex_};
        sites_[siteIdx].nextRefresh =
            clock_type::now() + sites_[siteIdx].refreshInterval;
        sites_[siteIdx].redirCount = 0;
        // the WorkSSL client ctor can throw if SSL init fails
        makeRequest(sites_[siteIdx].startingResource, siteIdx, lock);
    }
    catch (std::exception&)
    {
        onSiteFetch(
            boost::system::error_code{-1, boost::system::generic_category()},
            {},
            detail::response_type{},
            siteIdx);
    }
}

void
ValidatorSite::parseJsonResponse(
    std::string const& res,
    std::size_t siteIdx,
    std::lock_guard<std::mutex>& sites_lock)
{
    Json::Value const body = [&res, siteIdx, this]() {
        Json::Reader r;
        Json::Value body;
        if (!r.parse(res.data(), body))
        {
            JLOG(j_.warn()) << "Unable to parse JSON response from  "
                            << sites_[siteIdx].activeResource->uri;
            throw std::runtime_error{"bad json"};
        }
        return body;
    }();

    auto const [valid, version, blobs] = [&body]() {
        // Check the easy fields first
        bool valid = body.isObject() && body.isMember(jss::manifest) &&
            body[jss::manifest].isString() && body.isMember(jss::version) &&
            body[jss::version].isInt();
        // Check the version-specific blob & signature fields
        std::uint32_t version;
        std::vector<ValidatorBlobInfo> blobs;
        if (valid)
        {
            version = body[jss::version].asUInt();
            blobs = ValidatorList::parseBlobs(version, body);
            valid = !blobs.empty();
        }
        return std::make_tuple(valid, version, blobs);
    }();

    if (!valid)
    {
        JLOG(j_.warn()) << "Missing fields in JSON response from  "
                        << sites_[siteIdx].activeResource->uri;
        throw std::runtime_error{"missing fields"};
    }

    auto const manifest = body[jss::manifest].asString();
    assert(version == body[jss::version].asUInt());
    auto const& uri = sites_[siteIdx].activeResource->uri;
    auto const hash = sha512Half(manifest, blobs, version);
    auto const applyResult = app_.validators().applyListsAndBroadcast(
        manifest,
        version,
        blobs,
        uri,
        hash,
        app_.overlay(),
        app_.getHashRouter(),
        app_.getOPs());

    sites_[siteIdx].lastRefreshStatus.emplace(
        Site::Status{clock_type::now(), applyResult.bestDisposition(), ""});

    for (auto const& [disp, count] : applyResult.dispositions)
    {
        switch (disp)
        {
            case ListDisposition::accepted:
                JLOG(j_.debug()) << "Applied " << count
                                 << " new validator list(s) from " << uri;
                break;
            case ListDisposition::expired:
                JLOG(j_.debug()) << "Applied " << count
                                 << " expired validator list(s) from " << uri;
                break;
            case ListDisposition::same_sequence:
                JLOG(j_.debug())
                    << "Ignored " << count
                    << " validator list(s) with current sequence from " << uri;
                break;
            case ListDisposition::pending:
                JLOG(j_.debug()) << "Processed " << count
                                 << " future validator list(s) from " << uri;
                break;
            case ListDisposition::known_sequence:
                JLOG(j_.debug())
                    << "Ignored " << count
                    << " validator list(s) with future known sequence from "
                    << uri;
                break;
            case ListDisposition::stale:
                JLOG(j_.warn()) << "Ignored " << count
                                << "stale validator list(s) from " << uri;
                break;
            case ListDisposition::untrusted:
                JLOG(j_.warn()) << "Ignored " << count
                                << " untrusted validator list(s) from " << uri;
                break;
            case ListDisposition::invalid:
                JLOG(j_.warn()) << "Ignored " << count
                                << " invalid validator list(s) from " << uri;
                break;
            case ListDisposition::unsupported_version:
                JLOG(j_.warn())
                    << "Ignored " << count
                    << " unsupported version validator list(s) from " << uri;
                break;
            default:
                BOOST_ASSERT(false);
        }
    }

    if (body.isMember(jss::refresh_interval) &&
        body[jss::refresh_interval].isNumeric())
    {
        using namespace std::chrono_literals;
        std::chrono::minutes const refresh = boost::algorithm::clamp(
            std::chrono::minutes{body[jss::refresh_interval].asUInt()},
            1min,
            24h);
        sites_[siteIdx].refreshInterval = refresh;
        sites_[siteIdx].nextRefresh =
            clock_type::now() + sites_[siteIdx].refreshInterval;
    }
}

std::shared_ptr<ValidatorSite::Site::Resource>
ValidatorSite::processRedirect(
    detail::response_type& res,
    std::size_t siteIdx,
    std::lock_guard<std::mutex>& sites_lock)
{
    using namespace boost::beast::http;
    std::shared_ptr<Site::Resource> newLocation;
    if (res.find(field::location) == res.end() || res[field::location].empty())
    {
        JLOG(j_.warn()) << "Request for validator list at "
                        << sites_[siteIdx].activeResource->uri
                        << " returned a redirect with no Location.";
        throw std::runtime_error{"missing location"};
    }

    if (sites_[siteIdx].redirCount == max_redirects)
    {
        JLOG(j_.warn()) << "Exceeded max redirects for validator list at "
                        << sites_[siteIdx].loadedResource->uri;
        throw std::runtime_error{"max redirects"};
    }

    JLOG(j_.debug()) << "Got redirect for validator list from "
                     << sites_[siteIdx].activeResource->uri
                     << " to new location " << res[field::location];

    try
    {
        newLocation =
            std::make_shared<Site::Resource>(std::string(res[field::location]));
        ++sites_[siteIdx].redirCount;
        if (newLocation->pUrl.scheme != "http" &&
            newLocation->pUrl.scheme != "https")
            throw std::runtime_error(
                "invalid scheme in redirect " + newLocation->pUrl.scheme);
    }
    catch (std::exception&)
    {
        JLOG(j_.error()) << "Invalid redirect location: "
                         << res[field::location];
        throw;
    }
    return newLocation;
}

void
ValidatorSite::onSiteFetch(
    boost::system::error_code const& ec,
    endpoint_type const& endpoint,
    detail::response_type&& res,
    std::size_t siteIdx)
{
    {
        std::lock_guard lock_sites{sites_mutex_};
        if (endpoint != endpoint_type{})
            sites_[siteIdx].lastRequestEndpoint = endpoint;
        JLOG(j_.debug()) << "Got completion for "
                         << sites_[siteIdx].activeResource->uri << " "
                         << endpoint;
        auto onError = [&](std::string const& errMsg, bool retry) {
            sites_[siteIdx].lastRefreshStatus.emplace(Site::Status{
                clock_type::now(), ListDisposition::invalid, errMsg});
            if (retry)
                sites_[siteIdx].nextRefresh =
                    clock_type::now() + error_retry_interval;

            // See if there's a copy saved locally from last time we
            // saw the list.
            missingSite();
        };
        if (ec)
        {
            JLOG(j_.warn())
                << "Problem retrieving from "
                << sites_[siteIdx].activeResource->uri << " " << endpoint << " "
                << ec.value() << ":" << ec.message();
            onError("fetch error", true);
        }
        else
        {
            try
            {
                using namespace boost::beast::http;
                switch (res.result())
                {
                    case status::ok:
                        sites_[siteIdx].lastRequestSuccessful = true;
                        parseJsonResponse(res.body(), siteIdx, lock_sites);
                        break;
                    case status::moved_permanently:
                    case status::permanent_redirect:
                    case status::found:
                    case status::temporary_redirect: {
                        auto newLocation =
                            processRedirect(res, siteIdx, lock_sites);
                        assert(newLocation);
                        // for perm redirects, also update our starting URI
                        if (res.result() == status::moved_permanently ||
                            res.result() == status::permanent_redirect)
                        {
                            sites_[siteIdx].startingResource = newLocation;
                        }
                        makeRequest(newLocation, siteIdx, lock_sites);
                        return;  // we are still fetching, so skip
                                 // state update/notify below
                    }
                    default: {
                        JLOG(j_.warn())
                            << "Request for validator list at "
                            << sites_[siteIdx].activeResource->uri << " "
                            << endpoint
                            << " returned bad status: " << res.result_int();
                        onError("bad result code", true);
                    }
                }
            }
            catch (std::exception& ex)
            {
                onError(ex.what(), false);
            }
        }
        sites_[siteIdx].activeResource.reset();
    }

    std::lock_guard lock_state{state_mutex_};
    fetching_ = false;
    if (!stopping_)
        setTimer(lock_state);
    cv_.notify_all();
}

void
ValidatorSite::onTextFetch(
    boost::system::error_code const& ec,
    std::string const& res,
    std::size_t siteIdx)
{
    {
        std::lock_guard lock_sites{sites_mutex_};
        try
        {
            if (ec)
            {
                JLOG(j_.warn()) << "Problem retrieving from "
                                << sites_[siteIdx].activeResource->uri << " "
                                << ec.value() << ": " << ec.message();
                throw std::runtime_error{"fetch error"};
            }

            sites_[siteIdx].lastRequestSuccessful = true;

            parseJsonResponse(res, siteIdx, lock_sites);
        }
        catch (std::exception& ex)
        {
            sites_[siteIdx].lastRefreshStatus.emplace(Site::Status{
                clock_type::now(), ListDisposition::invalid, ex.what()});
        }
        sites_[siteIdx].activeResource.reset();
    }

    std::lock_guard lock_state{state_mutex_};
    fetching_ = false;
    if (!stopping_)
        setTimer(lock_state);
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
        std::lock_guard lock{sites_mutex_};
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
                if (!site.lastRefreshStatus->message.empty())
                    v[jss::last_refresh_message] =
                        site.lastRefreshStatus->message;
            }
            v[jss::refresh_interval_min] =
                static_cast<Int>(site.refreshInterval.count());
        }
    }
    return jrr;
}
}  // namespace ripple
