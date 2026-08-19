# Installing xrpld

> [!NOTE]
> These instructions apply to packages published from 2026-08-19 onwards.
> For xrpld 3.3.0 and earlier see [install-legacy.md](./install-legacy.md).

`xrpld` is published as DEB and RPM packages for 64-bit x86 Linux.
Use APT on Debian-based distributions such as Debian and Ubuntu,
and YUM on Red Hat-based distributions such as RHEL, AlmaLinux, and Rocky Linux.
To build from source instead, see [BUILD.md](../BUILD.md).

## Release channels

Packages are published to four channels:

- `stable` - the latest production release
- `unstable` - release candidates
- `experimental` - beta builds
- `develop` - every push to the [`develop` branch](https://github.com/XRPLF/rippled/tree/develop)

See [Publishing packages](../package/README.md#publishing-packages) for how channels are produced.

The instructions below use `stable`.
To follow another channel, replace `stable` with its name
wherever it appears in the repository configuration.

> [!WARNING]
> Channels other than `stable` may be broken at any time.
> Do not use them for production servers.

## Install the xrpld package

### With the APT package manager

1.  Install utilities:

    ```bash
    sudo apt update -y
    sudo apt install -y apt-transport-https ca-certificates curl gnupg
    ```

2.  Add the XRPL Foundation package-signing key to your list of trusted keys:

    ```bash
    sudo install -d -m 0755 /etc/apt/keyrings
    sudo curl -fsS https://packages.xrplf.org/xrplf.asc -o /etc/apt/keyrings/xrplf.asc
    ```

3.  Check the fingerprint of the newly-added key:

    ```bash
    gpg --show-keys /etc/apt/keyrings/xrplf.asc
    ```

    The output should be:

    ```text
    pub   rsa4096 2026-08-18 [SC]
          B655416741221F780FBCFBC9AA84D41A11D29FA9
    uid                      XRPLF Packages <distribution@xrplf.org>
    ```

    In particular, make sure that the fingerprint matches.

4.  Add the repository, using the channel you picked in [Release channels](#release-channels):

    ```bash
    echo "deb [signed-by=/etc/apt/keyrings/xrplf.asc] https://packages.xrplf.org/repository/deb-stable any main" | \
        sudo tee /etc/apt/sources.list.d/xrplf.list
    ```

5.  Fetch the repository:

    ```bash
    sudo apt -y update
    ```

6.  Install the `xrpld` software package:

    ```bash
    sudo apt -y install xrpld
    ```

### With the YUM package manager

1.  Add the XRPL Foundation package-signing key:

    ```bash
    sudo rpm --import https://packages.xrplf.org/xrplf.asc
    ```

2.  Add the repository, using the channel you picked in [Release channels](#release-channels):

    ```bash
    cat << REPOFILE | sudo tee /etc/yum.repos.d/xrplf.repo
    [xrplf-stable]
    name=XRP Ledger Packages
    enabled=1
    baseurl=https://packages.xrplf.org/repository/rpm-stable/
    gpgcheck=1
    repo_gpgcheck=1
    gpgkey=https://packages.xrplf.org/xrplf.asc
    REPOFILE
    ```

    `gpgcheck=1` verifies each package against the key above.
    `repo_gpgcheck=1` verifies the repository metadata, which the server signs with the same key.

3.  Install the `xrpld` package:

    ```bash
    sudo yum install -y xrpld
    ```

## The xrpld service

Both package managers install a systemd unit and enable it, so `xrpld` starts on boot.
Check whether it is already running:

```bash
systemctl status xrpld.service
```

The APT packages start it immediately as well; the YUM packages do not, so start it yourself:

```bash
sudo systemctl start xrpld.service
```

### Optional: binding to privileged ports

To serve incoming API requests on port 80 or 443, grant the service the capability to bind them.
You must also update the config file's port settings.

```bash
sudo install -d -m 0755 /etc/systemd/system/xrpld.service.d
sudo tee /etc/systemd/system/xrpld.service.d/privileged-ports.conf >/dev/null <<'EOF'
[Service]
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
AmbientCapabilities=CAP_NET_BIND_SERVICE
EOF
sudo systemctl daemon-reload
sudo systemctl restart xrpld.service
```
