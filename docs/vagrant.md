# Vagrant SOCI

[Vagrant](https://www.vagrantup.com/) used to build and provision
virtual environments for **hassle-free** SOCI development.

## Features

* Ubuntu 14.04 (Trusty) virtual machine
* Multi-machine set-up with three VMs: `soci`, `oracle`, `db2`.
* Support networking between the configured machines.
* `soci.vm`:
  * hostname: `vmsoci.local`
  * build essentials
  * core dependencies
  * backend dependencies
  * FOSS databases installed with sample `soci` user and instance pre-configured
  * during provision, automatically clones and builds SOCI from `master` branch.
* `db2.vm`:
  * hostname: `vmdb2.local`
  * IBM DB2 Express-C 9.7 installed from [archive.canonical.com](http://archive.canonical.com) packages.
* `oracle.vm`:
    * *TODO*: provision with Oracle XE
* SOCI local git repository (aka `$SOCI_HOME`) is automatically shared on host
  and all guest machines.

## Prerequisites

* Speedy broadband, time and coffee.
* Recommended 4GB or much more RAM (tested with 16GB only).

### SOCI DB2 backend

The `soci.vm` will be configured properly to build the DB2 backend only if
it is provisioned with complete DB2 CLI client (libraries and headers).
You need to download "IBM Data Server Driver Package (DS Driver)" manually
and make it visible to Vagrant:

1. Go to [IBM Data Server Client Packages](http://www-01.ibm.com/support/docview.wss?uid=swg21385217).
2. Download "IBM Data Server Driver Package (DS Driver)".
3. Copy the package to `${SOCI_HOME}/tmp` directory, on host machine.

## Usage

Below, simple and easy development workflow with Vagrant is outlined:

* [Boot](https://docs.vagrantup.com/v2/getting-started/up.html)

```console
vagrant up
```

or boot VMs selectively:

```console
vagrant up {soci|db2}
```

First time you run it, be patient as Vagrant downloads VM box and
provisions it installing all the necessary packages.

* You can SSH into the machine

```console
vagrant ssh {soci|db2}
```

* Run git commands can either from host or VM `soci` (once connected via SSH)

```console
cd /vagrant # aka $SOCI_HOME
git pull origin master
```

* You can edit source code on both, on host or VM `soci`.
* For example, edit in your favourite editor on host machine, then build, run, test and debug on guest machine from command line.
* Alternatively, edit and build on host machine using your favourite IDE, then test and debug connecting to DBs on guest machines via network.

* Build on VM `soci`

```console
vagrant ssh soci
cd /vagrant    # aka $SOCI_HOME
cd soci-build  # aka $SOCI_BUILD
make
```

You can also execute the `build.h` script provided to run CMake and make

```console
vagrant ssh soci
cd $SOCI_BUILD
/vagrant/scripts/vagrant/build.sh
```

* Debug, only on VM `soci`, for example, with gdb or remotely Visual Studio 2017.

* [Teardown](https://docs.vagrantup.com/v2/getting-started/teardown.html)

```console
vagrant {suspend|halt|destroy} {soci|db2}
```

Check Vagrant [command-line interface](https://docs.vagrantup.com/v2/cli/index.html) for complete list of commands.

### Environment variables

All variables available to the `vagrant` user on the VMs are defined in and sourced from `/vagrant/scripts/vagrant/common.env`:

* `SOCI_HOME` where SOCI master is cloned (`/vagrant` on VM `soci`)
* `SOCI_BUILD` where CMake generates build configuration (`/home/vagrant/soci-build` on VM `soci`)
* `SOCI_HOST` network accessible VM `soci` hostname (`ping vmsoci.local`)
* `SOCI_USER` default database user and database name
* `SOCI_PASS` default database password for both, `SOCI_USER` and root/sysdba
  of particular database.
* `SOCI_DB2_HOST` network accessible VM `db2` hostname (`ping vmdb2.local`)
* `SOCI_DB2_USER` admin username to DB2 instance.
* `SOCI_DB2_USER` admin password to DB2 instance.

Note, those variables are also used by provision scripts to set up databases.

## Troubleshooting

* Analyze `vagrant up` output.
* On Windows, prefer `vagrant ssh` from inside MinGW Shell where  `ssh.exe` is available or learn how to use Vagrant with PuTTY.
* If you modify any of `scripts/vagrant/*.sh` scripts, **ensure** they have unified end-of-line characters to `LF` only. Otherwise, provisioning steps may fail.
