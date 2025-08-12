Our [build instructions][BUILD.md] assume you have a C++ development
environment complete with Git, Python, Conan, CMake, and a C++ compiler.
This document exists to help readers set one up on any of the Big Three
platforms: Linux, macOS, or Windows.

[BUILD.md]: ../../BUILD.md

## Linux

Package ecosystems vary across Linux distributions,
so there is no one set of instructions that will work for every Linux user.
The instructions below are written for Debian 12 (Bookworm).

```
export GCC_RELEASE=12
sudo apt update
sudo apt install --yes gcc-${GCC_RELEASE} g++-${GCC_RELEASE} python3-pip \
  python-is-python3 python3-venv python3-dev curl wget ca-certificates \
  git build-essential cmake ninja-build libc6-dev
sudo pip install --break-system-packages conan

sudo update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-${GCC_RELEASE} 999
sudo update-alternatives --install \
  /usr/bin/gcc gcc /usr/bin/gcc-${GCC_RELEASE} 100 \
  --slave /usr/bin/g++ g++ /usr/bin/g++-${GCC_RELEASE} \
  --slave /usr/bin/gcc-ar gcc-ar /usr/bin/gcc-ar-${GCC_RELEASE} \
  --slave /usr/bin/gcc-nm gcc-nm /usr/bin/gcc-nm-${GCC_RELEASE} \
  --slave /usr/bin/gcc-ranlib gcc-ranlib /usr/bin/gcc-ranlib-${GCC_RELEASE} \
  --slave /usr/bin/gcov gcov /usr/bin/gcov-${GCC_RELEASE} \
  --slave /usr/bin/gcov-tool gcov-tool /usr/bin/gcov-tool-${GCC_RELEASE} \
  --slave /usr/bin/gcov-dump gcov-dump /usr/bin/gcov-dump-${GCC_RELEASE} \
  --slave /usr/bin/lto-dump lto-dump /usr/bin/lto-dump-${GCC_RELEASE}
sudo update-alternatives --auto cc
sudo update-alternatives --auto gcc
```

If you use different Linux distribution, hope the instruction above can guide
you in the right direction. We try to maintain compatibility with all recent
compiler releases, so if you use a rolling distribution like e.g. Arch or CentOS
then there is a chance that everything will "just work".

## macOS

Open a Terminal and enter the below command to bring up a dialog to install
the command line developer tools.
Once it is finished, this command should return a version greater than the
minimum required (see [BUILD.md][]).

```
clang --version
```

### Install Xcode Specific Version (Optional)

If you develop other applications using XCode you might be consistently updating to the newest version of Apple Clang.
This will likely cause issues building rippled. You may want to install a specific version of Xcode:

1. **Download Xcode**
   - Visit [Apple Developer Downloads](https://developer.apple.com/download/more/)
   - Sign in with your Apple Developer account
   - Search for an Xcode version that includes **Apple Clang (Expected Version)**
   - Download the `.xip` file

2. **Install and Configure Xcode**

   ```bash
   # Extract the .xip file and rename for version management
   # Example: Xcode_16.2.app

   # Move to Applications directory
   sudo mv Xcode_16.2.app /Applications/

   # Set as default toolchain (persistent)
   sudo xcode-select -s /Applications/Xcode_16.2.app/Contents/Developer

   # Set as environment variable (temporary)
   export DEVELOPER_DIR=/Applications/Xcode_16.2.app/Contents/Developer
   ```

The command line developer tools should include Git too:

```
git --version
```

Install [Homebrew][],
use it to install [pyenv][],
use it to install Python,
and use it to install Conan:

[Homebrew]: https://brew.sh/
[pyenv]: https://github.com/pyenv/pyenv

```
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
brew update
brew install xz
brew install pyenv
pyenv install 3.11
pyenv global 3.11
eval "$(pyenv init -)"
pip install 'conan'
```

Install CMake with Homebrew too:

```
brew install cmake
```
