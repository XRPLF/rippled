#pragma once

#include <xrpl/beast/insight/Collector.h>

#include <memory>

namespace beast::insight {

/** A Collector which does not collect metrics. */
class NullCollector : public Collector
{
public:
    explicit NullCollector() = default;

    static std::shared_ptr<Collector>
    make();
};

}  // namespace beast::insight
