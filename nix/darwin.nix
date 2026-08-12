# The darwin toolchain, counterpart to linux.nix. Split by consumer: a dev shell
# already has the SDK variables, a bare buildEnv has neither.
#
# darwin only - `libresolv` does not exist on Linux.
{ pkgs }:
let
  inherit (import ./packages.nix { inherit pkgs; })
    llvmVersion
    llvmPackages
    mkVersionedToolLinks
    ;

  # nixpkgs keeps libresolv out of the macOS SDK and ships it as a store dylib,
  # so the `-lresolv` c-ares asks for either fails to resolve or pins a store
  # path into xrpld. This copy carries the system install name instead.
  libresolvSystemStub =
    pkgs.runCommand "libresolv-system-stub"
      {
        nativeBuildInputs = [ llvmPackages.bintools ];
      }
      ''
        mkdir -p "$out/lib"
        cp ${pkgs.darwin.libresolv}/lib/libresolv.9.dylib "$out/lib/"
        chmod +w "$out/lib/libresolv.9.dylib"
        llvm-install-name-tool -id /usr/lib/libresolv.9.dylib "$out/lib/libresolv.9.dylib"
        ln -s libresolv.9.dylib "$out/lib/libresolv.dylib"
      '';
in
{
  # For an environment that only puts binaries on PATH.
  toolchain = [
    llvmPackages.clang
    (mkVersionedToolLinks {
      name = "clang";
      package = llvmPackages.clang;
      version = llvmVersion;
      tools = [
        "clang"
        "clang++"
      ];
    })
  ];

  # Without these CMake asks `xcrun` and gets the Command Line Tools SDK, whose
  # headers clash with the Nix libc++ ones.
  sdkEnv = {
    DEVELOPER_DIR = "${pkgs.apple-sdk}";
    SDKROOT = "${pkgs.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
  };

  # The salted name is what the bintools wrapper reads; plain NIX_LDFLAGS only
  # reaches it via role variables a Nix stdenv would set. The salt comes from the
  # target platform, so this fits the gcc wrapper too.
  linkerEnv = {
    "NIX_LDFLAGS_${llvmPackages.clang.bintools.suffixSalt}" = "-L${libresolvSystemStub}/lib";
  };
}
