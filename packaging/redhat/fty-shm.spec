#
#    fty-shm - Lockless metric sharing library for 42ity
#
#    Copyright (C) 2018 Eaton
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License along
#    with this program; if not, write to the Free Software Foundation, Inc.,
#    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#

# To build with draft APIs, use "--with drafts" in rpmbuild for local builds or add
#   Macros:
#   %_with_drafts 1
# at the BOTTOM of the OBS prjconf
%bcond_with drafts
%if %{with drafts}
%define DRAFTS yes
%else
%define DRAFTS no
%endif
%define SYSTEMD_UNIT_DIR %(pkg-config --variable=systemdsystemunitdir systemd)
Name:           fty-shm
Version:        1.0.0
Release:        1
Summary:        lockless metric sharing library for 42ity
License:        GPL-2.0+
URL:            https://42ity.org
Source0:        %{name}-%{version}.tar.gz
Group:          System/Libraries
# Note: ghostscript is required by graphviz which is required by
#       asciidoc. On Fedora 24 the ghostscript dependencies cannot
#       be resolved automatically. Thus add working dependency here!
BuildRequires:  ghostscript
BuildRequires:  asciidoc
BuildRequires:  automake
BuildRequires:  autoconf
BuildRequires:  libtool
BuildRequires:  pkgconfig
BuildRequires:  systemd-devel
BuildRequires:  systemd
%{?systemd_requires}
BuildRequires:  xmlto
BuildRequires:  gcc-c++
BuildRequires:  libsodium-devel
BuildRequires:  zeromq-devel
BuildRequires:  czmq-devel
BuildRequires:  malamute-devel
BuildRequires:  log4cplus-devel
BuildRequires:  fty-common-logging-devel
BuildRequires:  fty-proto-devel
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
fty-shm lockless metric sharing library for 42ity.

%package -n libfty_shm0
Group:          System/Libraries
Summary:        lockless metric sharing library for 42ity shared library

%description -n libfty_shm0
This package contains shared library for fty-shm: lockless metric sharing library for 42ity

%post -n libfty_shm0 -p /sbin/ldconfig
%postun -n libfty_shm0 -p /sbin/ldconfig

%files -n libfty_shm0
%defattr(-,root,root)
%{_libdir}/libfty_shm.so.*

%package devel
Summary:        lockless metric sharing library for 42ity
Group:          System/Libraries
Requires:       libfty_shm0 = %{version}
Requires:       libsodium-devel
Requires:       zeromq-devel
Requires:       czmq-devel
Requires:       malamute-devel
Requires:       log4cplus-devel
Requires:       fty-common-logging-devel
Requires:       fty-proto-devel

%description devel
lockless metric sharing library for 42ity development tools
This package contains development files for fty-shm: lockless metric sharing library for 42ity

%files devel
%defattr(-,root,root)
%{_includedir}/*
%{_libdir}/libfty_shm.so
%{_libdir}/pkgconfig/libfty_shm.pc
%{_mandir}/man3/*
%{_mandir}/man7/*

%prep

%setup -q

%build
sh autogen.sh
%{configure} --enable-drafts=%{DRAFTS} --with-systemd-units
make %{_smp_mflags}

%install
make install DESTDIR=%{buildroot} %{?_smp_mflags}

# remove static libraries
find %{buildroot} -name '*.a' | xargs rm -f
find %{buildroot} -name '*.la' | xargs rm -f

%files
%defattr(-,root,root)
%doc README.md
%{_bindir}/fty-shm-cleanup
%{_mandir}/man1/fty-shm-cleanup*
%config(noreplace) %{_sysconfdir}/fty-shm/fty-shm-cleanup.cfg
%{SYSTEMD_UNIT_DIR}/fty-shm-cleanup.service
%{SYSTEMD_UNIT_DIR}/fty-shm-cleanup.timer
%dir %{_sysconfdir}/fty-shm
%if 0%{?suse_version} > 1315
%post
%systemd_post fty-shm-cleanup.service fty-shm-cleanup.timer
%preun
%systemd_preun fty-shm-cleanup.service fty-shm-cleanup.timer
%postun
%systemd_postun_with_restart fty-shm-cleanup.service fty-shm-cleanup.timer
%endif

%changelog
