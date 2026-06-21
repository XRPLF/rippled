#include <test/formal_verification/ffi/cpp/CppExterns.h>

#include <test/formal_verification/ffi/LeanConvert.h>
#include <test/formal_verification/ffi/protocol/UInt256FFI.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/digest.h>

#include <lean/lean.h>

using namespace xrpl;
using namespace xrpl::test::formal_verification;

lean_object*
cpp_sha_512_half(lean_object* bytes)
{
    // bytes is borrowed (@& on the Lean side)
    uint256 const h = sha512Half(Slice{lean_sarray_cptr(bytes), lean_sarray_size(bytes)});
    return lean_uint256_build(mkBytes(h).give());
}

// Single-attempt pseudo-account derivation: returns the raw 20-byte RipeshaHasher digest
lean_object*
cpp_pseudo_account_address_hash(uint16_t i, lean_object* parentHash, lean_object* pseudoOwnerKey)
{
    // parentHash/pseudoOwnerKey are borrowed (@& on the Lean side)
    uint256 const ph = readBaseUint<uint256>(parentHash);
    uint256 const pok = readBaseUint<uint256>(pseudoOwnerKey);

    // --- copied from xrpl::pseudoAccountAddress (inline there, not a standalone function) ---
    RipeshaHasher rsh;
    auto const hash = sha512Half(i, ph, pok);
    rsh(hash.data(), hash.size());
    RipeshaHasher::result_type const raw = static_cast<RipeshaHasher::result_type>(rsh);
    // --- end copy ---

    return mkBytes(raw.data(), raw.size()).give();
}
