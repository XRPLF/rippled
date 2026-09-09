# The Rust half of the tool set shared by the CI environment and the dev shell:
# the stable toolchain pinned by rust-toolchain.toml, the nightly the Rust
# coverage job needs, and the cargo plugins. Consumed by packages.nix.
{ pkgs }:
let
  # rust-overlay's toolchain propagates the *default* stdenv.cc onto the PATH (so
  # cargo has a linker). That default may be different from the clang we pin
  # elsewhere, so it shadows our clang and the build can silently use a different
  # compiler version. Drop that cc from every propagation channel instead of
  # pinning a replacement: the toolchain then carries no compiler and cargo just
  # uses the active shell's stdenv cc.
  #
  # The channel list is every list mkDerivation propagates to a dependent's
  # environment (including the two legacy aliases). rust-overlay currently only
  # uses propagatedBuildInputs and depsHostHostPropagated, but covering all of
  # them means an upstream switch to another channel cannot quietly put the
  # compiler back on PATH.
  dropDefaultCc =
    toolchain:
    let
      defaultCc = pkgs.stdenv.cc; # default compiler from nixpkgs stdenv
      withoutDefaultCc = builtins.filter (dep: (dep.outPath or "") != defaultCc.outPath);
    in
    toolchain.overrideAttrs (
      old:
      pkgs.lib.genAttrs [
        "depsBuildBuildPropagated"
        "propagatedNativeBuildInputs" # alias of depsBuildHostPropagated
        "depsBuildTargetPropagated"
        "depsHostHostPropagated"
        "propagatedBuildInputs" # alias of depsHostTargetPropagated
        "depsTargetTargetPropagated"
      ] (channel: withoutDefaultCc (old.${channel} or [ ]))
    );

  rustToolchain = dropDefaultCc (pkgs.rust-bin.fromRustupToolchainFile ../rust-toolchain.toml);

  # cargo-llvm-cov honours the #[coverage(off)] that keeps unit tests out of the
  # coverage report only under a nightly rustc, and looks for llvm-profdata and
  # llvm-cov in that same toolchain's sysroot — hence llvm-tools-preview.
  #
  # Not every nightly ships every component, so `nightly.latest` breaks on the
  # days llvm-tools-preview is absent; selectLatestNightlyWith walks back to the
  # newest one that has it. The result is the newest such nightly *known to the
  # locked rust-overlay*, which means updating flake.lock moves the compiler that
  # produces the coverage numbers — and with it the rustc version recorded in
  # nix/check-tools/*.txt, so those snapshots need regenerating alongside.
  rustNightly = dropDefaultCc (
    pkgs.rust-bin.selectLatestNightlyWith (
      toolchain: toolchain.minimal.override { extensions = [ "llvm-tools-preview" ]; }
    )
  );

  # A second toolchain cannot go on PATH: its cargo and rustc would collide with
  # the pinned stable's in the ci-env buildEnv, which resolves collisions by
  # picking one silently. Reaching the nightly only through this wrapper keeps it
  # in the image closure (the Docker build copies the whole closure, not just
  # what is linked into /bin) while leaving it inactive everywhere that does not
  # ask for it.
  #
  # The script's `path` subcommand exists for scopes wider than one command — a
  # CI job appending to $GITHUB_PATH, so that the cargo cache action's own
  # `rustc -vV` probe, which runs in a step of its own, agrees with the toolchain
  # the build will use.
  rustNightlyScript = pkgs.replaceVarsWith {
    name = "rust-nightly";
    src = ./rust-nightly.sh;
    dir = "bin";
    isExecutable = true;
    replacements = {
      inherit (pkgs) runtimeShell;
      rustNightlyBin = "${rustNightly}/bin";
    };
  };
in
{
  packages = [
    pkgs.cargo-audit
    pkgs.cargo-llvm-cov
    pkgs.cargo-nextest
    rustNightlyScript
    rustToolchain
  ];
}
