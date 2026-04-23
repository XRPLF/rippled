Name:     xrpld
Version:  %{xrpld_version}
Release:  %{xrpld_release}%{?dist}
Summary:  XRP Ledger daemon

License:  ISC
URL:      https://github.com/XRPLF/rippled

ExclusiveArch: x86_64 aarch64
BuildRequires: systemd-rpm-macros

%undefine _debugsource_packages
%debug_package

%build_mtime_policy clamp_to_source_date_epoch

%{?systemd_requires}
%{?sysusers_requires_compat}

%description
xrpld is the reference implementation of the XRP Ledger protocol. It
participates in the peer-to-peer XRP Ledger network, processes
transactions, and maintains the ledger database.

%prep
:

%install
rm -rf %{buildroot}

SRC=%{_sourcedir}

install -Dm0755 ${SRC}/xrpld           %{buildroot}/opt/xrpld/bin/xrpld
install -Dm0644 ${SRC}/xrpld.cfg       %{buildroot}/opt/xrpld/etc/xrpld.cfg
install -Dm0644 ${SRC}/validators.txt  %{buildroot}/opt/xrpld/etc/validators.txt

mkdir -p %{buildroot}/etc/opt  %{buildroot}/usr/bin %{buildroot}/usr/local/bin
ln -s /opt/xrpld/etc           %{buildroot}/etc/opt/xrpld
ln -s /opt/xrpld/bin/xrpld     %{buildroot}/usr/bin/xrpld

# TODO: remove when rippled deprecated
ln -s xrpld                 %{buildroot}/opt/xrpld/bin/rippled
ln -s /opt/xrpld/bin/xrpld  %{buildroot}/usr/local/bin/rippled
ln -s xrpld.cfg             %{buildroot}/opt/xrpld/etc/rippled.cfg
ln -s /opt/xrpld            %{buildroot}/opt/ripple
ln -s /etc/opt/xrpld        %{buildroot}/etc/opt/ripple

install -Dm0644 ${SRC}/xrpld.service        %{buildroot}%{_unitdir}/xrpld.service
install -Dm0644 ${SRC}/update-xrpld.service %{buildroot}%{_unitdir}/update-xrpld.service
install -Dm0644 ${SRC}/update-xrpld.timer   %{buildroot}%{_unitdir}/update-xrpld.timer

install -Dm0644 ${SRC}/xrpld.sysusers %{buildroot}%{_sysusersdir}/xrpld.conf
install -Dm0644 ${SRC}/xrpld.tmpfiles %{buildroot}%{_tmpfilesdir}/xrpld.conf

install -Dm0644 ${SRC}/50-xrpld.preset %{buildroot}%{_presetdir}/50-xrpld.preset

install -Dm0755 ${SRC}/update-xrpld.sh    %{buildroot}/opt/xrpld/bin/update-xrpld.sh
install -Dm0644 ${SRC}/update-xrpld-cron  %{buildroot}/opt/xrpld/bin/update-xrpld-cron
install -Dm0644 ${SRC}/xrpld.logrotate    %{buildroot}/opt/xrpld/bin/xrpld.logrotate

install -Dm0644 ${SRC}/LICENSE.md %{buildroot}/opt/xrpld/share/LICENSE.md
install -Dm0644 ${SRC}/README.md  %{buildroot}/opt/xrpld/share/README.md

%pre
%sysusers_create_package xrpld %{_sourcedir}/xrpld.sysusers

%post
systemd-tmpfiles --create %{_tmpfilesdir}/xrpld.conf || :
%systemd_post xrpld.service update-xrpld.timer

%preun
%systemd_preun xrpld.service update-xrpld.timer

%postun
%systemd_postun_with_restart xrpld.service

%files
%license /opt/xrpld/share/LICENSE.md
%doc /opt/xrpld/share/README.md

%dir /opt/xrpld
%dir /opt/xrpld/bin
%dir /opt/xrpld/etc

/opt/xrpld/bin/xrpld
/opt/xrpld/bin/xrpld.logrotate
/opt/xrpld/bin/update-xrpld.sh
/opt/xrpld/bin/update-xrpld-cron

/usr/bin/xrpld
/etc/opt/xrpld

%config(noreplace) /opt/xrpld/etc/xrpld.cfg
%config(noreplace) /opt/xrpld/etc/validators.txt

%{_unitdir}/xrpld.service
%{_unitdir}/update-xrpld.service
%{_unitdir}/update-xrpld.timer
%{_presetdir}/50-xrpld.preset
%{_sysusersdir}/xrpld.conf
%{_tmpfilesdir}/xrpld.conf

%ghost %dir /var/lib/xrpld
%ghost %dir /var/log/xrpld

# TODO: remove when rippled deprecated
/opt/xrpld/bin/rippled
/usr/local/bin/rippled
/opt/xrpld/etc/rippled.cfg
/etc/opt/ripple
/opt/ripple
