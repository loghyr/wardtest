dnl SPDX-License-Identifier: Apache-2.0
AC_INIT([wardtest], [0.1.0], [loghyr@gmail.com])
AC_CONFIG_AUX_DIR([.])
AM_INIT_AUTOMAKE([foreign subdir-objects -Wall -Werror])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC

CFLAGS="$CFLAGS -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -g"

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
