# The darwin toolchain, counterpart to linux.nix. Split by consumer: a dev
# shell's stdenv provides the SDK variables, nothing provides libresolv.
#
# darwin only - `libresolv` does not exist on Linux.
{ pkgs }:
let
  inherit (import ./packages.nix { inherit pkgs; })
    llvmVersion
    llvmPackages
    mkVersionedToolLinks
    ;

  # nixpkgs keeps libresolv out of the macOS SDK, so neither c-ares' `-lresolv`
  # nor grpc's <arpa/nameser.h> resolves. Headers can come from nixpkgs; the
  # library cannot, or its store path lands in xrpld - hence this copy.
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
    # The wrappers re-export only part of cctools; a bare env has no stdenv to
    # supply the rest, and without `dsymutil` even `clang -g` cannot link. One
    # by one, because buildEnv rejects any name a wrapper owns (notably `ld`).
    (pkgs.linkFarm "cctools-extra" (
      map
        (tool: {
          name = "bin/${tool}";
          path = "${llvmPackages.clang.bintools.bintools}/bin/${tool}";
        })
        [
          "codesign_allocate"
          "dsymutil"
          "dwarfdump"
          "install_name_tool"
          "lipo"
          "otool"
        ]
    ))
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

  # Salted names: the wrappers only read plain NIX_CFLAGS_COMPILE / NIX_LDFLAGS
  # through role variables a Nix stdenv would set. The salt is the target
  # platform, so this fits the gcc wrapper too.
  #
  # No space after -isystem: these are written one per line as KEY=VALUE, and a
  # shell sourcing that reads the space as the end of the assignment.
  libresolvEnv = {
    "NIX_CFLAGS_COMPILE_${llvmPackages.clang.suffixSalt}" =
      "-isystem${pkgs.darwin.libresolv.dev}/include";
    "NIX_LDFLAGS_${llvmPackages.clang.bintools.suffixSalt}" = "-L${libresolvSystemStub}/lib";
  };
}
