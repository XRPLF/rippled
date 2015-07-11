//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/BasicConfig.h>
#include <iostream>
#include <limits>

namespace ripple {
namespace detail {

void
accessViolation()
{
    // dereference memory
    // location zero
    int volatile* j = 0;
    (void)*j;
}

// This hook lets you do pre or post
// processing on exceptions to suit needs.
void
throwException (std::exception_ptr ep)
{
    std::rethrow_exception(ep);
}

/** Do we die when danger() is called? */
bool dangerMode = false;

/** Do we use an access violation or an abort to die? */
bool const dieUsingAccessViolation = true;

// TODO: core counting logic goes here, if we decide to do it.

/** Number of cores already existing in the rippled directory. */
int coreCount = 0;

/** Maximum number of cores allowed in the rippled directory. */
int const maxCoreCount = std::numeric_limits<int>::max();

void endExecution(bool mustDie,
                  char const* name,
                  FailureReport const& report)
{
    std::cerr << name << ": " << report.message << ":"
              << report.filename << ":" << report.line << "\n";

    if (mustDie)
    {
        if (coreCount >= maxCoreCount)
            std::terminate();  // TODO: or try to delete a core.
        else if (dieUsingAccessViolation)
            detail::accessViolation();
        else
            std::terminate();
    }
}

} // detail

bool getDangerMode()
{
    return detail::dangerMode;
}

void setupDanger(BasicConfig const& config)
{
    detail::dangerMode = get<bool>(
        config["danger"], "danger", detail::dangerMode);
}

void die(FailureReport const& report)
{
    detail::endExecution(true, "FATAL", report);
}

void danger(FailureReport const& report)
{
    detail::endExecution(detail::dangerMode, "DANGER", report);
}

void
LogicError (std::string const&)
{
    detail::accessViolation();
}

} // ripple
