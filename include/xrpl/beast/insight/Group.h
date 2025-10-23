#ifndef BEAST_INSIGHT_GROUP_H_INCLUDED
#define BEAST_INSIGHT_GROUP_H_INCLUDED

#include <xrpl/beast/insight/Collector.h>

#include <memory>
#include <string>

namespace beast {
namespace insight {

/** A collector front-end that manages a group of metrics. */
class Group : public Collector
{
public:
    using ptr = std::shared_ptr<Group>;

    /** Returns the name of this group, for diagnostics. */
    virtual std::string const&
    name() const = 0;
};

}  // namespace insight
}  // namespace beast

#endif
