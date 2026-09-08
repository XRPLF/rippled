# Lean4 toolchain (lean, lake) for formal verification
#
# Nixpkgs does not provide versioned lean4 packages, and the packaging
# changed between 4.28 and latest, so it's not possible to just override
# the version in nixpkgs.
{
  lib,
  stdenv,
  fetchurl,
  zstd,
  autoPatchelfHook,
}:
let
  # Must match the toolchain lake expects. Once the Lean sources land, this
  # should read formal_verification/lean-toolchain instead, so the two pins
  # cannot drift apart.
  version = "4.28.0";

  # Bump alongside the version above. A stale hash fails the build and prints
  # the correct one, so it cannot silently fetch the wrong toolchain.
  releases = {
    x86_64-linux = {
      tag = "linux";
      hash = "sha256-zrOj+ET3rr9jJF4rUcKNWw7TiULBn5PPP+vVIDAhYL0=";
    };
    aarch64-linux = {
      tag = "linux_aarch64";
      hash = "sha256-yGWAEmHHR9TxXQi+ypq8IKypB5BKu7KE3iWjf0tFWLw=";
    };
    x86_64-darwin = {
      tag = "darwin";
      hash = "sha256-TJfaEKkm2Qat8z/JmKJUakED6gzfmVzs78G6ox/twAg=";
    };
    aarch64-darwin = {
      tag = "darwin_aarch64";
      hash = "sha256-YZQvnRkH25GAIBVKUXyH+2SEHkjOuwAy/AkJ340YmgU=";
    };
  };

  inherit (stdenv.hostPlatform) system;
  release =
    releases.${system} or (throw "lean4: no release pinned for ${system}, add it to nix/lean4.nix");
in
stdenv.mkDerivation {
  pname = "lean4";
  inherit version;

  src = fetchurl {
    url = "https://github.com/leanprover/lean4/releases/download/v${version}/lean-${version}-${release.tag}.tar.zst";
    inherit (release) hash;
  };

  nativeBuildInputs = [
    zstd
  ]
  ++ lib.optional stdenv.hostPlatform.isLinux autoPatchelfHook;

  # The release binaries name the FHS loader in PT_INTERP; autoPatchelfHook
  # points them at the Nix one.
  buildInputs = lib.optional stdenv.hostPlatform.isLinux stdenv.cc.cc.lib;

  # Skip Darwin fixup: fixDarwinDylibNames cannot rewrite the prebuilt
  # libleanshared_1.dylib install name, so it aborts.
  dontFixup = stdenv.hostPlatform.isDarwin;

  # Keep the release layout as shipped: bin/ needs lib/lean/ beside it.
  installPhase = ''
    runHook preInstall
    mkdir -p $out
    mv ./* $out/
    runHook postInstall
  '';

  meta = {
    description = "Lean4 theorem prover and toolchain (official release)";
    homepage = "https://github.com/leanprover/lean4";
    license = lib.licenses.asl20;
    platforms = lib.attrNames releases;
    mainProgram = "lean";
  };
}
