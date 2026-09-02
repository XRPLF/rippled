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

# These have to precede %%debug_package: it opens the debuginfo subpackage, and
# any tag after it is silently dropped from the main package.
%{?systemd_requires}
%{?sysusers_requires_compat}

%undefine _debugsource_packages
%debug_package
# Level 3 rather than the el9 default of 19: it shrinks the multi-gigabyte
# debuginfo package roughly fourfold in about a second, where 19 would spend
# minutes on it.
%global _binary_payload w3.zstdio
%global _find_debuginfo_dwz_opts %{nil}

# Reproducibility: the first two take their value from the SOURCE_DATE_EPOCH
# build_pkg.py exports. Without these the header records the wall clock and the
# build container's hostname, so two builds of the same commit differ.
%global clamp_mtime_to_source_date_epoch 1
%global use_source_date_epoch_as_buildtime 1
%global _buildhost xrplf.org


%description
xrpld is the reference implementation of the XRP Ledger protocol. It
participates in the peer-to-peer XRP Ledger network, processes
transactions, and maintains the ledger database.
This package also includes the validator-keys tool for validator key
management.

%prep
:

%build
:

%install
install -Dm0755 %{_sourcedir}/xrpld                %{buildroot}%{_bindir}/%{name}
install -Dm0755 %{_sourcedir}/validator-keys       %{buildroot}%{_bindir}/validator-keys
install -Dm0644 %{_sourcedir}/xrpld.cfg            %{buildroot}%{_sysconfdir}/%{name}/xrpld.cfg
install -Dm0644 %{_sourcedir}/validators.txt       %{buildroot}%{_sysconfdir}/%{name}/validators.txt

# systemd units, sysusers, tmpfiles, preset
install -Dm0644 %{_sourcedir}/xrpld.service        %{buildroot}%{_unitdir}/xrpld.service
install -Dm0644 %{_sourcedir}/xrpld.sysusers       %{buildroot}%{_sysusersdir}/xrpld.conf
install -Dm0644 %{_sourcedir}/xrpld.tmpfiles       %{buildroot}%{_tmpfilesdir}/xrpld.conf
install -d %{buildroot}%{_presetdir}
cat >%{buildroot}%{_presetdir}/50-xrpld.preset <<'EOF'
enable xrpld.service
EOF

# Logrotate config
install -Dm0644 %{_sourcedir}/xrpld.logrotate      %{buildroot}%{_sysconfdir}/logrotate.d/%{name}

# Docs
install -Dm0644 %{_sourcedir}/LICENSE.md %{buildroot}%{_docdir}/%{name}/LICENSE.md
install -Dm0644 %{_sourcedir}/README.md  %{buildroot}%{_docdir}/%{name}/README.md
# Upstream notice for the bundled validator-keys tool.
install -Dm0644 %{_sourcedir}/validator-keys-LICENSE %{buildroot}%{_docdir}/%{name}/validator-keys-LICENSE

# Legacy compatibility for pre-FHS package layouts.
# TODO: remove after rippled fully deprecated.
install -d %{buildroot}/usr/local/bin
ln -s %{_bindir}/%{name} %{buildroot}/usr/local/bin/rippled

%pre
%sysusers_create_package %{name} %{_sourcedir}/xrpld.sysusers

%post
%tmpfiles_create_package %{name} %{_sourcedir}/xrpld.tmpfiles
%systemd_post xrpld.service

%preun
%systemd_preun xrpld.service

%postun
%systemd_postun xrpld.service

%files
%dir %{_docdir}/%{name}
%license %{_docdir}/%{name}/LICENSE.md
%license %{_docdir}/%{name}/validator-keys-LICENSE
%doc %{_docdir}/%{name}/README.md

%attr(0755,root,root) %dir %{_sysconfdir}/%{name}

%{_bindir}/%{name}
%{_bindir}/validator-keys

%config(noreplace) %{_sysconfdir}/%{name}/xrpld.cfg
%config(noreplace) %{_sysconfdir}/%{name}/validators.txt
%config(noreplace) %{_sysconfdir}/logrotate.d/%{name}


%{_unitdir}/xrpld.service
%attr(0644,root,root) %{_presetdir}/50-xrpld.preset
%{_sysusersdir}/xrpld.conf
%{_tmpfilesdir}/xrpld.conf
%ghost %dir /var/lib/xrpld
%ghost %dir /var/log/xrpld

# Legacy compatibility for pre-FHS package layouts.
# TODO: remove after rippled fully deprecated.
/usr/local/bin/rippled
