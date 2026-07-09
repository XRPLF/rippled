{ pkgs }:
let
  # Compiler versions used across the dev shell and the CI environment.
  gccVersion = 15;
  llvmVersion = 22;

  gccPackage = pkgs."gcc${toString gccVersion}";
  llvmPackages = pkgs."llvmPackages_${toString llvmVersion}";

  # Bound explicitly so it tracks llvmPackages above, not the `with pkgs` default.
  clangTools = llvmPackages.clang-tools;

  # In LLVM 22, run-clang-tidy.py moved from share/clang/ to bin/, so nixpkgs
  # clang-tools no longer links it. Wrap it manually.
  runClangTidy = pkgs.writeShellScriptBin "run-clang-tidy" ''
    exec ${pkgs.python3}/bin/python3 ${llvmPackages.clang-unwrapped}/bin/run-clang-tidy "$@"
  '';
in
{
  inherit
    gccVersion
    llvmVersion
    gccPackage
    llvmPackages
    ;

  commonPackages = with pkgs; [
    ccache
    clangbuildanalyzer
    clangTools
    cmake
    conan
    curlMinimal # needed for codecov/codecov-action
    doxygen
    file # needed for cpack in Clio
    gcovr
    gh
    git
    git-cliff
    git-lfs
    gnumake
    gnupg # needed for signing commits & codecov/codecov-action
    graphviz
    less # needed for git diff
    mold
    nettools # provides netstat, used to debug failures in CI
    ninja
    patchelf
    perl # needed for openssl
    pkg-config
    pre-commit
    # protoc generates the Go gRPC bindings and embeds its own version string into every committed
    # .pb.go file. To allow CI to verify those files with a plain `git diff`, we pin the version to
    # `protobuf_34` rather than the rolling `protobuf` to keep regeneration reproducible across the
    # Nix frequently changing unstable channel. The protoc-gen-go* plugins have no versioned
    # attributes in nixpkgs; protoc-gen-go's version is in turn constrained by the go.mod require
    # on google.golang.org/protobuf.
    protobuf_34 # provides protoc
    protoc-gen-go # protoc plugin for the Go message bindings
    protoc-gen-go-grpc # protoc plugin for the Go gRPC service stubs
    python3
    runClangTidy
    vim
    zip
  ];
}
