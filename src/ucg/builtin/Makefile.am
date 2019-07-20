#
# Copyright (c) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

noinst_LTLIBRARIES         = libucg_builtin.la
libucg_builtin_la_CFLAGS   = $(BASE_CFLAGS)
libucg_builtin_la_CPPFLAGS = $(BASE_CPPFLAGS)

noinst_HEADERS = \
	ops/builtin_ops.h \
	ops/builtin_cb.inl \
	plan/builtin_plan.h

libucg_builtin_la_SOURCES = \
	builtin.c \
	ops/builtin_ops.c \
	plan/builtin_tree.c \
	plan/builtin_neighbor.c \
	plan/builtin_recursive.c
