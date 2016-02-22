%define product_name ixpdimm_sw
%define build_version 99.99.99.9999
%define build_release 1
%define corename lib%{product_name}-core
%define cliname %{product_name}
%define monitorname lib%{product_name}-monitor
%define cimlibs lib%{product_name}-cim
%define dname lib%{product_name}-devel
%define _unpackaged_files_terminate_build 0

Name: lib%{product_name}
Version: %{build_version}
Release: %{build_release}%{?dist}
Summary: API for development of %{product_name} management utilities
License: BSD
Group: Applications/System
URL: https://01.org/ixpdimm-sw
Source: %{product_name}.tar.bz2

%define  debug_package %{nil}

%description
An application program interface (API) for configuring and managing
%{product_name}. Including basic inventory, capacity provisioning,
health monitoring, and troubleshooting.

%package -n %dname
Summary:        Development files for %{name}
License:        BSD
Group:          Development/Libraries
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description -n %dname
The %{name}-devel package contains header files for
developing applications that use %{name}.

%package -n %corename
Summary:        Development files for %{name}
License:        BSD
Group:          Application/System
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description -n %corename
The %{corename} package contains libraries that support
other %{product_name} products.


%package -n %cimlibs
Summary:        CIM provider for %{name}

License:        BSD
Group:          Application/System
Requires:       %{corename}%{?_isa} = %{version}-%{release}
Requires:       pywbem
Requires(pre):  pywbem
Requires(post): pywbem

%description -n %cimlibs
%{cimlibs} is a common information model (CIM) provider that exposes
%{product_name} as standard CIM objects in order to plug-in to various
common information model object managers (CIMOMS).

%package -n %monitorname
Summary:        Daemon for monitoring the status of %{product_name}
License:        BSD
Group:          Application/System
Requires:       %{cimlibs}%{?_isa} = %{version}-%{release}
BuildRequires:  systemd-rpm-macros
%{?systemd_requires}

%description -n %monitorname
A daemon for monitoring the health and status of %{product_name}

%package -n %cliname
Summary:        CLI for managment of %{product_name}
License:        BSD
Group:          Application/System
Requires:       %{cimlibs}%{?_isa} = %{version}-%{release}

%description -n %cliname
A command line interface (CLI) application for configuring and
managing %{prodcut_name}. Including commands for basic inventory,
capacity provisioning, health monitoring, and troubleshooting.


%prep
%setup -q -n %{product_name}

%build
make BUILDNUM=%{build_version} RELEASE=1 DATADIR=%{_datadir} LINUX_PRODUCT_NAME=%{product_name} CFLAGS_EXTERNAL="%{?cflag}"

%install
make install RELEASE=1 RPM_ROOT=%{buildroot} LIB_DIR=%{_libdir} INCLUDE_DIR=%{_includedir} BIN_DIR=%{_bindir} DATADIR=%{_datadir} UNIT_DIR=%{_unitdir} LINUX_PRODUCT_NAME=%{product_name} SYSCONF_DIR=%{_sysconfdir} MANPAGE_DIR=%{_mandir}

%post -n %corename
/sbin/ldconfig

%post -n %cimlibs
/sbin/ldconfig
if [ -x /usr/sbin/cimserver ]
then
	cimserver --status &> /dev/null
	if [ $? -eq 0 ]
	then
	CIMMOF=cimmof
	else
    for repo in /var/lib/Pegasus /var/lib/pegasus /usr/local/var/lib/pegasus /var/local/lib/pegasus /var/opt/tog-pegasus /opt/ibm/icc/cimom
    do
      if [ -d $repo/repository ]
      then
	  CIMMOF="cimmofl -R $repo"
      fi
    done
	fi
	for ns in interop root/interop root/PG_Interop;
	do
	   $CIMMOF -E -n$ns %{_datadir}/%{product_name}/Pegasus/mof/pegasus_register.mof &> /dev/null
	   if [ $? -eq 0 ]
	   then
			$CIMMOF -uc -n$ns %{_datadir}/%{product_name}/Pegasus/mof/pegasus_register.mof &> /dev/null
			$CIMMOF -uc -n$ns %{_datadir}/%{product_name}/Pegasus/mof/profile_registration.mof &> /dev/null
			break
	   fi
	done
	$CIMMOF -aE -uc -n root/intelwbem %{_datadir}/%{product_name}/Pegasus/mof/intelwbem.mof &> /dev/null
fi
if [ -x /usr/sbin/sfcbd ]
then
	RESTART=0
	systemctl is-active sblim-sfcb.service &> /dev/null
	if [ $? -eq 0 ]
	then
		RESTART=1
		systemctl stop sblim-sfcb.service &> /dev/null
	fi

	sfcbstage -n root/intelwbem -r %{_datadir}/%{product_name}/sfcb/INTEL_NVDIMM.reg %{_datadir}/%{product_name}/sfcb/sfcb_intelwbem.mof
	sfcbrepos -f

	if [[ $RESTART -gt 0 ]]
	then
		systemctl start sblim-sfcb.service &> /dev/null
	fi
