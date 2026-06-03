#include <xrpld/app/misc/ValidatorSite.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/app/misc/detail/Work.h>
#include <xrpld/app/misc/detail/WorkFile.h>
#include <xrpld/app/misc/detail/WorkPlain.h>
#include <xrpld/app/misc/detail/WorkSSL.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/SlabAllocator.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>

#include <boost/asio/error.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/impl/serializer.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/detail/generic_category.hpp>
#include <boost/system/system_error.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl {

constexpr auto kDefaultRefreshInterval = std::chrono::minutes{5};
constexpr auto kErrorRetryInterval = std::chrono::seconds{30};
unsigned constexpr short kMaxRedirects = 3;

ValidatorSite::Site::Resource::Resource(std::string inUri) : uri{std::move(inUri)}
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
    {
        throw std::runtime_error("Unsupported scheme: '" + pUrl.scheme + "'");
    }
}

ValidatorSite::Site::Site(std::string uri)
    : loadedResource{std::make_shared<Resource>(std::move(uri))}
    , startingResource{loadedResource}
    , refreshInterval{kDefaultRefreshInterval}
    , nextRefresh{clock_type::now()}

{
}

ValidatorSite::ValidatorSite(
    Application& app,
    std::optional<beast::Journal> j,
    std::chrono::seconds timeout)
    : app_{app}
    , j_{j ? *j : app_.getJournal("ValidatorSite")}
    , timer_{app_.getIOContext()}
    , fetching_{false}
    , pending_{false}
    , stopping_{false}
    , requestTimeout_{timeout}
{
}

ValidatorSite::~ValidatorSite()
{
    std::unique_lock<std::mutex> lock{stateMutex_};
    if (timer_.expiry() > clock_type::time_point{})
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
ValidatorSite::missingSite(std::scoped_lock<std::mutex> const& lockSites)
{
    auto const sites = app_.getValidators().loadLists();
    return sites.empty() || load(sites, lockSites);
}

bool
ValidatorSite::load(std::vector<std::string> const& siteURIs)
{
    JLOG(j_.debug()) << "Loading configured validator list sites";

    std::scoped_lock const lock{sitesMutex_};

    return load(siteURIs, lock);
}

bool
ValidatorSite::load(
    std::vector<std::string> const& siteURIs,
    std::scoped_lock<std::mutex> const& lockSites)
{
    // If no sites are provided, act as if a site failed to load.
    if (siteURIs.empty())
    {
        return missingSite(lockSites);
    }

    for (auto const& uri : siteURIs)
    {
        try
        {
            sites_.emplace_back(uri);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.error()) << "Invalid validator site uri: " << uri << ": " << e.what();
            return false;
        }
    }

    JLOG(j_.debug()) << "Loaded " << siteURIs.size() << " sites";

    return true;
}

void
ValidatorSite::start()
{
    std::scoped_lock const l0{sitesMutex_};
    std::scoped_lock const l1{stateMutex_};
    if (timer_.expiry() == clock_type::time_point{})
        setTimer(l0, l1);
}

void
ValidatorSite::join()
{
    std::unique_lock<std::mutex> lock{stateMutex_};
    cv_.wait(lock, [&] { return !pending_; });
}

void
ValidatorSite::stop()
{
    std::unique_lock<std::mutex> lock{stateMutex_};
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
    catch (boost::system::system_error const&)  // NOLINT(bugprone-empty-catch)
    {
    }
    stopping_ = false;
    pending_ = false;
    cv_.notify_all();
}

void
ValidatorSite::setTimer(
    std::scoped_lock<std::mutex> const& siteLock,
    std::scoped_lock<std::mutex> const& stateLock)
{
    auto next = std::ranges::min_element(
        sites_, [](Site const& a, Site const& b) { return a.nextRefresh < b.nextRefresh; });

    if (next != sites_.end())
    {
        pending_ = next->nextRefresh <= clock_type::now();
        cv_.notify_all();
        timer_.expires_at(next->nextRefresh);
        auto idx = std::distance(sites_.begin(), next);
        timer_.async_wait(
            [this, idx](boost::system::error_code const& ec) { this->onTimer(idx, ec); });
    }
}

