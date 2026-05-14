/** @file
 *  Out-of-line destructor definitions for the `beast::insight` metric
 *  abstract base classes.
 *
 *  Each of `CounterImpl`, `EventImpl`, `GaugeImpl`, and `MeterImpl` declares
 *  its destructor pure virtual (`virtual ~Foo() = 0`), making the class
 *  abstract while still serving as the polymorphic base.  In C++, a pure
 *  virtual destructor is special: it is called implicitly by every derived
 *  class destructor, so the linker requires an out-of-line body even though
 *  the function is pure.  This translation unit provides those four bodies.
 *
 *  Defining them here, rather than inline in each header, gives the
 *  definitions a single, explicit home and keeps the headers free of
 *  implementation detail.
 *
 *  @see CounterImpl, EventImpl, GaugeImpl, MeterImpl
 */

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
