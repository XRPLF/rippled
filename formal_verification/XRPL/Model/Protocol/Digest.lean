import XRPL.Model.Basics.BaseUInt

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

-- SHA-512Half: the cryptographic primitive is delegated to rippled via FFI
@[extern "cpp_sha_512_half"]
opaque sha512Half (bytes : @& ByteArray) : UInt256

end XRPL.Model.Protocol
