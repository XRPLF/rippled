# The environment CI builds in: every tool on PATH, no Nix stdenv setup hooks.
# Baked into the `nix-*` Docker images on Linux (see nix/docker), built on the
# runner on macOS (see .github/actions/setup-nix-env).
{
  pkgs,
  customGlibc,
  ...
}:
let
  inherit (import ./packages.nix { inherit pkgs; }) commonPackages;

  # Each forces something absent on the other platform, so both stay lazy.
  linux = import ./linux.nix { inherit pkgs customGlibc; };
  darwin = import ./darwin.nix { inherit pkgs; };

  # What a buildEnv cannot express: environment variables. $GITHUB_ENV format;
  # `set -a; . env; set +a` loads it in a shell.
  darwinEnv = pkgs.writeTextDir "share/xrpld-ci-env/env" (
    pkgs.lib.concatStrings (
      pkgs.lib.mapAttrsToList (name: value: "${name}=${value}\n") (darwin.sdkEnv // darwin.libresolvEnv)
    )
  );

  toolchain = if pkgs.stdenv.isLinux then linux.toolchain else (darwin.toolchain ++ [ darwinEnv ]);
in
{
  default = pkgs.buildEnv {
    name = "xrpld-ci-env";
    paths =
      commonPackages
      ++ toolchain
      ++ [
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
