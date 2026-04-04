dnl SPDX-License-Identifier: Apache-2.0
AC_INIT([wardtest], [0.1.0], [loghyr@gmail.com])
AC_CONFIG_AUX_DIR([.])
AM_INIT_AUTOMAKE([foreign subdir-objects -Wall -Werror])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC
AM_PROG_AR
AC_PROG_RANLIB

dnl Default: debug build (-O0 -g).  Use --enable-optimize for -O2.
AC_ARG_ENABLE([optimize],
  [AS_HELP_STRING([--enable-optimize], [Build with -O2 instead of -O0])],
  [opt_level="-O2"], [opt_level="-O0"])

CFLAGS="-std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -g ${opt_level}"

PKG_CHECK_MODULES([CHECK], [check >= 0.12.0])

AC_CONFIG_FILES([Makefile src/Makefile tests/Makefile])
AC_OUTPUT