void
ValidatorSite::makeRequest(
    std::shared_ptr<Site::Resource> resource,
    std::size_t siteIdx,
    std::scoped_lock<std::mutex> const& sitesLock)
{
    fetching_ = true;
    sites_[siteIdx].activeResource = resource;
    std::shared_ptr<detail::Work> sp;
    auto timeoutCancel = [this]() {
        std::scoped_lock const lockState{stateMutex_};
        // docs indicate cancel_one() can throw, but this
        // should be reconsidered if it changes to noexcept
        try
        {
            timer_.cancel_one();
        }
        catch (boost::system::system_error const&)  // NOLINT(bugprone-empty-catch)
        {
        }
    };
    auto onFetch = [this, siteIdx, timeoutCancel](
                       error_code const& err,
                       endpoint_type const& endpoint,
                       detail::response_type const& resp) {
        timeoutCancel();
        onSiteFetch(err, endpoint, resp, siteIdx);
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
            std::to_string(*resource->pUrl.port),  // NOLINT(bugprone-unchecked-optional-access)
                                                   // port defaulted at parse time
            app_.getIOContext(),
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
            std::to_string(*resource->pUrl.port),  // NOLINT(bugprone-unchecked-optional-access)
                                                   // port defaulted at parse time
            app_.getIOContext(),
            sites_[siteIdx].lastRequestEndpoint,
            sites_[siteIdx].lastRequestSuccessful,
            onFetch);
    }
    else
    {
        BOOST_ASSERT(resource->pUrl.scheme == "file");
        sp = std::make_shared<detail::WorkFile>(
            resource->pUrl.path, app_.getIOContext(), onFetchFile);
    }

    sites_[siteIdx].lastRequestSuccessful = false;
    work_ = sp;
    sp->run();
    // start a timer for the request, which shouldn't take more
    // than requestTimeout_ to complete
    std::scoped_lock const lockState{stateMutex_};
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
        std::scoped_lock const lockSite{sitesMutex_};
        // In some circumstances, both this function and the response
        // handler (onSiteFetch or onTextFetch) can get queued and
        // processed. In all observed cases, the response handler
        // processes a network error. Usually, this function runs first,
        // but on extremely rare occasions, the response handler can run
        // first, which will leave activeResource empty.
        auto const& site = sites_[siteIdx];
        if (site.activeResource)
        {
            JLOG(j_.warn()) << "Request for " << site.activeResource->uri << " took too long";
        }
        else
            JLOG(j_.error()) << "Request took too long, but a response has "
                                "already been processed";
    }

    std::scoped_lock const lockState{stateMutex_};
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
        std::scoped_lock const lock{sitesMutex_};
        sites_[siteIdx].nextRefresh = clock_type::now() + sites_[siteIdx].refreshInterval;
        sites_[siteIdx].redirCount = 0;
        // the WorkSSL client ctor can throw if SSL init fails
        makeRequest(sites_[siteIdx].startingResource, siteIdx, lock);
    }
    catch (std::exception const& ex)
    {
        JLOG(j_.error()) << "Exception in " << __func__ << ": " << ex.what();
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
    std::scoped_lock<std::mutex> const& sitesLock)
{
    json::Value const body = [&res, siteIdx, this]() {
        json::Reader r;
        json::Value body;
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
        std::uint32_t version = 0;
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
    XRPL_ASSERT(
        version == body[jss::version].asUInt(),
        "xrpl::ValidatorSite::parseJsonResponse : version match");
    auto const& uri = sites_[siteIdx].activeResource->uri;
    auto const hash = sha512Half(manifest, blobs, version);
    auto const applyResult = app_.getValidators().applyListsAndBroadcast(
        manifest,
        version,
        blobs,
        uri,
        hash,
        app_.getOverlay(),
        app_.getHashRouter(),
        app_.getOPs());

    sites_[siteIdx].lastRefreshStatus.emplace(
        Site::Status{
            .refreshed = clock_type::now(),
            .disposition = applyResult.bestDisposition(),
            .message = ""});

    for (auto const& [disp, count] : applyResult.dispositions)
    {
        switch (disp)
        {
            case ListDisposition::Accepted:
                JLOG(j_.debug()) << "Applied " << count << " new validator list(s) from " << uri;
                break;
            case ListDisposition::Expired:
                JLOG(j_.debug()) << "Applied " << count << " expired validator list(s) from "
                                 << uri;
                break;
            case ListDisposition::SameSequence:
                JLOG(j_.debug()) << "Ignored " << count
                                 << " validator list(s) with current sequence from " << uri;
                break;
            case ListDisposition::Pending:
                JLOG(j_.debug()) << "Processed " << count << " future validator list(s) from "
                                 << uri;
                break;
            case ListDisposition::KnownSequence:
                JLOG(j_.debug()) << "Ignored " << count
                                 << " validator list(s) with future known sequence from " << uri;
                break;
            case ListDisposition::Stale:
                JLOG(j_.warn()) << "Ignored " << count << "stale validator list(s) from " << uri;
                break;
            case ListDisposition::Untrusted:
                JLOG(j_.warn()) << "Ignored " << count << " untrusted validator list(s) from "
                                << uri;
                break;
            case ListDisposition::Invalid:
                JLOG(j_.warn()) << "Ignored " << count << " invalid validator list(s) from " << uri;
                break;
            case ListDisposition::UnsupportedVersion:
                JLOG(j_.warn()) << "Ignored " << count
                                << " unsupported version validator list(s) from " << uri;
                break;
            default:
                BOOST_ASSERT(false);
        }
    }

    if (body.isMember(jss::refresh_interval) && body[jss::refresh_interval].isNumeric())
    {
        using namespace std::chrono_literals;
        std::chrono::minutes const refresh = std::clamp(
            std::chrono::minutes{body[jss::refresh_interval].asUInt()},
            1min,
            std::chrono::minutes{24h});
        sites_[siteIdx].refreshInterval = refresh;
        sites_[siteIdx].nextRefresh = clock_type::now() + sites_[siteIdx].refreshInterval;
    }
}

std::shared_ptr<ValidatorSite::Site::Resource>
ValidatorSite::processRedirect(
    detail::response_type const& res,
    std::size_t siteIdx,
    std::scoped_lock<std::mutex> const& sitesLock)
{
    using namespace boost::beast::http;
    std::shared_ptr<Site::Resource> newLocation;
    if (!res.contains(field::location) || res[field::location].empty())
    {
        JLOG(j_.warn()) << "Request for validator list at " << sites_[siteIdx].activeResource->uri
                        << " returned a redirect with no Location.";
        throw std::runtime_error{"missing location"};
    }

    if (sites_[siteIdx].redirCount == kMaxRedirects)
    {
        JLOG(j_.warn()) << "Exceeded max redirects for validator list at "
                        << sites_[siteIdx].loadedResource->uri;
        throw std::runtime_error{"max redirects"};
    }

    JLOG(j_.debug()) << "Got redirect for validator list from "
                     << sites_[siteIdx].activeResource->uri << " to new location "
                     << res[field::location];

    try
    {
        newLocation = std::make_shared<Site::Resource>(std::string(res[field::location]));
        ++sites_[siteIdx].redirCount;
        if (newLocation->pUrl.scheme != "http" && newLocation->pUrl.scheme != "https")
            throw std::runtime_error("invalid scheme in redirect " + newLocation->pUrl.scheme);
    }
    catch (std::exception const& ex)
    {
        JLOG(j_.error()) << "Invalid redirect location: " << res[field::location];
        throw;
    }
    return newLocation;
}

void
ValidatorSite::onSiteFetch(
    boost::system::error_code const& ec,
    endpoint_type const& endpoint,
    detail::response_type const& res,
    std::size_t siteIdx)
{
    std::scoped_lock lockSites{sitesMutex_};
    {
        if (endpoint != endpoint_type{})
            sites_[siteIdx].lastRequestEndpoint = endpoint;
        JLOG(j_.debug()) << "Got completion for " << sites_[siteIdx].activeResource->uri << " "
                         << endpoint;
        auto onError = [&](std::string const& errMsg, bool retry) {
            sites_[siteIdx].lastRefreshStatus.emplace(
                Site::Status{
                    .refreshed = clock_type::now(),
                    .disposition = ListDisposition::Invalid,
                    .message = errMsg});
            if (retry)
                sites_[siteIdx].nextRefresh = clock_type::now() + kErrorRetryInterval;

            // See if there's a copy saved locally from last time we
            // saw the list.
            missingSite(lockSites);
        };
        if (ec)
        {
            JLOG(j_.warn()) << "Problem retrieving from " << sites_[siteIdx].activeResource->uri
                            << " " << endpoint << " " << ec.value() << ":" << ec.message();
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
                        parseJsonResponse(res.body(), siteIdx, lockSites);
                        break;
                    case status::moved_permanently:
                    case status::permanent_redirect:
                    case status::found:
                    case status::temporary_redirect: {
                        auto newLocation = processRedirect(res, siteIdx, lockSites);
                        XRPL_ASSERT(
                            newLocation,
                            "xrpl::ValidatorSite::onSiteFetch : non-null "
                            "validator");
                        // for perm redirects, also update our starting URI
                        if (res.result() == status::moved_permanently ||
                            res.result() == status::permanent_redirect)
                        {
                            sites_[siteIdx].startingResource = newLocation;
                        }
                        makeRequest(newLocation, siteIdx, lockSites);
                        return;  // we are still fetching, so skip
                                 // state update/notify below
                    }
                    default: {
                        JLOG(j_.warn()) << "Request for validator list at "
                                        << sites_[siteIdx].activeResource->uri << " " << endpoint
                                        << " returned bad status: " << res.result_int();
                        onError("bad result code", true);
                    }
                }
            }
            catch (std::exception const& ex)
            {
                JLOG(j_.error()) << "Exception in " << __func__ << ": " << ex.what();
                onError(ex.what(), false);
            }
        }
        sites_[siteIdx].activeResource.reset();
    }

    std::scoped_lock const lockState{stateMutex_};
    fetching_ = false;
    if (!stopping_)
        setTimer(lockSites, lockState);
    cv_.notify_all();
}

