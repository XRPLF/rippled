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

# Primary FHS layout
install -Dm0755 ${SRC}/xrpld           %{buildroot}%{_bindir}/xrpld
install -Dm0644 ${SRC}/xrpld.cfg       %{buildroot}%{_sysconfdir}/xrpld/xrpld.cfg
install -Dm0644 ${SRC}/validators.txt  %{buildroot}%{_sysconfdir}/xrpld/validators.txt

# systemd units, sysusers, tmpfiles, preset
install -Dm0644 ${SRC}/xrpld.service        %{buildroot}%{_unitdir}/xrpld.service
install -Dm0644 ${SRC}/update-xrpld.service %{buildroot}%{_unitdir}/update-xrpld.service
install -Dm0644 ${SRC}/update-xrpld.timer   %{buildroot}%{_unitdir}/update-xrpld.timer
install -Dm0644 ${SRC}/xrpld.sysusers       %{buildroot}%{_sysusersdir}/xrpld.conf
install -Dm0644 ${SRC}/xrpld.tmpfiles       %{buildroot}%{_tmpfilesdir}/xrpld.conf
install -Dm0644 ${SRC}/50-xrpld.preset      %{buildroot}%{_presetdir}/50-xrpld.preset

# Logrotate config (active by default)
install -Dm0644 ${SRC}/xrpld.logrotate      %{buildroot}%{_sysconfdir}/logrotate.d/xrpld

# Update helper (not on PATH; the systemd update timer references it by absolute path)
install -Dm0755 ${SRC}/update-xrpld.sh      %{buildroot}%{_libexecdir}/xrpld/update-xrpld.sh

# Docs
install -Dm0644 ${SRC}/LICENSE.md %{buildroot}%{_docdir}/xrpld/LICENSE.md
install -Dm0644 ${SRC}/README.md  %{buildroot}%{_docdir}/xrpld/README.md

# Legacy compat symlinks (remove next major release)
mkdir -p %{buildroot}/usr/local/bin
ln -s %{_bindir}/xrpld   %{buildroot}/usr/local/bin/rippled
ln -s xrpld.cfg          %{buildroot}%{_sysconfdir}/xrpld/rippled.cfg

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
%license %{_docdir}/xrpld/LICENSE.md
%doc %{_docdir}/xrpld/README.md

%dir %{_sysconfdir}/xrpld
%dir %{_libexecdir}/xrpld

%{_bindir}/xrpld

%config(noreplace) %{_sysconfdir}/xrpld/xrpld.cfg
%config(noreplace) %{_sysconfdir}/xrpld/validators.txt
%config(noreplace) %{_sysconfdir}/logrotate.d/xrpld

%{_libexecdir}/xrpld/update-xrpld.sh

%{_unitdir}/xrpld.service
%{_unitdir}/update-xrpld.service
%{_unitdir}/update-xrpld.timer
%{_presetdir}/50-xrpld.preset
%{_sysusersdir}/xrpld.conf
%{_tmpfilesdir}/xrpld.conf

%ghost %dir /var/lib/xrpld
%ghost %dir /var/log/xrpld

# Legacy compat symlinks (remove next major release)
/usr/local/bin/rippled
%{_sysconfdir}/xrpld/rippled.cfg
