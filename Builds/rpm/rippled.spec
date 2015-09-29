%define rippled_branch %(echo $RIPPLED_BRANCH)
Name:           rippled
# Version must be limited to MAJOR.MINOR.PATCH
Version:        0.30.0
# Release should include either the build or hotfix number (ex: hf1%{?dist} or b2%{?dist})
# If there is no b# or hf#, then use 1%{?dist}
Release:        b1%{?dist}
Summary:        Ripple peer-to-peer network daemon

Group:          Applications/Internet
License:        ISC
URL:            https://github.com/ripple/rippled

# curl -L -o SOURCES/rippled-release.zip https://github.com/ripple/rippled/archive/${RIPPLED_BRANCH}.zip
Source0:        rippled-%{rippled_branch}.zip
BuildRoot:      %(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)

BuildRequires:  gcc-c++ scons openssl-devel protobuf-devel
Requires:       protobuf openssl


%description
Rippled is the server component of the Ripple network.


%prep
%setup -n rippled-%{rippled_branch}


%build
scons -j `grep -c processor /proc/cpuinfo`


%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/usr/share/%{name}
cp LICENSE %{buildroot}/usr/share/%{name}/
mkdir -p %{buildroot}/usr/bin
cp build/rippled %{buildroot}/usr/bin/rippled
mkdir -p %{buildroot}/etc/%{name}
cp doc/rippled-example.cfg %{buildroot}/etc/%{name}/rippled.cfg
mkdir -p %{buildroot}/var/lib/%{name}/db
mkdir -p %{buildroot}/var/log/%{name}


%clean
rm -rf %{buildroot}


%files
%defattr(-,root,root,-)
/usr/bin/rippled
/usr/share/rippled/LICENSE
%config(noreplace) /etc/rippled/rippled.cfg
