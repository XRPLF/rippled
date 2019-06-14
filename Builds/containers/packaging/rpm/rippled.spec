%define rippled_version %(echo $RIPPLED_RPM_VERSION)
%define rpm_release %(echo $RPM_RELEASE)
%define rpm_patch %(echo $RPM_PATCH)
%define _prefix /opt/ripple
Name:           rippled
# Dashes in Version extensions must be converted to underscores
Version:        %{rippled_version}
Release:        %{rpm_release}%{?dist}%{rpm_patch}
Summary:        rippled daemon

License:        MIT
URL:            http://ripple.com/
Source0:        rippled.tar.gz
Source1:        validator-keys.tar.gz

BuildRequires:  protobuf-static openssl-static cmake zlib-static ninja-build

%description
rippled

%package devel
Summary: Files for development of applications using xrpl core library
Group: Development/Libraries
Requires: openssl-static, zlib-static

%description devel
core library for development of standalone applications that sign transactions.

%prep
%setup -c -n rippled -a 1

%build
cd rippled
mkdir -p bld.release
cd bld.release
cmake .. -G Ninja -DCMAKE_INSTALL_PREFIX=%{_prefix} -DCMAKE_BUILD_TYPE=Release -Dstatic=true -DCMAKE_VERBOSE_MAKEFILE=ON -Dlocal_protobuf=ON
# build VK
cd ../../validator-keys-tool
mkdir -p bld.release
cd bld.release
# Install a copy of the rippled artifacts into a local VK build dir so that it
# can use them to build against (VK needs xrpl_core lib to build). We install
# into a local build dir instead of buildroot because we want VK to have
# relative paths embedded in debug info, otherwise check-buildroot (rpmbuild)
# will complain
mkdir xrpl_dir
DESTDIR="%{_builddir}/validator-keys-tool/bld.release/xrpl_dir"  cmake --build ../../rippled/bld.release --target install -- -v
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%{_builddir}/validator-keys-tool/bld.release/xrpl_dir/opt/ripple -Dstatic=true -DCMAKE_VERBOSE_MAKEFILE=ON
cmake --build . --parallel -- -v

%pre
test -e /etc/pki/tls || { mkdir -p /etc/pki; ln -s /usr/lib/ssl /etc/pki/tls; }

%install
rm -rf $RPM_BUILD_ROOT
DESTDIR=$RPM_BUILD_ROOT cmake --build rippled/bld.release --target install -- -v
install -d ${RPM_BUILD_ROOT}/etc/opt/ripple
install -d ${RPM_BUILD_ROOT}/usr/local/bin
ln -s %{_prefix}/etc/rippled.cfg ${RPM_BUILD_ROOT}/etc/opt/ripple/rippled.cfg
ln -s %{_prefix}/etc/validators.txt ${RPM_BUILD_ROOT}/etc/opt/ripple/validators.txt
ln -s %{_prefix}/bin/rippled ${RPM_BUILD_ROOT}/usr/local/bin/rippled
install -D validator-keys-tool/bld.release/validator-keys ${RPM_BUILD_ROOT}%{_bindir}/validator-keys
install -D ./rippled/Builds/containers/shared/rippled.service ${RPM_BUILD_ROOT}/usr/lib/systemd/system/rippled.service
install -D ./rippled/Builds/containers/packaging/rpm/50-rippled.preset ${RPM_BUILD_ROOT}/usr/lib/systemd/system-preset/50-rippled.preset
install -D ./rippled/Builds/containers/shared/update-rippled.sh ${RPM_BUILD_ROOT}%{_bindir}/update-rippled.sh
install -D ./rippled/Builds/containers/shared/update-rippled-cron ${RPM_BUILD_ROOT}%{_prefix}/etc/update-rippled-cron
install -D ./rippled/Builds/containers/shared/rippled-logrotate ${RPM_BUILD_ROOT}/etc/logrotate.d/rippled
install -d $RPM_BUILD_ROOT/var/log/rippled
install -d $RPM_BUILD_ROOT/var/lib/rippled

%post
USER_NAME=rippled
GROUP_NAME=rippled

getent passwd $USER_NAME &>/dev/null || useradd $USER_NAME
getent group $GROUP_NAME &>/dev/null || groupadd $GROUP_NAME

chown -R $USER_NAME:$GROUP_NAME /var/log/rippled/
chown -R $USER_NAME:$GROUP_NAME /var/lib/rippled/
chown -R $USER_NAME:$GROUP_NAME %{_prefix}/

chmod 755 /var/log/rippled/
chmod 755 /var/lib/rippled/

chmod 644 %{_prefix}/etc/update-rippled-cron
chmod 644 /etc/logrotate.d/rippled
chown -R root:$GROUP_NAME %{_prefix}/etc/update-rippled-cron

%files
%doc rippled/README.md rippled/LICENSE
%{_bindir}/rippled
/usr/local/bin/rippled
%{_bindir}/update-rippled.sh
%{_prefix}/etc/update-rippled-cron
%{_bindir}/validator-keys
%config(noreplace) %{_prefix}/etc/rippled.cfg
%config(noreplace) /etc/opt/ripple/rippled.cfg
%config(noreplace) %{_prefix}/etc/validators.txt
%config(noreplace) /etc/opt/ripple/validators.txt
%config(noreplace) /etc/logrotate.d/rippled
%config(noreplace) /usr/lib/systemd/system/rippled.service
%config(noreplace) /usr/lib/systemd/system-preset/50-rippled.preset
%dir /var/log/rippled/
%dir /var/lib/rippled/

%files devel
%{_prefix}/include
%{_prefix}/lib/*.a
%{_prefix}/lib/cmake/ripple

%changelog
* Wed May 15 2019 Mike Ellery <mellery451@gmail.com>
- Make validator-keys use local rippled build for core lib

* Wed Aug 01 2018 Mike Ellery <mellery451@gmail.com>
- add devel package for signing library

* Thu Jun 02 2016 Brandon Wilson <bwilson@ripple.com>
- Install validators.txt

