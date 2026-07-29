{
  pkgs,
  customGlibc,
  ...
}:
let
  inherit (import ./packages.nix { inherit pkgs; })
    commonPackages
    gccVersion
    llvmVersion
    mkVersionedToolLinks
    ;

  # Custom-glibc toolchain, shared with the Linux dev shell (see compilers.nix).
  inherit (import ./compilers.nix { inherit pkgs customGlibc; })
    customGcc
    customClang
    customBinutils
    customGcov
    ;

  # Strip the generic cc/c++/cpp symlinks from the clang wrapper so it can
  # coexist with the gcc wrapper in buildEnv. gcc remains the default
  # compiler (cc/c++/cpp); clang is invoked explicitly as clang/clang++.
  customClangForCiEnv = pkgs.symlinkJoin {
    name = "clang-wrapper-custom-for-ci-env";
    paths = [ customClang ];
    postBuild = ''
      rm -f $out/bin/cc $out/bin/c++ $out/bin/cpp
    '';
  };

in
{
  default = pkgs.buildEnv {
    name = "xrpld-ci-env";
    paths = commonPackages ++ [
      customGcc
      customGcov
      customClangForCiEnv
      customBinutils
      (mkVersionedToolLinks {
        name = "gcc";
        package = customGcc;
        version = gccVersion;
        tools = [
          "gcc"
          "g++"
          "cpp"
        ];
      })
      (mkVersionedToolLinks {
        name = "clang";
        package = customClang;
        version = llvmVersion;
        tools = [
          "clang"
          "clang++"
        ];
      })
      # CA certificate bundle so HTTPS clients (git, curl, conan) can verify
      # TLS connections without ca-certificates being installed in the system.
      pkgs.cacert
    ];
    pathsToLink = [
      "/bin"
      "/etc/ssl/certs"
      "/lib"
      "/include"
      "/share"
    ];
  };
}
