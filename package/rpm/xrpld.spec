%if "%{?pkg_version}" == ""
%{error:pkg_version must be defined}
%endif

%if "%{?pkg_release}" == ""
%{error:pkg_release must be defined}
%endif

Name:     xrpld
Version:  %{pkg_version}
Release:  %{pkg_release}%{?dist}
Summary:  XRP Ledger daemon

License:  ISC
URL:      https://github.com/XRPLF/rippled

ExclusiveArch: x86_64 aarch64
BuildRequires: systemd-rpm-macros

%undefine _debugsource_packages
%debug_package
# Intentionally trade larger RPM artifacts for faster package validation.
%global _binary_payload w.ufdio
%global _find_debuginfo_dwz_opts %{nil}

%build_mtime_policy clamp_to_source_date_epoch

%{?systemd_requires}
%{?sysusers_requires_compat}

%description
xrpld is the reference implementation of the XRP Ledger protocol. It
participates in the peer-to-peer XRP Ledger network, processes
transactions, and maintains the ledger database.

%prep
:

%build
:

%install
install -Dm0755 %{_sourcedir}/xrpld                %{buildroot}%{_bindir}/%{name}
install -Dm0644 %{_sourcedir}/xrpld.cfg            %{buildroot}%{_sysconfdir}/%{name}/xrpld.cfg
install -Dm0644 %{_sourcedir}/validators.txt       %{buildroot}%{_sysconfdir}/%{name}/validators.txt

# systemd units, sysusers, tmpfiles, preset
install -Dm0644 %{_sourcedir}/xrpld.service        %{buildroot}%{_unitdir}/xrpld.service
install -Dm0644 %{_sourcedir}/xrpld.sysusers       %{buildroot}%{_sysusersdir}/xrpld.conf
install -Dm0644 %{_sourcedir}/xrpld.tmpfiles       %{buildroot}%{_tmpfilesdir}/xrpld.conf
install -Dm0644 /dev/null %{buildroot}%{_presetdir}/50-xrpld.preset
cat >%{buildroot}%{_presetdir}/50-xrpld.preset <<'EOF'
enable xrpld.service
EOF

# Logrotate config
install -Dm0644 %{_sourcedir}/xrpld.logrotate      %{buildroot}%{_sysconfdir}/logrotate.d/%{name}

# Docs
install -Dm0644 %{_sourcedir}/LICENSE.md %{buildroot}%{_docdir}/%{name}/LICENSE.md
install -Dm0644 %{_sourcedir}/README.md  %{buildroot}%{_docdir}/%{name}/README.md

# Legacy compatibility for pre-FHS package layouts.
# TODO: remove after rippled fully deprecated.
install -d %{buildroot}/usr/local/bin
ln -s %{_bindir}/%{name} %{buildroot}/usr/local/bin/rippled

%pre
%sysusers_create_package %{name} %{_sourcedir}/xrpld.sysusers

%post
systemd-tmpfiles --create %{_tmpfilesdir}/xrpld.conf || :
%systemd_post xrpld.service

%preun
%systemd_preun xrpld.service

%postun
%systemd_postun xrpld.service

%files
%license %{_docdir}/%{name}/LICENSE.md
%doc %{_docdir}/%{name}/README.md

%dir %{_sysconfdir}/%{name}

%{_bindir}/%{name}

%config(noreplace) %{_sysconfdir}/%{name}/xrpld.cfg
%config(noreplace) %{_sysconfdir}/%{name}/validators.txt
%config(noreplace) %{_sysconfdir}/logrotate.d/%{name}


%{_unitdir}/xrpld.service
%{_presetdir}/50-xrpld.preset
%{_sysusersdir}/xrpld.conf
%{_tmpfilesdir}/xrpld.conf
%ghost %dir /var/lib/xrpld
%ghost %dir /var/log/xrpld

# Legacy compatibility for pre-FHS package layouts.
# TODO: remove after rippled fully deprecated.
/usr/local/bin/rippled
