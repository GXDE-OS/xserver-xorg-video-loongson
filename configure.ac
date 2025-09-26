#  Copyright 2005 Adam Jackson.
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  on the rights to use, copy, modify, merge, publish, distribute, sub
#  license, and/or sell copies of the Software, and to permit persons to whom
#  the Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice (including the next
#  paragraph) shall be included in all copies or substantial portions of the
#  Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
#  ADAM JACKSON BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# Process this file with autoconf to produce a configure script

# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([xf86-video-loongson],
        [0.1.0],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xorg],
        [xf86-video-loongson])

AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_AUX_DIR(.)

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])
AC_SYS_LARGEFILE

# Require X.Org macros 1.8 or later for MAN_SUBSTS set by XORG_MANPAGE_SECTIONS
m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([must install xorg-macros 1.8 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.8)
XORG_DEFAULT_OPTIONS

AM_MAINTAINER_MODE

# Initialize libtool
AC_DISABLE_STATIC
AC_PROG_LIBTOOL

AC_CHECK_HEADERS([sys/ioctl.h])
AC_CHECK_HEADERS([stdint.h])

if test "x$GCC" = "xyes"; then
	CFLAGS="$CFLAGS -Wall"
fi

AH_TOP([#include "xorg-server.h"])

# Define a configure option for an alternate module directory
AC_ARG_WITH(xorg-module-dir,
            AS_HELP_STRING([--with-xorg-module-dir=DIR],
                           [Default xorg module directory [[default=$libdir/xorg/modules]]]),
            [moduledir="$withval"],
            [moduledir="$libdir/xorg/modules"])

# Store the list of server defined optional extensions in REQUIRED_MODULES
XORG_DRIVER_CHECK_EXT(RANDR, randrproto)
XORG_DRIVER_CHECK_EXT(RENDER, renderproto)
XORG_DRIVER_CHECK_EXT(XV, videoproto)
XORG_DRIVER_CHECK_EXT(DPMSExtension, xextproto)

# Checks for libraries.
PKG_CHECK_MODULES(LIBDRM, [libdrm >= 2.4.89])
# PKG_CHECK_MODULES(LIBDRM_ETNAVIV, [libdrm_etnaviv >= 2.4.91])
PKG_CHECK_MODULES(GBM, [gbm])

# Obtain compiler/linker options for the driver dependencies
PKG_CHECK_MODULES(XORG, [xorg-server >= 1.13 xproto fontsproto xf86driproto $REQUIRED_MODULES])
PKG_CHECK_MODULES(XEXT, [xextproto >= 7.0.99.1],
                  HAVE_XEXTPROTO_71="yes"; AC_DEFINE(HAVE_XEXTPROTO_71, 1, [xextproto 7.1 available]),
                  HAVE_XEXTPROTO_71="no")
AM_CONDITIONAL(HAVE_XEXTPROTO_71, [ test "$HAVE_XEXTPROTO_71" = "yes" ])

# Section "OutputClass" is only supported as of xserver 1.16
PKG_CHECK_EXISTS([xorg-server >= 1.16],
		 [sysconfigdir=`$PKG_CONFIG --variable=sysconfigdir xorg-server`],
		 [sysconfigdir=""])
AM_CONDITIONAL(HAS_XORG_CONF_DIR, [test "x$sysconfigdir" != "x"])

# Define a configure option for an alternate X Server configuration directory
AC_ARG_WITH(xorg-conf-dir,
            AS_HELP_STRING([--with-xorg-conf-dir=DIR],
                           [Default xorg.conf.d directory [[default=${prefix}/share/X11/xorg.conf.d]]]),
            [configdir="$withval"],
            [configdir='${prefix}/share/X11/xorg.conf.d'])
AC_SUBST(configdir)

AC_ARG_ENABLE([udev],
		AS_HELP_STRING([--disable-udev], [Disable libudev support [default=auto]]),
		[enable_udev="$enableval"],
		[enable_udev=auto])
if test "x$enable_udev" != "xno"; then
	PKG_CHECK_MODULES(LIBUDEV, [libudev], [LIBUDEV=yes], [LIBUDEV=no])
	if test "x$LIBUDEV" = xyes; then
		AC_DEFINE(HAVE_LIBUDEV, 1,[libudev support])
	elif test "x$enable_udev" != "xauto"; then
		AC_MSG_ERROR([Building with udev requested but libudev not found])
	fi
fi
AM_CONDITIONAL(LIBUDEV, test x$LIBUDEV = xyes)

SAVE_CFLAGS=$CFLAGS
CFLAGS=$LIBDRM_CFLAGS $XORG_CFLAGS

# Checks for header files.
AC_HEADER_STDC

PKG_CHECK_MODULES([PCIACCESS], [pciaccess >= 0.10])

PKG_CHECK_MODULES(UDEV, [libudev], [udev=yes], [udev=no])
if test x"$udev" = xyes; then
        AC_DEFINE(HAVE_UDEV,1,[Enable udev-based monitor hotplug detection])
fi

AC_CHECK_DECL(RegionDuplicate,
	      [AC_DEFINE(HAVE_REGIONDUPLICATE, 1,
	      [Have RegionDuplicate API])], [],
	      [#include <xorg-server.h>
	       #include <regionstr.h>])

AC_CHECK_DECL(fbGlyphs,
	      [AC_DEFINE(HAVE_FBGLYPHS, 1, [Have fbGlyphs API])], [],
	      [#include <X11/Xmd.h>
	       #include <X11/Xfuncproto.h>
	       #include <X11/extensions/renderproto.h>
	       #include <xorg-server.h>
	       #include <picture.h>
	       #include <glyphstr.h>
	       #include <fbpict.h>])

AC_CHECK_FUNCS([drmPrimeFDToHandle])



AC_CHECK_HEADERS([misyncshm.h], [], [],
		 [#include <X11/Xdefs.h>
		 #include <X11/Xfuncproto.h>
		 #include <xorg-server.h>
		 #include <screenint.h>])

AC_CHECK_HEADERS([present.h], [], [],
		 [#include <X11/Xmd.h>
		 #include <X11/Xproto.h>
		 #include "xorg-server.h"
		 #include <X11/X.h>])

AC_CHECK_HEADERS([dri3.h], [], [],
		 [#include <X11/Xmd.h>
		 #include <xorg-server.h>])

CFLAGS=$SAVE_CFLAGS

AC_SUBST([moduledir])

DRIVER_NAME=loongson
AC_SUBST([DRIVER_NAME])

AC_CONFIG_FILES([
                Makefile
                src/Makefile
                man/Makefile
                conf/Makefile
])
AC_OUTPUT

dnl
dnl Output some configuration info for the user
dnl
echo ""
echo "        prefix:              $prefix"
echo "        exec_prefix:         $exec_prefix"
echo "        libdir:              $libdir"
echo "        includedir:          $includedir"
echo "        configdir:           $configdir"

echo ""
echo "        CFLAGS:              $CFLAGS"
echo "        Macros:              $DEFINES"

echo ""
echo "        Run '${MAKE-make}' to build xf86-video-loongson"
echo ""
