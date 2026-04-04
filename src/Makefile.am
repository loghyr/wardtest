# SPDX-License-Identifier: Apache-2.0

bin_PROGRAMS = wardtest

# Convenience library for unit tests to link against
noinst_LIBRARIES = libwardtest.a

COMMON_SOURCES = \
	wardtest.h \
	machine.c \
	rng.c \
	crc32.c \
	xor.c \
	rs.c \
	codec.c \
	control.c \
	stop.c \
	state.c \
	chunk.c \
	meta.c \
	history.c \
	actions.c

libwardtest_a_SOURCES = $(COMMON_SOURCES)
libwardtest_a_CFLAGS = -I$(srcdir)

wardtest_SOURCES = wardtest.c $(COMMON_SOURCES)
wardtest_CFLAGS = -I$(srcdir)
wardtest_LDADD = -lpthread
