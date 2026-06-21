import XRPL.Model.Basics.Blob
import XRPL.Model.Protocol.AccountID

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

-- rippled's `sfCredential` inner object {sfIssuer, sfCredentialType}
structure AcceptedCredential where
  issuer : AccountID
  credentialType : Blob
  deriving Repr, DecidableEq, BEq, Hashable

end XRPL.Model.Protocol
