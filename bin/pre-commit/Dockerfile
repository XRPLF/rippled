ARG BASE_IMAGE=ubuntu:26.04

FROM ${BASE_IMAGE}

SHELL ["/bin/bash", "-e", "-o", "pipefail", "-c"]
ENTRYPOINT ["/bin/bash"]

ARG DEBIAN_FRONTEND=noninteractive

RUN <<EOF
pkgs=()
pkgs+=(curl)         # Required to install nix.
pkgs+=(doxygen)      # Needed for Clio's check-doxygen-docs.sh.
pkgs+=(git)          # Required for prepare-runner.
pkgs+=(libatomic1)   # Required to run pre-commit provided `node`.
pkgs+=(python3)      # Python 3 interpreter.
pkgs+=(python3-pip)  # Package manager for Python applications.
pkgs+=(xz-utils)     # Required to install nix

apt-get update
apt-get install -y --no-install-recommends "${pkgs[@]}"
apt-get clean
rm -rf /var/lib/apt/lists/*
EOF

ARG PRE_COMMIT_VERSION=4.6.0
RUN pip install --no-cache --break-system-packages \
    pre-commit==${PRE_COMMIT_VERSION}

RUN sh <(curl --proto '=https' --tlsv1.2 -L https://nixos.org/nix/install) --daemon --yes

# Add nix to PATH and set NIX environment variables,
# so nix is available in all shells including non-interactive shells (e.g., GitHub Actions).
ENV PATH="/nix/var/nix/profiles/default/bin:${PATH}"
ENV NIX_PROFILES="/nix/var/nix/profiles/default"
ENV NIX_SSL_CERT_FILE="/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt"

# Verify nix installation
RUN nix --version

ENV RUSTUP_HOME="/opt/rust/rustup"
ENV CARGO_HOME="/opt/rust/cargo"
ENV PATH="/opt/rust/cargo/bin:${PATH}"

WORKDIR /tmp
COPY rust-toolchain.toml /tmp/rust-toolchain.toml
RUN <<EOF
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
    | sh -s -- -y --no-modify-path --profile minimal --default-toolchain none
rustup toolchain install
rustup show
cargo fmt --version
EOF

WORKDIR /
