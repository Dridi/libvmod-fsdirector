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
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h> 

#include <magic.h>

#include "vre.h"
#include "vrt.h"
#include "vas.h"
#include "vss.h"
#include "vtcp.h"

#include "cache/cache.h"
#include "cache/cache_backend.h"

#include "vcc_if.h"

/*--------------------------------------------------------------------
 * Stolen from bin/varnishd/cache/cache_backend.c
 */

struct vdi_simple {
        unsigned                magic;
#define VDI_SIMPLE_MAGIC        0x476d25b7
        struct director         dir;
        struct backend          *backend;
        const struct vrt_backend *vrt;
};

/*--------------------------------------------------------------------*/

#define WS_LEN        0x400000 // 4 MiB
#define HTTP1_BUF     0x200000 // 2 MiB
#define HTTP1_MAX_HDR 20

struct vmod_fsdirector_file_system {
        unsigned                 magic;
#define VMOD_FSDIRECTOR_MAGIC    0x94874A52
        const char               *root;
        struct vdi_simple        *vs;
        int                      sock;
        struct vss_addr          **vss_addr;
        char                     port[6];
        char                     sockaddr_size;
        struct sockaddr_in       sockaddr;
        pthread_t                tp;
        struct worker            *wrk;
        struct http_conn         htc;
        magic_t                  magic_cookie;
        char                     *thread_name;
        char                     *ws_name;
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
prepare_body(struct http_conn *htc)
{
	dprintf(htc->fd, "\r\n");
}

static void
handle_file_error(struct http_conn *htc, int err) {
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
	prepare_body(htc);
}

static void
add_content_type(struct vmod_fsdirector_file_system *fs, const char *path)
{
	const char *mime = magic_file(fs->magic_cookie, path);

	// XXX how to free a string from magic_file or magic_error ?
	if (mime == NULL) {
		perror(magic_error(fs->magic_cookie));
		return;
	}

	dprintf(fs->htc.fd, "Content-Type: %s\r\n", mime);
}

static void
send_response(struct vmod_fsdirector_file_system *fs, struct stat *stat_buf,
    const char *path)
{
	int fd;
	off_t offset = 0;
	ssize_t remaining = stat_buf->st_size;
	ssize_t written;

	fd = open(path, O_RDONLY);

	if(fd < 0) {
		handle_file_error(&fs->htc, errno);
		return;
	}

	prepare_answer(&fs->htc, 200);
	dprintf(fs->htc.fd, "Content-Length: %lu\r\n", stat_buf->st_size);
	add_content_type(fs, path);
	prepare_body(&fs->htc);

	while (remaining > 0) {
		written = sendfile(fs->htc.fd, fd, &offset, remaining);
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
answer_file(struct vmod_fsdirector_file_system *fs, struct stat *stat_buf,
    const char *path)
{
	mode_t mode = stat_buf->st_mode;

	if (S_ISREG(mode)) {
		if (stat_buf->st_size) {
			send_response(fs, stat_buf, path);
		}
		else {
			prepare_answer(&fs->htc, 204);
			prepare_body(&fs->htc);
		}
	}
	else if (S_ISLNK(mode)) {
		// TODO follow link or send redirection ?
	}
	else {
		prepare_answer(&fs->htc, 404);
		prepare_body(&fs->htc);
	}
}

static void
answer_appropriate(struct vmod_fsdirector_file_system *fs)
{
	unsigned available;
	char *url;
	char *url_start;
	char *url_end;
	size_t url_len;
	size_t root_len;
	char *path;
	struct stat stat_buf;

	if (strncmp("GET ", fs->htc.ws->s, 4)) {
		prepare_answer(&fs->htc, 405);
		prepare_body(&fs->htc);
		return;
	}

	url_start = &fs->htc.ws->s[4];
	url_end = strchr(url_start, ' ');
	url_len = url_end - url_start;
	root_len = strlen(fs->root);
	url = WS_Alloc(fs->htc.ws, root_len + url_len + 1);
	memcpy(url, fs->root, root_len);
	memcpy(&url[root_len], url_start, url_len + 1);
	url[root_len + url_len] = '\0';

	path = url;
	if (stat(path, &stat_buf) < 0) {
		handle_file_error(&fs->htc, errno);
		return;
	}

	answer_file(fs, &stat_buf, path);
}

static void *
server_bgthread(struct worker *wrk, void *priv)
{
	struct vmod_fsdirector_file_system *fs;
	struct sockaddr_storage addr_s;
	socklen_t len;
	struct http_conn *htc;
	int fd;
	enum htc_status_e htc_status;

	CAST_OBJ_NOTNULL(fs, priv, VMOD_FSDIRECTOR_MAGIC);
	assert(fs->sock >= 0);

	htc = &fs->htc;
	fs->wrk = wrk;
	WS_Init(wrk->aws, fs->ws_name, malloc(WS_LEN), WS_LEN);

	while (1) {
		do {
			fd = accept(fs->sock, (void*)&addr_s, &len);
		} while (fd < 0 && errno == EAGAIN);

		if (fd < 0) {
			continue;
		}

		HTTP1_Init(htc, wrk->aws, fd, NULL, HTTP1_BUF, HTTP1_MAX_HDR);

		htc_status = HTTP1_Rx(htc);
		switch (htc_status) {
			case HTTP1_OVERFLOW:
			case HTTP1_ERROR_EOF:
			case HTTP1_ALL_WHITESPACE:
			case HTTP1_NEED_MORE:
				prepare_answer(htc, 400);
				prepare_body(htc);
				break;
			case HTTP1_COMPLETE:
				answer_appropriate(fs);
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

	WRK_BgThread(&fs->tp, fs->thread_name, server_bgthread, fs);
}

static magic_t
load_magic_cookie()
{
	magic_t magic_cookie = magic_open(MAGIC_NONE|MAGIC_MIME);

	AN(magic_cookie);

	if (magic_load(magic_cookie, NULL) < 0) {
		perror("magic_load");
		magic_close(magic_cookie);
		magic_cookie = NULL;
	}

	return magic_cookie;
}

VCL_VOID
vmod_file_system__init(const struct vrt_ctx *ctx,
    struct vmod_fsdirector_file_system **fsp,
    const char *vcl_name, VCL_BACKEND be, const char *root)
{
	struct vmod_fsdirector_file_system *fs;
	struct vdi_simple *vs;

	AN(ctx);
	AN(fsp);
	AN(vcl_name);
	AZ(*fsp);

	CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
	CAST_OBJ_NOTNULL(vs, be->priv, VDI_SIMPLE_MAGIC);

	ALLOC_OBJ(fs, VMOD_FSDIRECTOR_MAGIC);
	AN(fs);
	*fsp = fs;
	fs->vs = vs;

	fs->thread_name = malloc(sizeof("fsthread-")    + strlen(vcl_name));
	fs->ws_name     = malloc(sizeof("fsworkspace-") + strlen(vcl_name));

	AN(fs->thread_name);
	AN(fs->ws_name);

	sprintf(fs->thread_name, "fsthread-%s", vcl_name);
	sprintf(fs->ws_name,  "fsworkspace-%s", vcl_name);

	AN(root);
	assert(root[0] == '\0' || root[0] == '/');
	fs->root = root;

	fs->magic_cookie = load_magic_cookie();
	AN(fs->magic_cookie);

	server_start(fs);
}

VCL_VOID
vmod_file_system__fini(struct vmod_fsdirector_file_system **fsp)
{
	struct vmod_fsdirector_file_system *fs;
	void *res;

	// XXX It seems that the destructor is not called yet.
	//     A little reminder then...
	abort();

	fs = *fsp;
	*fsp = NULL;
	CHECK_OBJ_NOTNULL(fs, VMOD_FSDIRECTOR_MAGIC);

	AZ(pthread_cancel(fs->tp));
	AZ(pthread_join(fs->tp, &res));
	assert(res == PTHREAD_CANCELED);

	magic_close(fs->magic_cookie);
	free(fs->thread_name);
	free(fs->ws_name);
	free(fs->wrk->aws);
	FREE_OBJ(fs->wrk);
	FREE_OBJ(fs);
}

