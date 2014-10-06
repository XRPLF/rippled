//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#include <ripple/metrics/impl/MetricsResource.h>

#include <memory>

namespace ripple {
namespace metrics {
namespace impl {

MetricsResource::MetricsResource(ExposableMetricsElement* element)
  : m_element (element) {}

const std::string
MetricsResource::name() const
{
  return m_element->name();
};

const Json::Value
MetricsResource::history (clock_type::time_point const since, resolution const& res) const
{
    Json::Value ret (Json::arrayValue);
    std::vector<bucket> hist (m_element->getHistory (since, res));

    for (auto i = hist.cbegin(); i != hist.cend(); i++) {
        Json::Value dataPoint (Json::objectValue);
        dataPoint["average"] = static_cast<int>(i->avg);
        dataPoint["count"] = static_cast<int>(i->count);
        dataPoint["min"] = static_cast<int>(i->min);
        dataPoint["max"] = static_cast<int>(i->max);
        ret.append (dataPoint);
    }

    return ret;
}

const Json::Value
MetricsResourceList::history (clock_type::time_point const since, resolution const& res) const
{
    Json::Value ret (Json::objectValue);
    for (auto i = m_list.cbegin(); i != m_list.cend(); i++) {
        MetricsResource mRes (*i);
        ret[mRes.name()] = mRes.history (since, res);
    }

    return ret;
}

const Json::Value
MetricsResourceList::list () const
{
    Json::Value ret (Json::arrayValue);
    for(auto i = m_list.cbegin (); i != m_list.cend (); i++) {
      ret.append ((*i)->name ());
    }
    return ret;
}

std::unique_ptr<MetricsResource>
MetricsResourceList::getNamedResource (const std::string& name) const
{
    for(auto i = m_list.cbegin (); i != m_list.cend (); i++) {
      if ((*i)->name () == name)
        return std::make_unique<MetricsResource> (*i);
    }
    // FIXME: raise exception?
    return nullptr;
}

} // namespace impl
} // namespace metrics
} // namespace ripple

