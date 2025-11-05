#ifndef BEAST_INSIGHT_NULLCOLLECTOR_H_INCLUDED
#define BEAST_INSIGHT_NULLCOLLECTOR_H_INCLUDED

#include <xrpl/beast/insight/Collector.h>

namespace beast {
namespace insight {

/** A Collector which does not collect metrics. */
class NullCollector : public Collector
{
public:
    explicit NullCollector() = default;

    static std::shared_ptr<Collector>
    New();
};

}  // namespace insight
}  // namespace beast

#endif
