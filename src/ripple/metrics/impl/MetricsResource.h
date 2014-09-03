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

#ifndef METRICS_METRICSRESOURCE_H_INCLUDED
#define METRICS_METRICSRESOURCE_H_INCLUDED

#include "MetricsImpl.h"

#include <ripple/json/json_value.h>

#include <memory>

namespace ripple {

namespace metrics {

namespace impl {

class MetricsResource {
public:
  MetricsResource (ExposableMetricsElement* element);

  const std::string name () const;

  const Json::Value
    history (const clock_type::time_point& since, const resolution& res) const;

private:
  ExposableMetricsElement* m_element;
};

class MetricsResourceList {
public:
  MetricsResourceList (const std::forward_list<ExposableMetricsElement*>& metrics)
    : m_list (metrics) {}

  const Json::Value
    history (const clock_type::time_point& since, const resolution& res) const;

  const Json::Value
    list () const;

  std::unique_ptr<MetricsResource>
    getNamedResource (const std::string& name) const;

private:
  const std::forward_list<ExposableMetricsElement*> m_list;
};

} //namespace impl

} //namespace metrics

} // namespace ripple

#endif //METRICS_METRICSRESOURCE_H_INCLUDED
