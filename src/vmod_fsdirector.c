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
// XXX autoheader that
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h> 

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

static void
prepare_answer(struct http_conn *htc, int status)
{
	char *message;

	switch (status) {
		case 200: message = "OK";                 break;
		case 204: message = "No Content";         break;
		case 400: message = "Bad Request";        break;
		case 403: message = "Forbidden";          break;
		case 404: message = "Not Found";          break;
		case 405: message = "Method Not Allowed"; break;
		default:
			status = 500;
			message = "Internal Error";
	}

	dprintf(htc->fd, "HTTP/1.1 %d %s\r\n", status, message);
	dprintf(htc->fd, "Connection: close\r\n");
}

static void
prepare_body(struct http_conn *htc, const char *content_type)
{
	if (content_type) {
		dprintf(htc->fd, "Content-Type: %s\r\n", content_type);
	}
	dprintf(htc->fd, "\r\n");
}

static void
handle_stat_error(struct http_conn *htc, int err) {
	int status;

	switch (err) {
		case EACCES:
			status = 403;
			break;
		case ENAMETOOLONG:
		case EFAULT:
			status = 400;
			break;
		case ENOENT:
		case ENOTDIR:
			status = 404;
			break;
		default:
			status = 500;
	}

	prepare_answer(htc, status);
	prepare_body(htc, NULL);
}

static void
send_response(struct http_conn *htc, struct stat *stat_buf, const char *path)
{
	int fd;
	off_t offset = 0;
	ssize_t remaining = stat_buf->st_size;
	ssize_t written;

	fd = open(path, O_RDONLY);

	// TODO handle failure
	assert(fd > 0);

	prepare_answer(htc, 200);
	dprintf(htc->fd, "Content-Length: %lu\r\n", stat_buf->st_size);
	prepare_body(htc, NULL); // XXX handle content type ?

	while (remaining > 0) {
		written = sendfile(htc->fd, fd, &offset, remaining);
		if (written < 0) {
			perror("sendfile");
			break;
		}
		remaining -= written;
	}

	// XXX too late for a 500 response...
	close(fd);
}

static void
answer_file(struct http_conn *htc, struct stat *stat_buf, const char *path)
{
	mode_t mode = stat_buf->st_mode;

	if (S_ISREG(mode)) {
		if (stat_buf->st_size) {
			send_response(htc, stat_buf, path);
		}
		else {
			prepare_answer(htc, 204);
			prepare_body(htc, NULL);
		}
	}
	else if (S_ISLNK(mode)) {
		// TODO follow link or send redirection ?
	}
	else {
		prepare_answer(htc, 404);
		prepare_body(htc, NULL);
	}
}

static void
answer_appropriate(struct http_conn *htc)
{
	unsigned available;
	char *url;
	char *url_start;
	char *url_end;
	size_t url_len;
	char *path;
	struct stat stat_buf;

	if (strncmp("GET ", htc->ws->s, 4)) {
		prepare_answer(htc, 405);
		prepare_body(htc, NULL);
		return;
	}

	url_start = &htc->ws->s[4];
	url_end = strchr(url_start, ' ');
	url_len = url_end - url_start;
	url = WS_Alloc(htc->ws, url_len + 1);
	memcpy(url, url_start, url_len + 1);
	url[url_len] = '\0';

	path = url;
	if (stat(path, &stat_buf) < 0) {
		handle_stat_error(htc, errno);
		return;
	}

	answer_file(htc, &stat_buf, path);
}

static void *
server_bgthread(struct worker *wrk, void *priv)
{
	struct vmod_fsdirector_file_system *fs;
	struct sockaddr_storage addr_s;
	struct http_conn htc;
	socklen_t len;
	int fd;
	enum htc_status_e htc_status;

	CAST_OBJ_NOTNULL(fs, priv, VMOD_FSDIRECTOR_MAGIC);
	assert(fs->sock >= 0);

	fs->wrk = wrk; // XXX hardcoded size for malloc
	WS_Init(wrk->aws, "fsworkspace-", malloc(4096*1024), 4096*1024);

	while (1) {
		do {
			fd = accept(fs->sock, (void*)&addr_s, &len);
		} while (fd < 0 && errno == EAGAIN);

		if (fd < 0) {
			continue;
		}

		HTTP1_Init(&htc, wrk->aws, fd, NULL, 2048*1024, 20); // XXX hardcoded

		htc_status = HTTP1_Rx(&htc);
		switch (htc_status) {
			case HTTP1_OVERFLOW:
			case HTTP1_ERROR_EOF:
			case HTTP1_ALL_WHITESPACE:
			case HTTP1_NEED_MORE:
				prepare_answer(&htc, 400);
				prepare_body(&htc, NULL);
				break;
			case HTTP1_COMPLETE:
				answer_appropriate(&htc);
				break;
		}

		WS_Reset(wrk->aws, NULL);
		close(fd);
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

	free(fs->wrk->aws);
	FREE_OBJ(fs->wrk);
	FREE_OBJ(fs);
}

