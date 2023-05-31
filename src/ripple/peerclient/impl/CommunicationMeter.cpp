//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <ripple/peerclient/CommunicationMeter.h>

#include <boost/units/io.hpp>

#include <algorithm>
#include <numeric>

namespace ripple {

using namespace boost::units;
using namespace ripple::units;

void
CommunicationMeter::addMessage(quantity<byte> nbytes)
{
    nrequests_ += 1;
    nbytes_ += nbytes;
    while (CommunicationMeter::Clock::now() > horizon_)
    {
        horizon_ += INTERVAL;
        ibucket_ = (ibucket_ + 1) % NBUCKETS;
        histRequests_[ibucket_] = 0;
        histBytes_[ibucket_] = 0;
    }
    histRequests_[ibucket_] += 1;
    histBytes_[ibucket_] += nbytes;
}

std::ostream&
operator<<(std::ostream& out, CommunicationMeter const& meter)
{
    using namespace boost::units;

    auto format = get_format(out);
    auto prefix = get_autoprefix(out);
    out << symbol_format << engineering_prefix;

    using resolution = std::chrono::seconds;
    auto elapsed = std::chrono::duration_cast<resolution>(
        CommunicationMeter::Clock::now() - meter.start_);
    out << meter.nrequests_ << " request (" << meter.nbytes_ << ") in "
        << elapsed / resolution(1);
    out << " | ";

    auto window = std::max(
        std::chrono::duration_cast<resolution>(
            CommunicationMeter::INTERVAL * CommunicationMeter::NBUCKETS),
        elapsed);
    int nseconds = window.count();
    auto reqs = std::accumulate(
        meter.histRequests_.begin(), meter.histRequests_.end(), 0lu);
    auto nbytes = std::accumulate(
        meter.histBytes_.begin(), meter.histBytes_.end(), 0 * bytes);
    out << reqs / nseconds << " request/s (" << nbytes / nseconds
        << "/s) in the last " << CommunicationMeter::WINDOW_NAME;

    set_format(out, format);
    set_autoprefix(out, prefix);
    return out;
}

}  // namespace ripple
