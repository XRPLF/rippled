#pragma once

#include <test/formal_verification/ffi/protocol/IssueFFI.h>
#include <test/formal_verification/ffi/protocol/MPTIssueFFI.h>

#include <xrpl/protocol/Asset.h>

#include <lean/lean.h>

#include <cstdint>

extern "C" {
lean_object*
lean_asset_issue_build(lean_object* issue);
lean_object*
lean_asset_mpt_issue_build(lean_object* mptIssue);
uint8_t
lean_asset_kind(lean_object* asset);
lean_object*
lean_asset_issue(lean_object* asset);
lean_object*
lean_asset_mpt_issue(lean_object* asset);
}

namespace xrpl::test::formal_verification {

class AssetFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;
    using CppType = Asset;

    static AssetFFI
    build(Asset const& a)
    {
        if (a.holds<Issue>())
            return AssetFFI(leanCall(lean_asset_issue_build, IssueFFI::build(a.get<Issue>())));
        return AssetFFI(
            leanCall(lean_asset_mpt_issue_build, MPTIssueFFI::build(a.get<MPTIssue>())));
    }

    Asset
    read() const
    {
        if (leanGet<std::uint8_t>(lean_asset_kind) == 0)
            return Asset{*leanGetOpt<IssueFFI>(lean_asset_issue)};
        return Asset{*leanGetOpt<MPTIssueFFI>(lean_asset_mpt_issue)};
    }
};

static_assert(LeanWrapper<AssetFFI>);

}  // namespace xrpl::test::formal_verification
