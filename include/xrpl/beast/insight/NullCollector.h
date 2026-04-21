#pragma once

#include <xrpl/beast/insight/Collector.h>

namespace beast::insight {

/** A Collector which does not collect metrics. */
class NullCollector : public Collector
{
public:
    explicit NullCollector() = default;

    static std::shared_ptr<Collector>
    New();
};

}  // namespace beast::insight
