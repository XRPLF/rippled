import XRPL.Model.Protocol.MPTIssue


namespace XRPL.FFI

open XRPL.Model.Protocol (MPTIssue MPTID)

@[export lean_mpt_issue_build]
def lean_mpt_issue_build (mptID : MPTID) : MPTIssue := ⟨mptID⟩
@[export lean_mpt_issue_mpt_id]
def lean_mpt_issue_mpt_id (m : MPTIssue) : MPTID := m.mptID

end XRPL.FFI
