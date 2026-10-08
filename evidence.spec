# evidence.spec
#
# Copyright (c) 2002-2005 Azundris <evidence@azundris.com>
#

# required items
Name: evidence
Version: 0.9.8
Release: %(date '+%Y%m%d')
License: GPL
Group: Productivity/File utilities

%define manifest %{_builddir}/%{name}-%{version}-%{release}.manifest

# optional items
Vendor: Azundris
Distribution: SuSE 9.2 compatible
#Icon: evidence.xpm
URL: http://evidence.sourceforge.net/
Packager: Azundris <evidence@azundris.com>

# source + patches
Source: %{name}-%{version}.tar.gz

# RPM info
#AutoReqProv: no
# Provides: evidence
#Requires: evas >= 0.9.9, gtk2 >= 2.4.0, glib2 >= 2.4.0, freetype2 >= 2.1.3, glibc >= 2.3.2-5, XFree86-libs >= 4.3.0, libjpeg >= 6.2.0, pcre >= 3.9, imlib2 >= 1.1.0, libpng >= 1.2.5, zlib >= 1.1.4
#Conflicts:
#Prereq:

#Prefix: /usr
BuildRoot: /var/tmp/%{name}-%{version}

Summary: Evidence is a (finderesque?) GTK2/evas2 file manager.

%description
Evidence is a file-manager with plugins for everything -- from ultra-fast JPEG
previews to MP3/ID3 and Ogg tag editing. No bloat -- what you don't need, you
don't load. Micro-shell, MP3 thumbnailing and extensive theming opportunities
complete this slightly different browser.

%prep
%setup -q
#%patch0 -p1

%build
%configure --prefix=/usr --enable-x
make

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT
%makeinstall

# __os_install_post is implicitly expanded after the
# %install section... do it now, and then disable it,
# so all work is done before building manifest.

%{?__os_install_post}
%define __os_install_post %{nil}

# build the file list automagically into %{manifest}

cd $RPM_BUILD_ROOT
rm -f %{manifest}
find . -type d \
        | sed '1,2d;s,^\.,\%attr(-\,root\,root) \%dir ,' >> %{manifest}
find . -type f \
        | sed 's,^\.,\%attr(-\,root\,root) ,' >> %{manifest}
find . -type l \
        | sed 's,^\.,\%attr(-\,root\,root) ,' >> %{manifest}

#%pre
#%post
#%preun
#%postun

%clean
rm -f %{manifest}
rm -rf $RPM_BUILD_ROOT

%files -f %{manifest}
%defattr(-,root,root)
%doc docs AUTHORS COPYING* README NEWS ChangeLog DEPENDENCIES HELP_DEVELOP
#%doc README
#%docdir
#%config

%changelog
