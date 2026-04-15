# SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only

TESTS = test_wardtest
check_PROGRAMS = test_wardtest

test_wardtest_SOURCES = \
	test_rng.c \
	test_crc32.c \
	test_xor.c \
	test_rs.c \
	test_chunk.c \
	test_meta.c \
	test_control.c \
	test_codec.c \
	test_actions.c \
	test_main.c

test_wardtest_CFLAGS = \
	-I$(top_srcdir)/src \
	$(CHECK_CFLAGS)

test_wardtest_LDADD = \
	$(top_builddir)/src/libwardtest.a \
	$(CHECK_LIBS) \
	-lpthread
