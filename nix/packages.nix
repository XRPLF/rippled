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

  # rust-overlay's toolchain propagates the *default* stdenv.cc onto the PATH (so
  # cargo has a linker). That default may be different from the clang we pin here,
  # so it shadows our clang and the build can silently use a different compiler
  # version. Drop that cc from every propagation channel instead of pinning a
  # replacement: the toolchain then carries no compiler and cargo just uses the
  # active shell's stdenv cc. Must cover all channels — rust-overlay uses both
  # propagatedBuildInputs and depsHostHostPropagated.
  rustToolchainBase = pkgs.rust-bin.fromRustupToolchainFile ../rust-toolchain.toml;
  rustToolchain =
    let
      defaultCc = pkgs.stdenv.cc; # default compiler from nixpkgs stdenv
      withoutDefaultCc = builtins.filter (dep: (dep.outPath or "") != defaultCc.outPath);
    in
    rustToolchainBase.overrideAttrs (old: {
      propagatedBuildInputs = withoutDefaultCc (old.propagatedBuildInputs or [ ]);
      depsHostHostPropagated = withoutDefaultCc (old.depsHostHostPropagated or [ ]);
    });

  # Nix wraps its toolchain so that binaries are exposed only under unsuffixed
  # names (gcc, g++, clang-tidy, ...). Several tools probe for a
  # version-suffixed name first and fall back to a system binary on the PATH
  # when Nix doesn't provide it:
  #   - Conan's Boost recipe looks up `g++-<major>` before plain `g++`.
  #   - bin/pre-commit/clang_tidy_check.py looks up `run-clang-tidy-<v>` and
  #     `clang-apply-replacements-<v>` before the unsuffixed names.
  # On a host that also has the matching system binary (e.g. Ubuntu's
  # `/usr/bin/g++-15` or `clang-tidy-22`) the probe escapes Nix and mixes a
  # system tool into the Nix environment. Generate version-suffixed symlinks
  # next to a package's tools so those probes resolve to the Nix ones.
  #
  # Compiler links must point at whichever compiler is active in a given
  # environment (the plain stdenv compiler in the dev shell, the custom-glibc
  # wrappers in ci-env.nix), so those callers pass their own `package`; the
  # clang tooling is environment-independent and is linked in commonPackages.
  mkVersionedToolLinks =
    {
      name,
      package,
      version,
      tools,
    }:
    pkgs.linkFarm "${name}-${toString version}-versioned-links" (
      map (tool: {
        name = "bin/${tool}-${toString version}";
        path = "${package}/bin/${tool}";
      }) tools
    );

  # The cc-wrapper doesn't re-export gcov, but coverage tooling (gcovr) needs a
  # gcov that exactly matches the compiler. Surface it from a gcc `cc` output.
  mkGcov =
    { name, cc }:
    pkgs.linkFarm "gcov-${name}" [
      {
        name = "bin/gcov";
        path = "${cc}/bin/gcov";
      }
    ];

  clangToolLinks = mkVersionedToolLinks {
    name = "clang-tools";
    package = clangTools;
    version = llvmVersion;
    tools = [
      "clang-tidy"
      "clang-apply-replacements"
      "clang-format"
    ];
  };
  runClangTidyLink = mkVersionedToolLinks {
    name = "run-clang-tidy";
    package = runClangTidy;
    version = llvmVersion;
    tools = [ "run-clang-tidy" ];
  };
in
{
  inherit
    gccVersion
    llvmVersion
    gccPackage
    llvmPackages
    mkVersionedToolLinks
    mkGcov
    ;

  commonPackages = with pkgs; [
    clangToolLinks
    runClangTidyLink
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
    # Rust packages
    cargo-audit
    cargo-llvm-cov
    cargo-nextest
    corrosion
    rustToolchain
  ];
}
