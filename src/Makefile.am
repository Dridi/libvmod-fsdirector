#
# libvmod-fsdirector - FileSystem module for Varnish 4
#
# Copyright (C) 2013, Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
# All rights reserved.
#
# Redistribution  and use in source and binary forms, with or without
# modification,  are permitted provided that the following conditions
# are met:
#
# 1. Redistributions   of  source   code   must   retain  the   above
#    copyright  notice, this  list of  conditions  and the  following
#    disclaimer.
# 2. Redistributions   in  binary  form  must  reproduce  the   above
#    copyright  notice, this  list of  conditions and  the  following
#    disclaimer   in  the   documentation   and/or  other   materials
#    provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT  NOT
# LIMITED  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND  FITNESS
# FOR  A  PARTICULAR  PURPOSE ARE DISCLAIMED. IN NO EVENT  SHALL  THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL,    SPECIAL,   EXEMPLARY,   OR   CONSEQUENTIAL   DAMAGES
# (INCLUDING,  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES;  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT  LIABILITY,  OR  TORT (INCLUDING  NEGLIGENCE  OR  OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
# OF THE POSSIBILITY OF SUCH DAMAGE.
#

INCLUDES = -I$(VARNISHSRC)/include \
           -I$(VARNISHSRC)/bin/varnishd \
           -I$(VARNISHSRC)

vmoddir = $(VMODDIR)
vmodtool = $(VARNISHSRC)/lib/libvcl/vmodtool.py

vmod_LTLIBRARIES = libvmod_fsdirector.la

libvmod_fsdirector_la_LDFLAGS = -module -export-dynamic -avoid-version

libvmod_fsdirector_la_SOURCES = \
	vcc_if.c \
	vcc_if.h \
	vmod_fsdirector.c

vcc_if.c vcc_if.h: vmod_fsdirector.vcc
	@PYTHON@ $(vmodtool) vmod_fsdirector.vcc

VMOD_TESTS = tests/*.vtc
.PHONY: $(VMOD_TESTS)

tests/*.vtc:
	$(VARNISHSRC)/bin/varnishtest/varnishtest -Dvarnishd=$(VARNISHSRC)/bin/varnishd/varnishd -Dvmod_topbuild=$(abs_top_builddir) $@

check: $(VMOD_TESTS)

EXTRA_DIST = \
	vmod_fsdirector.vcc \
	$(VMOD_TESTS)

CLEANFILES = $(builddir)/vcc_if.c $(builddir)/vcc_if.h

