/** @file
 *  Out-of-line destructor definition for `HookImpl`.
 *
 *  Even though `HookImpl::~HookImpl` is declared pure virtual, the base
 *  destructor is always invoked at the end of every subclass destruction
 *  chain and therefore must have a definition.  Placing `= default` here
 *  in the `.cpp` anchors the vtable and destructor body to a single
 *  translation unit, which is the idiomatic pattern for abstract base
 *  classes in C++.
 *
 *  @see HookImpl, Hook
 */
#include <xrpl/beast/insight/Hook.h>

#include <xrpl/beast/insight/HookImpl.h>

namespace beast::insight {

HookImpl::~HookImpl() = default;
}  // namespace beast::insight
