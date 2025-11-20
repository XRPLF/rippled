#ifndef BEAST_INSIGHT_EVENTIMPL_H_INCLUDED
#define BEAST_INSIGHT_EVENTIMPL_H_INCLUDED

#include <chrono>
#include <memory>

namespace beast {
namespace insight {

class Event;

class EventImpl : public std::enable_shared_from_this<EventImpl>
{
public:
    using value_type = std::chrono::milliseconds;

    virtual ~EventImpl() = 0;
    virtual void
    notify(value_type const& value) = 0;
};

}  // namespace insight
}  // namespace beast

#endif
