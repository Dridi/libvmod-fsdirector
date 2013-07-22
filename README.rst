.. libvmod-fsdirector - FileSystem module for Varnish 4

   Copyright (C) 2013, Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
   All rights reserved.

   Redistribution  and use in source and binary forms, with or without
   modification,  are permitted provided that the following conditions
   are met:

   1. Redistributions   of  source   code   must   retain  the   above
      copyright  notice, this  list of  conditions  and the  following
      disclaimer.
   2. Redistributions   in  binary  form  must  reproduce  the   above
      copyright  notice, this  list of  conditions and  the  following
      disclaimer   in  the   documentation   and/or  other   materials
      provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT  NOT
   LIMITED  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND  FITNESS
   FOR  A  PARTICULAR  PURPOSE ARE DISCLAIMED. IN NO EVENT  SHALL  THE
   COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
   INCIDENTAL,    SPECIAL,   EXEMPLARY,   OR   CONSEQUENTIAL   DAMAGES
   (INCLUDING,  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
   SERVICES;  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
   HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
   STRICT  LIABILITY,  OR  TORT (INCLUDING  NEGLIGENCE  OR  OTHERWISE)
   ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
   OF THE POSSIBILITY OF SUCH DAMAGE.

===============
vmod_fsdirector
===============

--------------------------
Varnish QueryString Module
--------------------------

:Author: Dridi Boukelmoune
:Date: 2013-04-25
:Version: 0.1
:Manual section: 3

SYNOPSIS
========

.. sourcecode::

   import fsdirector;

DESCRIPTION
===========

With this module, you can make Varnish serve static files from your file system
without an additional server such as Apache HTTPD. The module actually starts a
server thread that will answer Varnish's HTTP requests with static files.

This is a *play with Linux system calls and Varnish internals* Proof of Concept
VMOD. Do I need to mention this is not suitable for production use ? You are
warned ;)

DEPENDENCIES
============

In order to build *and* run the VMOD, you will need:

* Varnish 4 (just in case ;-)
* libmagic

Do not hesitate to report any missing dependency.

HISTORY
=======

This VMOD was initially an attempt to create a *filesystem* director, in order
to test backends manipulation (more precisely directors) introduced by Varnish
4.

Obviously, it failed, but instead of creating a director, I use a backend
declared in the VCL and create a server thread with the backend's parameters.
By the way, I try to reuse Varnish's code instead of writing my own for several
reasons:

* developer => lazy
* it forces me to read and understand lots of code written by experienced C
  developers

There is a strong probability that the director attempt failed because I didn't
use Varnish's internals properly. You can read the commit log to know the steps
I go through while writing it, I'm still playing with it when I want to try
stuff.

EXAMPLE
=======

This is very easy to use::

   import fsdirector;

   backend static {
      .host = "127.0.0.1";
      .port = "8080";
   }
   
   sub vcl_init {
      new fs = fsdirector.file_system(static, "/var/www");
   }

See *src/tests* for more examples.

COPYRIGHT
=========

This document is licensed under the same license as the
libvmod-fsdirector project. See LICENSE for details.

* Copyright (c) 2013 Dridi Boukelmoune

