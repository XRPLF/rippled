//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_JTX_PLUGINENV_H_INCLUDED
#define RIPPLE_TEST_JTX_PLUGINENV_H_INCLUDED

#include <ripple/app/main/PluginSetup.h>
#include <ripple/app/tx/applySteps.h>
#include <test/jtx/Env.h>

namespace ripple {
namespace test {
namespace jtx {

class PluginEnv : public Env
{
private:
    static const bool isPlugin = true;

public:
    PluginEnv(
        beast::unit_test::suite& suite_,
        std::unique_ptr<Config> config,
        FeatureBitset features,
        uint256 additionalFeature = uint256{},
        std::unique_ptr<Logs> logs = nullptr,
        beast::severities::Severity thresh = beast::severities::kError)
        : Env(suite_, std::move(config), features, std::move(logs), thresh)
    {
        if (additionalFeature != uint256{})
            app().config().features.insert(additionalFeature);
    }
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif