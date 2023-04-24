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

BuildRequires:  cmake zlib-static ninja-build

%description
rippled

%package devel
Summary: Files for development of applications using xrpl core library
Group: Development/Libraries
Requires: zlib-static

%description devel
core library for development of standalone applications that sign transactions.

%package reporting
Summary: Reporting Server for rippled

%description reporting
History server for XRP Ledger

%prep
%setup -c -n rippled

%build
rm -rf ~/.conan/profiles/default

cp /opt/libcstd/libstdc++.so.6.0.22 /usr/lib64
cp /opt/libcstd/libstdc++.so.6.0.22 /lib64
ln -sf /usr/lib64/libstdc++.so.6.0.22 /usr/lib64/libstdc++.so.6
ln -sf /lib64/libstdc++.so.6.0.22 /usr/lib64/libstdc++.so.6

source /opt/rh/rh-python38/enable
pip install "conan<2"
conan profile new default --detect
conan profile update settings.compiler.libcxx=libstdc++11 default
conan profile update settings.compiler.cppstd=20 default

cd rippled

mkdir -p bld.rippled
conan export external/snappy snappy/1.1.9@

pushd bld.rippled
conan install .. \
     --settings build_type=Release \
     --output-folder . \
     --build missing

cmake -G Ninja \
     -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake \
     -DCMAKE_INSTALL_PREFIX=%{_prefix} \
     -DCMAKE_BUILD_TYPE=Release \
     -Dunity=OFF \
     -Dstatic=ON \
     -Dvalidator_keys=ON \
     -DCMAKE_VERBOSE_MAKEFILE=ON \
     ..

cmake --build . --parallel $(nproc) --target rippled --target validator-keys
popd

mkdir -p bld.rippled-reporting
pushd bld.rippled-reporting

conan install .. \
     --settings build_type=Release \
     --output-folder . \
     --build missing \
     --settings compiler.cppstd=17 \
     --options reporting=True

cmake -G Ninja \
     -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake \
     -DCMAKE_INSTALL_PREFIX=%{_prefix} \
     -DCMAKE_BUILD_TYPE=Release \
     -Dunity=OFF \
     -Dstatic=ON \
     -Dvalidator_keys=ON \
     -Dreporting=ON \
     -DCMAKE_VERBOSE_MAKEFILE=ON \
     ..

cmake --build . --parallel $(nproc) --target rippled

%pre
test -e /etc/pki/tls || { mkdir -p /etc/pki; ln -s /usr/lib/ssl /etc/pki/tls; }

%install
rm -rf $RPM_BUILD_ROOT
DESTDIR=$RPM_BUILD_ROOT cmake --build rippled/bld.rippled --target install #-- -v
mkdir -p $RPM_BUILD_ROOT
rm -rf ${RPM_BUILD_ROOT}/%{_prefix}/lib64/
install -d ${RPM_BUILD_ROOT}/etc/opt/ripple
install -d ${RPM_BUILD_ROOT}/usr/local/bin

install -D ./rippled/cfg/rippled-example.cfg ${RPM_BUILD_ROOT}/%{_prefix}/etc/rippled.cfg
install -D ./rippled/cfg/validators-example.txt ${RPM_BUILD_ROOT}/%{_prefix}/etc/validators.txt

ln -sf %{_prefix}/etc/rippled.cfg ${RPM_BUILD_ROOT}/etc/opt/ripple/rippled.cfg
ln -sf %{_prefix}/etc/validators.txt ${RPM_BUILD_ROOT}/etc/opt/ripple/validators.txt
ln -sf %{_prefix}/bin/rippled ${RPM_BUILD_ROOT}/usr/local/bin/rippled
install -D rippled/bld.rippled/validator-keys/validator-keys ${RPM_BUILD_ROOT}%{_bindir}/validator-keys
install -D ./rippled/Builds/containers/shared/rippled.service ${RPM_BUILD_ROOT}/usr/lib/systemd/system/rippled.service
install -D ./rippled/Builds/containers/packaging/rpm/50-rippled.preset ${RPM_BUILD_ROOT}/usr/lib/systemd/system-preset/50-rippled.preset
install -D ./rippled/Builds/containers/shared/update-rippled.sh ${RPM_BUILD_ROOT}%{_bindir}/update-rippled.sh
install -D ./rippled/bin/getRippledInfo ${RPM_BUILD_ROOT}%{_bindir}/getRippledInfo
install -D ./rippled/Builds/containers/shared/update-rippled-cron ${RPM_BUILD_ROOT}%{_prefix}/etc/update-rippled-cron
install -D ./rippled/Builds/containers/shared/rippled-logrotate ${RPM_BUILD_ROOT}/etc/logrotate.d/rippled
install -d $RPM_BUILD_ROOT/var/log/rippled
install -d $RPM_BUILD_ROOT/var/lib/rippled