void
ValidatorSite::onTextFetch(
    boost::system::error_code const& ec,
    std::string const& res,
    std::size_t siteIdx)
{
    std::scoped_lock const lockSites{sitesMutex_};
    {
        try
        {
            if (ec)
            {
                JLOG(j_.warn()) << "Problem retrieving from " << sites_[siteIdx].activeResource->uri
                                << " " << ec.value() << ": " << ec.message();
                throw std::runtime_error{"fetch error"};
            }

            sites_[siteIdx].lastRequestSuccessful = true;

            parseJsonResponse(res, siteIdx, lockSites);
        }
        catch (std::exception const& ex)
        {
            JLOG(j_.error()) << "Exception in " << __func__ << ": " << ex.what();
            sites_[siteIdx].lastRefreshStatus.emplace(
                Site::Status{
                    .refreshed = clock_type::now(),
                    .disposition = ListDisposition::Invalid,
                    .message = ex.what()});
        }
        sites_[siteIdx].activeResource.reset();
    }

    std::scoped_lock const lockState{stateMutex_};
    fetching_ = false;
    if (!stopping_)
        setTimer(lockSites, lockState);
    cv_.notify_all();
}

json::Value
ValidatorSite::getJson() const
{
    using namespace std::chrono;
    using Int = json::Value::Int;

    json::Value jrr(json::ValueType::Object);
    json::Value& jSites = (jrr[jss::validator_sites] = json::ValueType::Array);
    {
        std::scoped_lock const lock{sitesMutex_};
        for (Site const& site : sites_)
        {
            json::Value& v = jSites.append(json::ValueType::Object);
            std::stringstream uri;
            uri << site.loadedResource->uri;
            if (site.loadedResource != site.startingResource)
                uri << " (redirects to " << site.startingResource->uri + ")";
            v[jss::uri] = uri.str();
            v[jss::next_refresh_time] = to_string(site.nextRefresh);
            if (site.lastRefreshStatus)
            {
                v[jss::last_refresh_time] = to_string(site.lastRefreshStatus->refreshed);
                v[jss::last_refresh_status] = to_string(site.lastRefreshStatus->disposition);
                if (!site.lastRefreshStatus->message.empty())
                    v[jss::last_refresh_message] = site.lastRefreshStatus->message;
            }
            v[jss::refresh_interval_min] = static_cast<Int>(site.refreshInterval.count());
        }
    }
    return jrr;
}
}  // namespace xrpl
