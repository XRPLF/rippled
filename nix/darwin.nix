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
    # The wrappers re-export only some of cctools, and a dev shell gets the rest
    # from its stdenv: without `dsymutil`, `clang -g` cannot link at all. Named
    # one by one because buildEnv refuses any name a wrapper already carries,
    # and the wrapped `ld` has to keep winning.
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

  # libresolv is absent from the SDK entirely, so grpc finds neither
  # <arpa/nameser.h> nor the library. The headers can come straight from
  # nixpkgs; the library cannot, hence the stub.
  #
  # The salted names are what the wrappers read - plain NIX_CFLAGS_COMPILE and
  # NIX_LDFLAGS only reach them via role variables a Nix stdenv would set. The
  # salt comes from the target platform, so this fits the gcc wrapper too.
  # No space after -isystem: these values are written one per line as KEY=VALUE,
  # and a shell sourcing that reads a space as the end of the assignment.
  libresolvEnv = {
    "NIX_CFLAGS_COMPILE_${llvmPackages.clang.suffixSalt}" =
      "-isystem${pkgs.darwin.libresolv.dev}/include";
    "NIX_LDFLAGS_${llvmPackages.clang.bintools.suffixSalt}" = "-L${libresolvSystemStub}/lib";
  };
}