fi

%post -n %monitorname
%service_add_post nvmmonitor.service
exit 0

%post
/sbin/ldconfig

%postun -n %corename
/sbin/ldconfig

%postun -n %cimlibs 
/sbin/ldconfig

%pre -n %cimlibs
# If upgrading, deregister old version
if [ "$1" -gt 1 ]; then
	RESTART=0
	if [ -x /usr/sbin/cimserver ]
	then
		cimserver --status &> /dev/null
		if [ $? -gt 0 ]
		then
			RESTART=1
			cimserver enableHttpConnection=false enableHttpsConnection=false enableRemotePrivilegedUserAccess=false slp=false &> /dev/null
		fi
		cimprovider -d -m intelwbemprovider &> /dev/null
		cimprovider -r -m intelwbemprovider &> /dev/null
		mofcomp -v -r -n root/intelwbem %{_datadir}/%{product_name}/Pegasus/mof/intelwbem.mof &> /dev/null
		mofcomp -v -r -n root/intelwbem %{_datadir}/%{product_name}/Pegasus/mof/profile_registration.mof &> /dev/null
		if [[ $RESTART -gt 0 ]]
		then
			cimserver -s &> /dev/null
		fi
	fi
fi

%preun -n %cimlibs
RESTART=0
if [ -x /usr/sbin/cimserver ]
then
	cimserver --status &> /dev/null
	if [ $? -gt 0 ]
	then
		RESTART=1
		cimserver enableHttpConnection=false enableHttpsConnection=false enableRemotePrivilegedUserAccess=false slp=false &> /dev/null
	fi
	cimprovider -d -m intelwbemprovider &> /dev/null
	cimprovider -r -m intelwbemprovider &> /dev/null
	mofcomp -r -n root/intelwbem %{_datadir}/%{product_name}/Pegasus/mof/intelwbem.mof &> /dev/null
	mofcomp -v -r -n root/intelwbem %{_datadir}/%{product_name}/Pegasus/mof/profile_registration.mof &> /dev/null
	if [[ $RESTART -gt 0 ]]
	then
		cimserver -s &> /dev/null
	fi
fi

if [ -x /usr/sbin/sfcbd ]
then
	RESTART=0
	systemctl is-active sblim-sfcb.service &> /dev/null
	if [ $? -eq 0 ]
	then
		RESTART=1
		systemctl stop sblim-sfcb.service &> /dev/null
	fi

	sfcbunstage -n root/intelwbem -r INTEL_NVDIMM.reg sfcb_intelwbem.mof
	sfcbrepos -f

	if [[ $RESTART -gt 0 ]]
	then
		systemctl start sblim-sfcb.service &> /dev/null
	fi
fi

%preun -n %monitorname
%service_del_preun nvmmonitor.service

%postun -n %monitorname
%service_del_postun nvmmonitor.service

%preun
/sbin/ldconfig

%files
%defattr(-,root,root)
%{_libdir}/libnvm.so.*
%dir %{_datadir}/%{product_name}
%{_datadir}/%{product_name}/*.pem
%config(noreplace) %{_datadir}/%{product_name}/*.dat*
%license LICENSE

%files -n %dname
%defattr(-,root,root)
%{_libdir}/libnvm.so
%{_includedir}/nvm_types.h
%{_includedir}/nvm_management.h
%license LICENSE

%files -n %cimlibs
%defattr(-,root,root)
%{_libdir}/cmpi/libnvmwbem.so*
%dir %{_datadir}/%{product_name}/Pegasus
%dir %{_datadir}/%{product_name}/Pegasus/mof
%dir %{_datadir}/%{product_name}/sfcb
%{_datadir}/%{product_name}/sfcb/*.reg
%{_datadir}/%{product_name}/sfcb/*.mof
%{_datadir}/%{product_name}/Pegasus/mof/*.mof
%{_sysconfdir}/ld.so.conf.d/%{product_name}-%{_arch}.conf
%license LICENSE

%files -n %monitorname
%defattr(-,root,root)
%{_bindir}/nvmmonitor
%{_unitdir}/nvmmonitor.service
%license LICENSE
%{_mandir}/man8/nvmmonitor*

%files -n %cliname
%defattr(-,root,root)
%{_bindir}/nvmcli
%{_libdir}/libcrfeatures.so*
%license LICENSE
%{_mandir}/man8/nvmcli*

%changelog
* Wed Dec 24 2015 nicholas.w.moulin@intel.com
- Initial rpm release
