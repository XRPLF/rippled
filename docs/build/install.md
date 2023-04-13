This document contains instructions for installing rippled.
The APT package manager is common on Debian-based Linux distributions like
Ubuntu,
while the YUM package manager is common on Red Hat-based Linux distributions
like CentOS.
Installing from source is an option for all platforms,
and the only supported option for installing custom builds.


## From source

From a source build, you can install rippled and libxrpl using CMake's
`--install` mode:

```
cmake --install . --prefix /opt/local
```

The default [prefix][1] is typically `/usr/local` on Linux and macOS and
`C:/Program Files/rippled` on Windows.

[1]: https://cmake.org/cmake/help/latest/variable/CMAKE_INSTALL_PREFIX.html


## With the APT package manager

1. Update repositories:

        sudo apt update -y

2. Install utilities:

        sudo apt install -y apt-transport-https ca-certificates wget gnupg

3. Add Ripple's package-signing GPG key to your list of trusted keys:

        sudo mkdir /usr/local/share/keyrings/
        wget -q -O - "https://repos.ripple.com/repos/api/gpg/key/public" | gpg --dearmor > ripple-key.gpg
        sudo mv ripple-key.gpg /usr/local/share/keyrings


4. Check the fingerprint of the newly-added key:

        gpg /usr/local/share/keyrings/ripple-key.gpg

    The output should include an entry for Ripple such as the following:

        gpg: WARNING: no command supplied.  Trying to guess what you mean ...
        pub   rsa3072 2019-02-14 [SC] [expires: 2026-02-17]
            C0010EC205B35A3310DC90DE395F97FFCCAFD9A2
        uid           TechOps Team at Ripple <techops+rippled@ripple.com>
        sub   rsa3072 2019-02-14 [E] [expires: 2026-02-17]


    In particular, make sure that the fingerprint matches. (In the above example, the fingerprint is on the third line, starting with `C001`.)

4. Add the appropriate Ripple repository for your operating system version:

        echo "deb [signed-by=/usr/local/share/keyrings/ripple-key.gpg] https://repos.ripple.com/repos/rippled-deb focal stable" | \
            sudo tee -a /etc/apt/sources.list.d/ripple.list

    The above example is appropriate for **Ubuntu 20.04 Focal Fossa**. For other operating systems, replace the word `focal` with one of the following:

    - `jammy` for **Ubuntu 22.04 Jammy Jellyfish**
    - `bionic` for **Ubuntu 18.04 Bionic Beaver**
    - `bullseye` for **Debian 11 Bullseye**
    - `buster` for **Debian 10 Buster**

    If you want access to development or pre-release versions of `rippled`, use one of the following instead of `stable`:

    - `unstable` - Pre-release builds ([`release` branch](https://github.com/ripple/rippled/tree/release))
    - `nightly` - Experimental/development builds ([`develop` branch](https://github.com/ripple/rippled/tree/develop))

    **Warning:** Unstable and nightly builds may be broken at any time. Do not use these builds for production servers.

5. Fetch the Ripple repository.

        sudo apt -y update

6. Install the `rippled` software package:

        sudo apt -y install rippled

7. Check the status of the `rippled` service:

        systemctl status rippled.service

    The `rippled` service should start automatically. If not, you can start it manually:

        sudo systemctl start rippled.service

8. Optional: allow `rippled` to bind to privileged ports.

    This allows you to serve incoming API requests on port 80 or 443. (If you want to do so, you must also update the config file's port settings.)

        sudo setcap 'cap_net_bind_service=+ep' /opt/ripple/bin/rippled


## With the YUM package manager

1. Install the Ripple RPM repository:

    Choose the appropriate RPM repository for the stability of releases you want:

    - `stable` for the latest production release (`master` branch)
    - `unstable` for pre-release builds (`release` branch)
    - `nightly` for experimental/development builds (`develop` branch)

    *Stable*

        cat << REPOFILE | sudo tee /etc/yum.repos.d/ripple.repo
        [ripple-stable]
        name=XRP Ledger Packages
        enabled=1
        gpgcheck=0
        repo_gpgcheck=1
        baseurl=https://repos.ripple.com/repos/rippled-rpm/stable/
        gpgkey=https://repos.ripple.com/repos/rippled-rpm/stable/repodata/repomd.xml.key
        REPOFILE

    *Unstable*

        cat << REPOFILE | sudo tee /etc/yum.repos.d/ripple.repo
        [ripple-unstable]
        name=XRP Ledger Packages
        enabled=1
        gpgcheck=0
        repo_gpgcheck=1
        baseurl=https://repos.ripple.com/repos/rippled-rpm/unstable/
        gpgkey=https://repos.ripple.com/repos/rippled-rpm/unstable/repodata/repomd.xml.key
        REPOFILE

    *Nightly*

        cat << REPOFILE | sudo tee /etc/yum.repos.d/ripple.repo
        [ripple-nightly]
        name=XRP Ledger Packages
        enabled=1
        gpgcheck=0
        repo_gpgcheck=1
        baseurl=https://repos.ripple.com/repos/rippled-rpm/nightly/
        gpgkey=https://repos.ripple.com/repos/rippled-rpm/nightly/repodata/repomd.xml.key
        REPOFILE

2. Fetch the latest repo updates:

        sudo yum -y update

3. Install the new `rippled` package:

        sudo yum install -y rippled

4. Configure the `rippled` service to start on boot:

        sudo systemctl enable rippled.service

5. Start the `rippled` service:

        sudo systemctl start rippled.service
