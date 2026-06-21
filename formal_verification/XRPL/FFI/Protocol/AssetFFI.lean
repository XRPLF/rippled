import XRPL.Model.Protocol.Asset


namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_asset_issue_build]
def lean_asset_issue_build (i : Issue) : Asset := .issue i
@[export lean_asset_mpt_issue_build]
def lean_asset_mpt_issue_build (m : MPTIssue) : Asset := .mptIssue m
@[export lean_asset_kind]
def lean_asset_kind : Asset → UInt8
  | .issue _ => 0
  | .mptIssue _ => 1
@[export lean_asset_issue]
def lean_asset_issue : Asset → Option Issue
  | .issue i => some i
  | .mptIssue _ => none
@[export lean_asset_mpt_issue]
def lean_asset_mpt_issue : Asset → Option MPTIssue
  | .mptIssue m => some m
  | .issue _ => none

end XRPL.FFI
