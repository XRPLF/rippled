ARG BASE_IMAGE=ubuntu:26.04

FROM ${BASE_IMAGE}

SHELL ["/bin/bash", "-e", "-o", "pipefail", "-c"]
ENTRYPOINT ["/bin/bash"]

ARG DEBIAN_FRONTEND=noninteractive

RUN <<EOF
pkgs=()
pkgs+=(ca-certificates)  # Trust store for curl, git, pip and nix.
pkgs+=(curl)             # Required to install nix.
pkgs+=(doxygen)          # Needed for Clio's check-doxygen-docs.sh.
pkgs+=(git)              # Required for prepare-runner.
pkgs+=(libatomic1)       # Required to run pre-commit provided `node`.
pkgs+=(python3)          # Python 3 interpreter.
pkgs+=(python3-venv)     # Virtual environment for Python applications.
pkgs+=(xz-utils)         # Required to install nix

apt-get update
apt-get install -y --no-install-recommends "${pkgs[@]}"
apt-get clean
rm -rf /var/lib/apt/lists/*
EOF

# Sanity-check that the system CA bundle distrusts the compromised eMudhra roots,
# before pip and the nix installer download anything with it.
RUN --mount=type=bind,source=bin/check-distrusted-roots.sh,target=/check-distrusted-roots.sh \
    /check-distrusted-roots.sh

# Ahead of the system Python on PATH,
# so `python3` and `pip` resolve to the venv and installs stay out of the system Python.
ENV VIRTUAL_ENV="/opt/venv"
ENV PATH="${VIRTUAL_ENV}/bin:${PATH}"

ARG PRE_COMMIT_VERSION=4.6.2
RUN <<EOF
python3 -m venv "${VIRTUAL_ENV}"
pip install --no-cache pre-commit==${PRE_COMMIT_VERSION}
EOF

RUN sh <(curl --proto '=https' --tlsv1.2 -L https://nixos.org/nix/install) --daemon --yes

# Add nix to PATH and set NIX environment variables,
# so nix is available in all shells including non-interactive shells (e.g., GitHub Actions).
ENV PATH="/nix/var/nix/profiles/default/bin:${PATH}"
ENV NIX_PROFILES="/nix/var/nix/profiles/default"
# The system bundle gets security updates through apt;
# the one in the Nix profile is frozen at the installer's nixpkgs.
ENV NIX_SSL_CERT_FILE="/etc/ssl/certs/ca-certificates.crt"

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
