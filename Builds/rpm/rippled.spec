Name:           rippled
Version:        0.25.1
Release:        1%{?dist}
Summary:        Ripple peer-to-peer network daemon

Group:          Applications/Internet
License:        ISC
URL:            https://github.com/ripple/rippled

# curl -L -o SOURCES/rippled-release.zip https://github.com/ripple/rippled/archive/release.zip
Source0:        rippled-release.zip
BuildRoot:      %(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)

BuildRequires:  gcc-c++ scons openssl-devel protobuf-devel
Requires:       protobuf openssl


%description
Rippled is the server component of the Ripple network.


%prep
%setup -n rippled-release


%build
# Assume boost is manually installed
export RIPPLED_BOOST_HOME=/usr/local/boost_1_55_0
scons -j `grep -c processor /proc/cpuinfo` build/rippled


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
/etc/rippled/rippled-example.cfg

