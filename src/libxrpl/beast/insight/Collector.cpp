/** @file
 *  Out-of-line definition of the `Collector` pure-virtual destructor.
 *
 *  A pure-virtual destructor declared `= 0` in the header must still have
 *  an external definition: the compiler emits a call to every base-class
 *  destructor when a derived object is destroyed, so the linker must be
 *  able to resolve `Collector::~Collector`. Defining it here as `= default`
 *  anchors the vtable to this single translation unit, avoiding
 *  duplicate-symbol and ODR issues that would arise if the definition were
 *  placed inline in the header.
 *
 *  @see beast::insight::Collector
 *  @see beast::insight::NullCollector
 *  @see beast::insight::StatsDCollector
 */
#include <xrpl/beast/insight/Collector.h>

namespace beast::insight {

Collector::~Collector() = default;
}  // namespace beast::insight
