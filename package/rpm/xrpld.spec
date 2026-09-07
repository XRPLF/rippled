%if "%{?pkg_version}" == ""
%{error:pkg_version must be defined}
%endif

%if "%{?pkg_release}" == ""
%{error:pkg_release must be defined}
%endif

# The on-disk name, which every package ships under. A variant build
# (build_pkg.py --variant) only suffixes the package name, e.g. xrpld-assert.
%global app xrpld

Name:     %{app}%{?pkg_variant:-%{pkg_variant}}
Version:  %{pkg_version}
Release:  %{pkg_release}%{?dist}
Summary:  XRP Ledger daemon%{?pkg_variant: (%{pkg_variant} build)}

License:  ISC
URL:      https://github.com/XRPLF/rippled

ExclusiveArch: x86_64 aarch64
BuildRequires: systemd-rpm-macros

# A variant owns the same paths, so it stands in for the plain package.
%if "%{?pkg_variant}" != ""
Conflicts: %{app}
Provides:  %{app} = %{version}-%{release}
%endif

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
install -Dm0755 %{_sourcedir}/xrpld                %{buildroot}%{_bindir}/%{app}
install -Dm0755 %{_sourcedir}/validator-keys       %{buildroot}%{_bindir}/validator-keys
install -Dm0644 %{_sourcedir}/xrpld.cfg            %{buildroot}%{_sysconfdir}/%{app}/xrpld.cfg
install -Dm0644 %{_sourcedir}/validators.txt       %{buildroot}%{_sysconfdir}/%{app}/validators.txt

# systemd units, sysusers, tmpfiles, preset
install -Dm0644 %{_sourcedir}/xrpld.service        %{buildroot}%{_unitdir}/xrpld.service
install -Dm0644 %{_sourcedir}/xrpld.sysusers       %{buildroot}%{_sysusersdir}/xrpld.conf
install -Dm0644 %{_sourcedir}/xrpld.tmpfiles       %{buildroot}%{_tmpfilesdir}/xrpld.conf
install -d %{buildroot}%{_presetdir}
cat >%{buildroot}%{_presetdir}/50-%{app}.preset <<'EOF'
enable xrpld.service
EOF

# Logrotate config
install -Dm0644 %{_sourcedir}/xrpld.logrotate      %{buildroot}%{_sysconfdir}/logrotate.d/%{app}

# Docs
install -Dm0644 %{_sourcedir}/LICENSE.md %{buildroot}%{_docdir}/%{name}/LICENSE.md
install -Dm0644 %{_sourcedir}/README.md  %{buildroot}%{_docdir}/%{name}/README.md
# Upstream notice for the bundled validator-keys tool.
install -Dm0644 %{_sourcedir}/validator-keys-LICENSE %{buildroot}%{_docdir}/%{name}/validator-keys-LICENSE

# Legacy compatibility for pre-FHS package layouts.
# TODO: remove after rippled fully deprecated.
install -d %{buildroot}/usr/local/bin
ln -s %{_bindir}/%{app} %{buildroot}/usr/local/bin/rippled

%pre
%sysusers_create_package %{app} %{_sourcedir}/xrpld.sysusers

%post
%tmpfiles_create_package %{app} %{_sourcedir}/xrpld.tmpfiles
%systemd_post xrpld.service

%preun
%systemd_preun xrpld.service

%postun
%systemd_postun xrpld.service
# A flavour swap installs the replacement before erasing this package, so the
# %%preun above has just disabled a unit the replacement still owns. The unit
# file surviving our own erase is exactly that case; a plain erase takes it.
if [ $1 -eq 0 ] && [ -f %{_unitdir}/xrpld.service ]; then
    systemctl preset xrpld.service >/dev/null 2>&1 || :
fi

%files
%attr(0755,root,root) %dir %{_docdir}/%{name}
%license %{_docdir}/%{name}/LICENSE.md
%license %{_docdir}/%{name}/validator-keys-LICENSE
%doc %{_docdir}/%{name}/README.md

%attr(0755,root,root) %dir %{_sysconfdir}/%{app}

%{_bindir}/%{app}
%{_bindir}/validator-keys

%config(noreplace) %{_sysconfdir}/%{app}/xrpld.cfg
%config(noreplace) %{_sysconfdir}/%{app}/validators.txt
%config(noreplace) %{_sysconfdir}/logrotate.d/%{app}


%{_unitdir}/xrpld.service
%attr(0644,root,root) %{_presetdir}/50-%{app}.preset
%{_sysusersdir}/xrpld.conf
%{_tmpfilesdir}/xrpld.conf
%ghost %dir /var/lib/xrpld
%ghost %dir /var/log/xrpld

# Legacy compatibility for pre-FHS package layouts.
# TODO: remove after rippled fully deprecated.
/usr/local/bin/rippled
