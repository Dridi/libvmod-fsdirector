/*
 * libvmod-fsdirector - FileSystem module for Varnish 4
 *
 * Copyright (C) 2013, Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
 * All rights reserved.
 *
 * Redistribution  and use in source and binary forms, with or without
 * modification,  are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions   of  source   code   must   retain  the   above
 *    copyright  notice, this  list of  conditions  and the  following
 *    disclaimer.
 * 2. Redistributions   in  binary  form  must  reproduce  the   above
 *    copyright  notice, this  list of  conditions and  the  following
 *    disclaimer   in  the   documentation   and/or  other   materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT  NOT
 * LIMITED  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND  FITNESS
 * FOR  A  PARTICULAR  PURPOSE ARE DISCLAIMED. IN NO EVENT  SHALL  THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL,    SPECIAL,   EXEMPLARY,   OR   CONSEQUENTIAL   DAMAGES
 * (INCLUDING,  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES;  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT  LIABILITY,  OR  TORT (INCLUDING  NEGLIGENCE  OR  OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <netinet/in.h> // XXX autoheader that

#include "vre.h"
#include "vrt.h"
#include "vas.h"
#include "vss.h"
#include "vbm.h"
#include "vtcp.h"

#include "cache/cache.h"
#include "cache/cache_backend.h"

#include "vcc_if.h"

/*--------------------------------------------------------------------
 * Stolen from bin/varnishd/cache/cache_backend.c
 */

static
struct vdi_simple {
        unsigned                magic;
#define VDI_SIMPLE_MAGIC        0x476d25b7
        struct director         dir;
        struct backend          *backend;
        const struct vrt_backend *vrt;
};

/*--------------------------------------------------------------------*/

static bgthread_t server_bgthread;

struct vmod_fsdirector_file_system {
        unsigned                 magic;
#define VMOD_FSDIRECTOR_MAGIC    0x00000000 // TODO pick a magic
        struct vdi_simple        *vs;
        struct vbitmap           *vbm;
        int                      sock;
        struct vss_addr          **vss_addr;
        char                     port[6];
        char                     sockaddr_size;
        struct sockaddr_in       sockaddr;
        pthread_t                tp;
        struct worker            *wrk;
};

static void *
server_bgthread(struct worker *wrk, void *priv)
{
	struct vmod_fsdirector_file_system *fs;
	struct sockaddr_storage addr_s;
	socklen_t len;
	int fd;

	CAST_OBJ_NOTNULL(fs, priv, VMOD_FSDIRECTOR_MAGIC);
	assert(fs->sock >= 0);

	while (1) {
		do {
			fd = accept(fs->sock, (void*)&addr_s, &len);
		} while (fd < 0 && errno == EAGAIN);

	}

	pthread_exit(0);

	NEEDLESS_RETURN(NULL);
}

static void
server_start(struct vmod_fsdirector_file_system *fs)
{
	struct vdi_simple *vs;
	const struct vrt_backend *be;

	vs = fs->vs;
	be = vs->vrt;

	AN(VSS_resolve(be->ipv4_addr, be->port, &fs->vss_addr));
	fs->sock = VSS_listen(fs->vss_addr[0], be->max_connections);
	assert(fs->sock >= 0);

	/* TODO append an id to the thread name */
	WRK_BgThread(&fs->tp, "fsthread-", server_bgthread, fs);
}

VCL_VOID
vmod_file_system__init(struct req *req, struct vmod_fsdirector_file_system **fsp,
    const char *vcl_name, VCL_BACKEND be)
{
	struct vmod_fsdirector_file_system *fs;
	struct vdi_simple *vs;

	AZ(req);
	AN(fsp);
	AN(vcl_name);
	AZ(*fsp);
	CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, be->priv, VDI_SIMPLE_MAGIC);
	ALLOC_OBJ(fs, VMOD_FSDIRECTOR_MAGIC);
	AN(fs);
	*fsp = fs;
	fs->vs = vs;

	fs->vbm = vbit_init(8);
	AN(fs->vbm);

	server_start(fs);
}

VCL_VOID
vmod_file_system__fini(struct req *req, struct vmod_fsdirector_file_system **fsp)
{
	struct vmod_fsdirector_file_system *fs;
	void *res;

	// XXX It seems like the destructor is not called yet.
	//     A little reminder then...
	abort();

	AZ(req);
	fs = *fsp;
	*fsp = NULL;
	CHECK_OBJ_NOTNULL(fs, VMOD_FSDIRECTOR_MAGIC);

	vbit_destroy(fs->vbm);

	AZ(pthread_cancel(fs->tp));
	AZ(pthread_join(fs->tp, &res));
	assert(res == PTHREAD_CANCELED);

	FREE_OBJ(fs);
}

