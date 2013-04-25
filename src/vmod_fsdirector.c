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
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include "vre.h"
#include "vrt.h"
#include "vas.h"
#include "vbm.h"

#include "cache/cache.h"
#include "cache/cache_backend.h"

#include "vcc_if.h"

struct vmod_fsdirector_file_system {
	unsigned                 magic;
#define VMOD_FSDIRECTOR_MAGIC    0x00000000
	pthread_mutex_t          mtx;
	VCL_BACKEND              *backend;
	struct director          *dir;
	struct vbitmap           *vbm;
	/* TODO store pthread */
};

VCL_VOID
vmod_file_system__init(struct req *req, struct vmod_fsdirector_file_system **fsp,
    const char *vcl_name)
{
	struct vmod_fsdirector_file_system *fs;

	AZ(req);
	AN(fsp);
	AN(vcl_name);
	AZ(*fsp);
	ALLOC_OBJ(fs, VMOD_FSDIRECTOR_MAGIC);
	AN(fs);
	*fsp = fs;

	/* TODO create pthread */

	AZ(pthread_mutex_init(&fs->mtx, NULL));
	ALLOC_OBJ(fs->dir, DIRECTOR_MAGIC);
	AN(fs->dir);
	REPLACE(fs->dir->vcl_name, vcl_name);
	// fs->dir->priv = priv;
	// fs->dir->healthy = healthy;
	// fs->dir->getfd = getfd;
	fs->vbm = vbit_init(8);
	AN(fs->vbm);
}

VCL_VOID
vmod_file_system__fini(struct req *req, struct vmod_fsdirector_file_system **fsp)
{
	struct vmod_fsdirector_file_system *fs;

	AZ(req);
	fs = *fsp;
	*fsp = NULL;
	CHECK_OBJ_NOTNULL(fs, VMOD_FSDIRECTOR_MAGIC);

	free(fs->backend);
	AZ(pthread_mutex_destroy(&fs->mtx));
	FREE_OBJ(fs->dir);
	vbit_destroy(fs->vbm);

	/* TODO kill pthread */

	FREE_OBJ(fs);
}

VCL_BACKEND
vmod_file_system_backend(struct req *req, struct vmod_fsdirector_file_system *fs)
{
	CHECK_OBJ_NOTNULL(fs, VMOD_FSDIRECTOR_MAGIC);
	return (fs->dir);
}

