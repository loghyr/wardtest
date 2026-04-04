# SPDX-License-Identifier: Apache-2.0

bin_PROGRAMS = wardtest

wardtest_SOURCES = \
	wardtest.c \
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

wardtest_CFLAGS = -I$(srcdir)
wardtest_LDADD = -lpthread
