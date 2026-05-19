#include <xrpl/beast/insight/CounterImpl.h>
#include <xrpl/beast/insight/EventImpl.h>
#include <xrpl/beast/insight/GaugeImpl.h>
#include <xrpl/beast/insight/MeterImpl.h>

namespace beast::insight {

CounterImpl::~CounterImpl() = default;

EventImpl::~EventImpl() = default;

GaugeImpl::~GaugeImpl() = default;

MeterImpl::~MeterImpl() = default;
}  // namespace beast::insight
