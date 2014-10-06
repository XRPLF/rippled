//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#ifndef METRICS_METRICSIMPL_H_INCLUDED
#define METRICS_METRICSIMPL_H_INCLUDED

#include <ripple/metrics/Metrics.h>

#include <ripple/metrics/impl/History.h>

#include <ripple/http/Server.h>
#include <ripple/common/RippleSSLContext.h>

#include <beast/insight/HookImpl.h>
#include <beast/insight/CounterImpl.h>

#include <memory>
#include <forward_list>
#include <mutex>

namespace ripple {
namespace metrics {
namespace impl {

class MetricsImpl;
class MetricsMeterImpl;
class MetricsGaugeImpl;
class MetricsEventImpl;
class MetricsCounterImpl;
class MetricsHookImpl;

/**
 * MetricsImpl: Implementation of the Metrics beast::insight::Collector backend
 * 
 * This backend implements a small HTTP server to serve various metrics with
 * JSON.
 *
 */
class MetricsImpl
    : public beast::insight::Collector
    , public ripple::HTTP::Handler
    , public std::enable_shared_from_this <MetricsImpl>
{
public:
    using Ptr = std::shared_ptr<MetricsImpl>;

    /**
     * getMetricStore: Gets the forward_list that contains metrics of type @T
     */
    template <class T> const std::forward_list<T*>
        getMetricStore () const;
    template <class T> std::forward_list<T*>&
        getMetricStore ();

private:
    /**
     * m_server: The HTTP server used
     * m_context: The HTTP context
     */
    ripple::HTTP::Server m_server;
    std::unique_ptr<ripple::RippleSSLContext> m_context;

    /**
     * m_meters: List of MetricsMeterImpl created by make_meter()
     * m_gauges: List of MetricsGaugeImpl created by make_gaugek()
     * m_events: List of MetricsEventImpl created by make_event()
     * m_counters: List of MetricsCounterImpl created by make_counter()
     * m_hooks: List of MetricsHookImpl created by make_hook()
     */
    std::forward_list<MetricsMeterImpl*> m_meters;
    std::forward_list<MetricsGaugeImpl*> m_gauges;
    std::forward_list<MetricsEventImpl*> m_events;
    std::forward_list<MetricsCounterImpl*> m_counters;
    std::forward_list<MetricsHookImpl*> m_hooks;
    std::mutex m_metricLock;

    /**
     * createResponse: Creates a well-formatted HTTP 1.1 response
     *
     * @code: HTTP code
     * @body: Response body
     */
    std::string createResponse (int code, std::string const& body);

    const std::pair<std::vector<unsigned char>, size_t> getFileContents(std::string const& uri);

public:
    /**
     * MetricsImpl: Constructor that binds a port number and @beast::Journal
     *
     * @portNum: Port to listen on
     * @journal: @beast::Journal to log to
     */
    MetricsImpl (int portNum, beast::Journal journal);
    ~MetricsImpl ();

    /**
     * add: Adds the given metric to metric-specific storage
     *
     * @see: getMetricStore()
     */
    template <class T> void add(T* elem) {
        std::unique_lock<std::mutex> lock(m_metricLock);
        getMetricStore<T> ().push_front (elem);
    }

    /**
     * remove: Removes the given metric from the metric-specific storage
     *
     * @see: getMetricStore()
     */
    template <class T> void remove(T* elem) {
        std::unique_lock<std::mutex> lock(m_metricLock);
        getMetricStore<T> ().remove (elem);
    }

    // Internal callbacks for @m_server
    void onAccept (ripple::HTTP::Session& session) override;
    void onRequest (ripple::HTTP::Session& session) override;
    void onClose (
             ripple::HTTP::Session& session,
             boost::system::error_code const& ec
         ) override;
    void onStopped (ripple::HTTP::Server &server) override;

    // Implementation of beast::insight::Collector
    beast::insight::Hook make_hook (
        beast::insight::HookImpl::HandlerType const&
    ) override;
    beast::insight::Counter make_counter (std::string const&) override;
    beast::insight::Event make_event (std::string const&) override;
    beast::insight::Gauge make_gauge (std::string const&) override;
    beast::insight::Meter make_meter (std::string const&) override;

};

/**
 * MetricsElementBase: Base implementation of a Metrics collector element
 *
 * This class provides a shared pointer to the owning @MetricsImpl object
 */
class MetricsElementBase {
public:
    MetricsElementBase (MetricsImpl::Ptr const& impl)
        : m_impl (impl)
    {}

protected:
    /**
     * m_impl: A std::shared_ptr to the owning @MetricsImpl object
     */
    MetricsImpl::Ptr const& m_impl;
};

/**
 * ExposableMetricsElement: Base implementation of a Metrics collector element
 * that has a name
 *
 * This class provides two protected members shared amongst implementations of
 * Counter, Meter, Gauge, and Event objects used by the @MetricsCollector
 * class.
 *
 * @m_name: The name of the element
 * @see MetricsElementBase
 */


class ExposableMetricsElement
    : public MetricsElementBase {
public:
    ExposableMetricsElement(
        const std::string& name, MetricsImpl::Ptr const& impl
    );

    const std::string name() const;

    const std::vector<bucket> getHistory(
        clock_type::time_point const start, resolution const& res 
    ) const;

protected:
    std::string m_name;
    histories m_histories;
    mutable std::mutex m_historyMutex;
};

/**
 * MetricsHookImpl: Implementation of a MetricsHook
 */
class MetricsHookImpl
    : public beast::insight::HookImpl
    , public MetricsElementBase
{
public:
    MetricsHookImpl(HandlerType const& handler, MetricsImpl::Ptr const& impl);
    ~MetricsHookImpl();

    /**
     * handle: Calls the hook handler that was given in MetricsImpl::make_hook()
     */
    void handle();

private:
    HandlerType const m_handler;
    MetricsHookImpl& operator= (MetricsHookImpl const&);
};

class MetricsCounterImpl
    : public beast::insight::CounterImpl
    , public ExposableMetricsElement
{
public:
    MetricsCounterImpl(std::string const& type, MetricsImpl::Ptr const& impl);
    ~MetricsCounterImpl();
    void increment (value_type);

private:
    MetricsCounterImpl& operator= (MetricsCounterImpl const&);
    value_type m_last;
};

class MetricsEventImpl
    : public beast::insight::EventImpl
    , public ExposableMetricsElement
{
public:
    MetricsEventImpl(std::string const& type, MetricsImpl::Ptr const& impl);
    ~MetricsEventImpl();
    void notify (value_type const&);

private:
    MetricsEventImpl& operator= (MetricsEventImpl const&);
};

class MetricsGaugeImpl
    : public beast::insight::GaugeImpl
    , public ExposableMetricsElement
{
public:
    MetricsGaugeImpl (std::string const& type, MetricsImpl::Ptr const& impl);
    ~MetricsGaugeImpl ();
    void
        set (value_type);
    void
        increment (difference_type);

private:
    MetricsGaugeImpl&
        operator= (MetricsGaugeImpl const&);
    value_type m_last;
};

class MetricsMeterImpl
    : public beast::insight::MeterImpl
    , public ExposableMetricsElement
{
public:
    MetricsMeterImpl (std::string const& type, MetricsImpl::Ptr const& impl);
    ~MetricsMeterImpl ();
    void
        increment (value_type);

private:
    MetricsMeterImpl&
        operator= (MetricsMeterImpl const&);
    value_type m_last;
};

/**
 * getMetricStore<MetricsMeterImpl>: Returns the storage for Meter metrics
 *
 * @see: getMetricStore()
 */
template <>
std::forward_list<MetricsMeterImpl*>&
MetricsImpl::getMetricStore<MetricsMeterImpl>() {
    return m_meters;
}
template <>
const std::forward_list<MetricsMeterImpl*>
MetricsImpl::getMetricStore<MetricsMeterImpl>() const {
    return m_meters;
}

/**
 * getMetricStore<MetricsGaugeImpl>: Returns the storage for Gauge metrics
 *
 * @see: getMetricStore()
 */
template <>
std::forward_list<MetricsGaugeImpl*>&
MetricsImpl::getMetricStore<MetricsGaugeImpl>() {
    return m_gauges;
}
template <>
const std::forward_list<MetricsGaugeImpl*>
MetricsImpl::getMetricStore<MetricsGaugeImpl>() const {
    return m_gauges;
}

/**
 * getMetricStore<MetricsCounterImpl>: Returns the storage for Counter metrics
 *
 * @see: getMetricStore()
 */
template <>
std::forward_list<MetricsCounterImpl*>&
MetricsImpl::getMetricStore<MetricsCounterImpl>() {
    return m_counters;
}
template <>
const std::forward_list<MetricsCounterImpl*>
MetricsImpl::getMetricStore<MetricsCounterImpl>() const {
    return m_counters;
}

/**
 * getMetricStore<MetricsHookImpl>: Returns the storage for Hook metrics
 *
 * @see: getMetricStore()
 */
template <>
std::forward_list<MetricsHookImpl*>&
MetricsImpl::getMetricStore<MetricsHookImpl>() {
    return m_hooks;
}
template <>
const std::forward_list<MetricsHookImpl*>
MetricsImpl::getMetricStore<MetricsHookImpl>() const {
    return m_hooks;
}

/**
 * getMetricStore<MetricsEventImpl>: Returns the storage for Event metrics
 *
 * @see: getMetricStore()
 */
template <>
std::forward_list<MetricsEventImpl*>&
MetricsImpl::getMetricStore<MetricsEventImpl>() {
    return m_events;
}
template <>
const std::forward_list<MetricsEventImpl*>
MetricsImpl::getMetricStore<MetricsEventImpl>() const {
    return m_events;
}

} // namespace imp
} // namespace metrics
} // namespace ripple

#endif // METRICS_METRICSIMPL_H_INCLUDED