# reporting mode
%define _prefix /opt/rippled-reporting
mkdir -p ${RPM_BUILD_ROOT}/etc/opt/rippled-reporting/
install -D rippled/bld.rippled-reporting/rippled-reporting ${RPM_BUILD_ROOT}%{_bindir}/rippled-reporting
install -D ./rippled/cfg/rippled-reporting.cfg ${RPM_BUILD_ROOT}%{_prefix}/etc/rippled-reporting.cfg
install -D ./rippled/cfg/validators-example.txt ${RPM_BUILD_ROOT}%{_prefix}/etc/validators.txt
install -D ./rippled/Builds/containers/packaging/rpm/50-rippled-reporting.preset ${RPM_BUILD_ROOT}/usr/lib/systemd/system-preset/50-rippled-reporting.preset
ln -s %{_prefix}/bin/rippled-reporting ${RPM_BUILD_ROOT}/usr/local/bin/rippled-reporting
ln -s %{_prefix}/etc/rippled-reporting.cfg ${RPM_BUILD_ROOT}/etc/opt/rippled-reporting/rippled-reporting.cfg
ln -s %{_prefix}/etc/validators.txt ${RPM_BUILD_ROOT}/etc/opt/rippled-reporting/validators.txt
install -d $RPM_BUILD_ROOT/var/log/rippled-reporting
install -d $RPM_BUILD_ROOT/var/lib/rippled-reporting
install -D ./rippled/Builds/containers/shared/rippled-reporting.service ${RPM_BUILD_ROOT}/usr/lib/systemd/system/rippled-reporting.service
sed -E 's/rippled?/rippled-reporting/g' ./rippled/Builds/containers/shared/update-rippled.sh > ${RPM_BUILD_ROOT}%{_bindir}/update-rippled-reporting.sh
sed -E 's/rippled?/rippled-reporting/g' ./rippled/bin/getRippledInfo > ${RPM_BUILD_ROOT}%{_bindir}/getRippledReportingInfo
sed -E 's/rippled?/rippled-reporting/g' ./rippled/Builds/containers/shared/update-rippled-cron > ${RPM_BUILD_ROOT}%{_prefix}/etc/update-rippled-reporting-cron
sed -E 's/rippled?/rippled-reporting/g' ./rippled/Builds/containers/shared/rippled-logrotate > ${RPM_BUILD_ROOT}/etc/logrotate.d/rippled-reporting


%post
%define _prefix /opt/ripple
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

%post reporting
%define _prefix /opt/rippled-reporting
USER_NAME=rippled-reporting
GROUP_NAME=rippled-reporting

getent passwd $USER_NAME &>/dev/null || useradd -r $USER_NAME
getent group $GROUP_NAME &>/dev/null || groupadd $GROUP_NAME

chown -R $USER_NAME:$GROUP_NAME /var/log/rippled-reporting/
chown -R $USER_NAME:$GROUP_NAME /var/lib/rippled-reporting/
chown -R $USER_NAME:$GROUP_NAME %{_prefix}/

chmod 755 /var/log/rippled-reporting/
chmod 755 /var/lib/rippled-reporting/
chmod -x /usr/lib/systemd/system/rippled-reporting.service


%files
%define _prefix /opt/ripple
%doc rippled/README.md rippled/LICENSE.md
%{_bindir}/rippled
/usr/local/bin/rippled
%{_bindir}/update-rippled.sh
%{_bindir}/getRippledInfo
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

%files reporting
%define _prefix /opt/rippled-reporting
%doc rippled/README.md rippled/LICENSE.md

%{_bindir}/rippled-reporting
/usr/local/bin/rippled-reporting
%config(noreplace) /etc/opt/rippled-reporting/rippled-reporting.cfg
%config(noreplace) %{_prefix}/etc/rippled-reporting.cfg
%config(noreplace) %{_prefix}/etc/validators.txt
%config(noreplace) /etc/opt/rippled-reporting/validators.txt
%config(noreplace) /usr/lib/systemd/system/rippled-reporting.service
%config(noreplace) /usr/lib/systemd/system-preset/50-rippled-reporting.preset
%dir /var/log/rippled-reporting/
%dir /var/lib/rippled-reporting/
%{_bindir}/update-rippled-reporting.sh
%{_bindir}/getRippledReportingInfo
%{_prefix}/etc/update-rippled-reporting-cron
%config(noreplace) /etc/logrotate.d/rippled-reporting

%changelog
* Wed Aug 28 2019 Mike Ellery <mellery451@gmail.com>
- Switch to subproject build for validator-keys

* Wed May 15 2019 Mike Ellery <mellery451@gmail.com>
- Make validator-keys use local rippled build for core lib

* Wed Aug 01 2018 Mike Ellery <mellery451@gmail.com>
- add devel package for signing library

* Thu Jun 02 2016 Brandon Wilson <bwilson@ripple.com>
- Install validators.txt
