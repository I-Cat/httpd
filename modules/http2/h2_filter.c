/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
 
#include <assert.h>

#include <apr_strings.h>
#include <httpd.h>
#include <http_core.h>
#include <http_protocol.h>
#include <http_log.h>
#include <http_connection.h>
#include <scoreboard.h>

#include "h2_private.h"
#include "h2.h"
#include "h2_config.h"
#include "h2_conn_io.h"
#include "h2_ctx.h"
#include "h2_mplx.h"
#include "h2_push.h"
#include "h2_task.h"
#include "h2_stream.h"
#include "h2_request.h"
#include "h2_headers.h"
#include "h2_stream.h"
#include "h2_session.h"
#include "h2_util.h"
#include "h2_version.h"

#include "h2_filter.h"

#define UNSET       -1
#define H2MIN(x,y) ((x) < (y) ? (x) : (y))

static apr_status_t recv_RAW_DATA(conn_rec *c, h2_filter_cin *cin, 
                                  apr_bucket *b, apr_read_type_e block)
{
    h2_session *session = cin->session;
    apr_status_t status = APR_SUCCESS;
    apr_size_t len;
    const char *data;
    ssize_t n;
    
    (void)c;
    status = apr_bucket_read(b, &data, &len, block);
    
    while (status == APR_SUCCESS && len > 0) {
        n = nghttp2_session_mem_recv(session->ngh2, (const uint8_t *)data, len);
        
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, session->c,
                      H2_SSSN_MSG(session, "fed %ld bytes to nghttp2, %ld read"),
                      (long)len, (long)n);
        if (n < 0) {
            if (nghttp2_is_fatal((int)n)) {
                h2_session_event(session, H2_SESSION_EV_PROTO_ERROR, 
                                 (int)n, nghttp2_strerror((int)n));
                status = APR_EGENERAL;
            }
        }
        else {
            session->io.bytes_read += n;
            if ((apr_ssize_t)len <= n) {
                break;
            }
            len -= (apr_size_t)n;
            data += n;
        }
    }
    
    return status;
}

static apr_status_t recv_RAW_brigade(conn_rec *c, h2_filter_cin *cin, 
                                     apr_bucket_brigade *bb, 
                                     apr_read_type_e block)
{
    apr_status_t status = APR_SUCCESS;
    apr_bucket* b;
    int consumed = 0;
    
    h2_util_bb_log(c, c->id, APLOG_TRACE2, "RAW_in", bb);
    while (status == APR_SUCCESS && !APR_BRIGADE_EMPTY(bb)) {
        b = APR_BRIGADE_FIRST(bb);

        if (APR_BUCKET_IS_METADATA(b)) {
            /* nop */
        }
        else {
            status = recv_RAW_DATA(c, cin, b, block);
        }
        consumed = 1;
        apr_bucket_delete(b);
    }
    
    if (!consumed && status == APR_SUCCESS && block == APR_NONBLOCK_READ) {
        return APR_EAGAIN;
    }
    return status;
}

h2_filter_cin *h2_filter_cin_create(h2_session *session)
{
    h2_filter_cin *cin;
    
    cin = apr_pcalloc(session->pool, sizeof(*cin));
    if (!cin) {
        return NULL;
    }
    cin->session = session;
    return cin;
}

void h2_filter_cin_timeout_set(h2_filter_cin *cin, apr_interval_time_t timeout)
{
    cin->timeout = timeout;
}

apr_status_t h2_filter_core_input(ap_filter_t* f,
                                  apr_bucket_brigade* brigade,
                                  ap_input_mode_t mode,
                                  apr_read_type_e block,
                                  apr_off_t readbytes) 
{
    h2_filter_cin *cin = f->ctx;
    apr_status_t status = APR_SUCCESS;
    apr_interval_time_t saved_timeout = UNSET;
    const int trace1 = APLOGctrace1(f->c);
    
    if (trace1) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, f->c,
                      "h2_session(%ld): read, %s, mode=%d, readbytes=%ld", 
                      (long)f->c->id, (block == APR_BLOCK_READ)? 
                      "BLOCK_READ" : "NONBLOCK_READ", mode, (long)readbytes);
    }
    
    if (mode == AP_MODE_INIT || mode == AP_MODE_SPECULATIVE) {
        return ap_get_brigade(f->next, brigade, mode, block, readbytes);
    }
    
    if (mode != AP_MODE_READBYTES) {
        return (block == APR_BLOCK_READ)? APR_SUCCESS : APR_EAGAIN;
    }
    
    if (!cin->bb) {
        cin->bb = apr_brigade_create(cin->session->pool, f->c->bucket_alloc);
    }

    if (!cin->socket) {
        cin->socket = ap_get_conn_socket(f->c);
    }
    
    if (APR_BRIGADE_EMPTY(cin->bb)) {
        /* We only do a blocking read when we have no streams to process. So,
         * in httpd scoreboard lingo, we are in a KEEPALIVE connection state.
         */
        if (block == APR_BLOCK_READ) {
            if (cin->timeout > 0) {
                apr_socket_timeout_get(cin->socket, &saved_timeout);
                apr_socket_timeout_set(cin->socket, cin->timeout);
            }
        }
        status = ap_get_brigade(f->next, cin->bb, AP_MODE_READBYTES,
                                block, readbytes);
        if (saved_timeout != UNSET) {
            apr_socket_timeout_set(cin->socket, saved_timeout);
        }
    }
    
    switch (status) {
        case APR_SUCCESS:
            status = recv_RAW_brigade(f->c, cin, cin->bb, block);
            break;
        case APR_EOF:
        case APR_EAGAIN:
        case APR_TIMEUP:
            if (trace1) {
                ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, f->c,
                              "h2_session(%ld): read", f->c->id);
            }
            break;
        default:
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, f->c, APLOGNO(03046)
                          "h2_session(%ld): error reading", f->c->id);
            break;
    }
    return status;
}

/*******************************************************************************
 * http2 connection status handler + stream out source
 ******************************************************************************/

typedef struct {
    apr_bucket_refcount refcount;
    h2_bucket_event_cb *cb;
    void *ctx;
} h2_bucket_observer;
 
static apr_status_t bucket_read(apr_bucket *b, const char **str,
                                apr_size_t *len, apr_read_type_e block)
{
    (void)b;
    (void)block;
    *str = NULL;
    *len = 0;
    return APR_SUCCESS;
}

static void bucket_destroy(void *data)
{
    h2_bucket_observer *h = data;
    if (apr_bucket_shared_destroy(h)) {
        if (h->cb) {
            h->cb(h->ctx, H2_BUCKET_EV_BEFORE_DESTROY, NULL);
        }
        apr_bucket_free(h);
    }
}

apr_bucket * h2_bucket_observer_make(apr_bucket *b, h2_bucket_event_cb *cb,
                                 void *ctx)
{
    h2_bucket_observer *br;

    br = apr_bucket_alloc(sizeof(*br), b->list);
    br->cb = cb;
    br->ctx = ctx;

    b = apr_bucket_shared_make(b, br, 0, 0);
    b->type = &h2_bucket_type_observer;
    return b;
} 

apr_bucket * h2_bucket_observer_create(apr_bucket_alloc_t *list, 
                                       h2_bucket_event_cb *cb, void *ctx)
{
    apr_bucket *b = apr_bucket_alloc(sizeof(*b), list);

    APR_BUCKET_INIT(b);
    b->free = apr_bucket_free;
    b->list = list;
    b = h2_bucket_observer_make(b, cb, ctx);
    return b;
}
                                       
apr_status_t h2_bucket_observer_fire(apr_bucket *b, h2_bucket_event event)
{
    if (H2_BUCKET_IS_OBSERVER(b)) {
        h2_bucket_observer *l = (h2_bucket_observer *)b->data; 
        return l->cb(l->ctx, event, b);
    }
    return APR_EINVAL;
}

const apr_bucket_type_t h2_bucket_type_observer = {
    "H2OBS", 5, APR_BUCKET_METADATA,
    bucket_destroy,
    bucket_read,
    apr_bucket_setaside_noop,
    apr_bucket_split_notimpl,
    apr_bucket_shared_copy
};

apr_bucket *h2_bucket_observer_beam(struct h2_bucket_beam *beam,
                                    apr_bucket_brigade *dest,
                                    const apr_bucket *src)
{
    (void)beam;
    if (H2_BUCKET_IS_OBSERVER(src)) {
        h2_bucket_observer *l = (h2_bucket_observer *)src->data; 
        apr_bucket *b = h2_bucket_observer_create(dest->bucket_alloc, 
                                                  l->cb, l->ctx);
        APR_BRIGADE_INSERT_TAIL(dest, b);
        l->cb = NULL;
        l->ctx = NULL;
        h2_bucket_observer_fire(b, H2_BUCKET_EV_BEFORE_MASTER_SEND);
        return b;
    }
    return NULL;
}

static apr_status_t bbout(apr_bucket_brigade *bb, const char *fmt, ...)
                             __attribute__((format(printf,2,3)));
static apr_status_t bbout(apr_bucket_brigade *bb, const char *fmt, ...)
{
    va_list args;
    apr_status_t rv;

    va_start(args, fmt);
    rv = apr_brigade_vprintf(bb, NULL, NULL, fmt, args);
    va_end(args);

    return rv;
}

static void add_settings(apr_bucket_brigade *bb, h2_session *s, int last) 
{
    h2_mplx *m = s->mplx;
    
    bbout(bb, "  \"settings\": {\n");
    bbout(bb, "    \"SETTINGS_MAX_CONCURRENT_STREAMS\": %d,\n", m->max_streams); 
    bbout(bb, "    \"SETTINGS_MAX_FRAME_SIZE\": %d,\n", 16*1024); 
    bbout(bb, "    \"SETTINGS_INITIAL_WINDOW_SIZE\": %d,\n", h2_config_sgeti(s->s, H2_CONF_WIN_SIZE));
    bbout(bb, "    \"SETTINGS_ENABLE_PUSH\": %d\n", h2_session_push_enabled(s)); 
    bbout(bb, "  }%s\n", last? "" : ",");
}

static void add_peer_settings(apr_bucket_brigade *bb, h2_session *s, int last) 
{
    bbout(bb, "  \"peerSettings\": {\n");
    bbout(bb, "    \"SETTINGS_MAX_CONCURRENT_STREAMS\": %d,\n", 
        nghttp2_session_get_remote_settings(s->ngh2, NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)); 
    bbout(bb, "    \"SETTINGS_MAX_FRAME_SIZE\": %d,\n", 
        nghttp2_session_get_remote_settings(s->ngh2, NGHTTP2_SETTINGS_MAX_FRAME_SIZE)); 
    bbout(bb, "    \"SETTINGS_INITIAL_WINDOW_SIZE\": %d,\n", 
        nghttp2_session_get_remote_settings(s->ngh2, NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE)); 
    bbout(bb, "    \"SETTINGS_ENABLE_PUSH\": %d,\n", 
        nghttp2_session_get_remote_settings(s->ngh2, NGHTTP2_SETTINGS_ENABLE_PUSH)); 
    bbout(bb, "    \"SETTINGS_HEADER_TABLE_SIZE\": %d,\n", 
        nghttp2_session_get_remote_settings(s->ngh2, NGHTTP2_SETTINGS_HEADER_TABLE_SIZE)); 
    bbout(bb, "    \"SETTINGS_MAX_HEADER_LIST_SIZE\": %d\n", 
        nghttp2_session_get_remote_settings(s->ngh2, NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE)); 
    bbout(bb, "  }%s\n", last? "" : ",");
}

typedef struct {
    apr_bucket_brigade *bb;
    h2_session *s;
    int idx;
} stream_ctx_t;

static int add_stream(h2_stream *stream, void *ctx)
{
    stream_ctx_t *x = ctx;
    int32_t flowIn, flowOut;
    
    flowIn = nghttp2_session_get_stream_effective_local_window_size(x->s->ngh2, stream->id); 
    flowOut = nghttp2_session_get_stream_remote_window_size(x->s->ngh2, stream->id);
    bbout(x->bb, "%s\n    \"%d\": {\n", (x->idx? "," : ""), stream->id);
    bbout(x->bb, "    \"state\": \"%s\",\n", h2_stream_state_str(stream));
    bbout(x->bb, "    \"created\": %f,\n", ((double)stream->created)/APR_USEC_PER_SEC);
    bbout(x->bb, "    \"flowIn\": %d,\n", flowIn);
    bbout(x->bb, "    \"flowOut\": %d,\n", flowOut);
    bbout(x->bb, "    \"dataIn\": %"APR_OFF_T_FMT",\n", stream->in_data_octets);  
    bbout(x->bb, "    \"dataOut\": %"APR_OFF_T_FMT"\n", stream->out_data_octets);  
    bbout(x->bb, "    }");
    
    ++x->idx;
    return 1;
} 

static void add_streams(apr_bucket_brigade *bb, h2_session *s, int last) 
{
    stream_ctx_t x;
    
    x.bb = bb;
    x.s = s;
    x.idx = 0;
    bbout(bb, "  \"streams\": {");
    h2_mplx_m_stream_do(s->mplx, add_stream, &x);
    bbout(bb, "\n  }%s\n", last? "" : ",");
}

static void add_push(apr_bucket_brigade *bb, h2_session *s, 
                     h2_stream *stream, int last) 
{
    h2_push_diary *diary;
    apr_status_t status;
    
    bbout(bb, "    \"push\": {\n");
    diary = s->push_diary;
    if (diary) {
        const char *data;
        const char *base64_digest;
        apr_size_t len;
        
        status = h2_push_diary_digest_get(diary, bb->p, 256, 
                                          stream->request->authority, 
                                          &data, &len);
        if (status == APR_SUCCESS) {
            base64_digest = h2_util_base64url_encode(data, len, bb->p);
            bbout(bb, "      \"cacheDigest\": \"%s\",\n", base64_digest);
        }
    }
    bbout(bb, "      \"promises\": %d,\n", s->pushes_promised);
    bbout(bb, "      \"submits\": %d,\n", s->pushes_submitted);
    bbout(bb, "      \"resets\": %d\n", s->pushes_reset);
    bbout(bb, "    }%s\n", last? "" : ",");
}

static void add_in(apr_bucket_brigade *bb, h2_session *s, int last) 
{
    bbout(bb, "    \"in\": {\n");
    bbout(bb, "      \"requests\": %d,\n", s->remote.emitted_count);
    bbout(bb, "      \"resets\": %d, \n", s->streams_reset);
    bbout(bb, "      \"frames\": %ld,\n", (long)s->frames_received);
    bbout(bb, "      \"octets\": %"APR_UINT64_T_FMT"\n", s->io.bytes_read);
    bbout(bb, "    }%s\n", last? "" : ",");
}

static void add_out(apr_bucket_brigade *bb, h2_session *s, int last) 
{
    bbout(bb, "    \"out\": {\n");
    bbout(bb, "      \"responses\": %d,\n", s->responses_submitted);
    bbout(bb, "      \"frames\": %ld,\n", (long)s->frames_sent);
    bbout(bb, "      \"octets\": %"APR_UINT64_T_FMT"\n", s->io.bytes_written);
    bbout(bb, "    }%s\n", last? "" : ",");
}

static void add_stats(apr_bucket_brigade *bb, h2_session *s, 
                     h2_stream *stream, int last) 
{
    bbout(bb, "  \"stats\": {\n");
    add_in(bb, s, 0);
    add_out(bb, s, 0);
    add_push(bb, s, stream, 1);
    bbout(bb, "  }%s\n", last? "" : ",");
}

static apr_status_t h2_status_insert(h2_task *task, apr_bucket *b)
{
    h2_mplx *m = task->mplx;
    h2_stream *stream = h2_mplx_t_stream_get(m, task);
    h2_session *s;
    conn_rec *c;
    
    apr_bucket_brigade *bb;
    apr_bucket *e;
    int32_t connFlowIn, connFlowOut;
    
    if (!stream) {
        /* stream already done */
        return APR_SUCCESS;
    }
    s = stream->session;
    c = s->c;
    
    bb = apr_brigade_create(stream->pool, c->bucket_alloc);
    
    connFlowIn = nghttp2_session_get_effective_local_window_size(s->ngh2); 
    connFlowOut = nghttp2_session_get_remote_window_size(s->ngh2);
     
    bbout(bb, "{\n");
    bbout(bb, "  \"version\": \"draft-01\",\n");
    add_settings(bb, s, 0);
    add_peer_settings(bb, s, 0);
    bbout(bb, "  \"connFlowIn\": %d,\n", connFlowIn);
    bbout(bb, "  \"connFlowOut\": %d,\n", connFlowOut);
    bbout(bb, "  \"sentGoAway\": %d,\n", s->local.shutdown);

    add_streams(bb, s, 0);
    
    add_stats(bb, s, stream, 1);
    bbout(bb, "}\n");
    
    while ((e = APR_BRIGADE_FIRST(bb)) != APR_BRIGADE_SENTINEL(bb)) {
        APR_BUCKET_REMOVE(e);
        APR_BUCKET_INSERT_AFTER(b, e);
        b = e;
    }
    apr_brigade_destroy(bb);
    
    return APR_SUCCESS;
}

static apr_status_t status_event(void *ctx, h2_bucket_event event, 
                                 apr_bucket *b)
{
    h2_task *task = ctx;
    
    ap_log_cerror(APLOG_MARK, APLOG_TRACE3, 0, task->c->master, 
                  "status_event(%s): %d", task->id, event);
    switch (event) {
        case H2_BUCKET_EV_BEFORE_MASTER_SEND:
            h2_status_insert(task, b);
            break;
        default:
            break;
    }
    return APR_SUCCESS;
}

static apr_status_t discard_body(request_rec *r, apr_off_t maxlen)
{
    apr_bucket_brigade *bb;
    int seen_eos;
    apr_status_t rv;

    bb = apr_brigade_create(r->pool, r->connection->bucket_alloc);
    seen_eos = 0;
    do {
        apr_bucket *bucket;

        rv = ap_get_brigade(r->input_filters, bb, AP_MODE_READBYTES,
                            APR_BLOCK_READ, HUGE_STRING_LEN);

        if (rv != APR_SUCCESS) {
            apr_brigade_destroy(bb);
            return rv;
        }

        for (bucket = APR_BRIGADE_FIRST(bb);
             bucket != APR_BRIGADE_SENTINEL(bb);
             bucket = APR_BUCKET_NEXT(bucket))
        {
            const char *data;
            apr_size_t len;

            if (APR_BUCKET_IS_EOS(bucket)) {
                seen_eos = 1;
                break;
            }
            if (bucket->length == 0) {
                continue;
            }
            rv = apr_bucket_read(bucket, &data, &len, APR_BLOCK_READ);
            if (rv != APR_SUCCESS) {
                apr_brigade_destroy(bb);
                return rv;
            }
            maxlen -= bucket->length;
        }
        apr_brigade_cleanup(bb);
    } while (!seen_eos && maxlen >= 0);

    return APR_SUCCESS;
}

int h2_filter_h2_status_handler(request_rec *r)
{
    conn_rec *c = r->connection;
    h2_task *task;
    apr_bucket_brigade *bb;
    apr_bucket *b;
    apr_status_t status;
    
    if (strcmp(r->handler, "http2-status")) {
        return DECLINED;
    }
    if (r->method_number != M_GET && r->method_number != M_POST) {
        return DECLINED;
    }

    task = h2_ctx_get_task(r->connection);
    if (task) {
        /* In this handler, we do some special sauce to send footers back,
         * IFF we received footers in the request. This is used in our test
         * cases, since CGI has no way of handling those. */
        if ((status = discard_body(r, 1024)) != OK) {
            return status;
        }
        
        /* We need to handle the actual output on the main thread, as
         * we need to access h2_session information. */
        r->status = 200;
        r->clength = -1;
        r->chunked = 1;
        apr_table_unset(r->headers_out, "Content-Length");
        /* Discourage content-encodings */
        apr_table_unset(r->headers_out, "Content-Encoding");
        apr_table_setn(r->subprocess_env, "no-brotli", "1");
        apr_table_setn(r->subprocess_env, "no-gzip", "1");

        ap_set_content_type(r, "application/json");
        apr_table_setn(r->notes, H2_FILTER_DEBUG_NOTE, "on");

        bb = apr_brigade_create(r->pool, c->bucket_alloc);
        b = h2_bucket_observer_create(c->bucket_alloc, status_event, task);
        APR_BRIGADE_INSERT_TAIL(bb, b);
        b = apr_bucket_eos_create(c->bucket_alloc);
        APR_BRIGADE_INSERT_TAIL(bb, b);

        ap_log_rerror(APLOG_MARK, APLOG_TRACE2, 0, r,
                      "status_handler(%s): checking for incoming trailers", 
                      task->id);
        if (r->trailers_in && !apr_is_empty_table(r->trailers_in)) {
            ap_log_rerror(APLOG_MARK, APLOG_TRACE2, 0, r,
                          "status_handler(%s): seeing incoming trailers", 
                          task->id);
            apr_table_setn(r->trailers_out, "h2-trailers-in", 
                           apr_itoa(r->pool, 1));
        }
        
        status = ap_pass_brigade(r->output_filters, bb);
        if (status == APR_SUCCESS
            || r->status != HTTP_OK
            || c->aborted) {
            return OK;
        }
        else {
            /* no way to know what type of error occurred */
            ap_log_rerror(APLOG_MARK, APLOG_TRACE1, status, r,
                          "status_handler(%s): ap_pass_brigade failed", 
                          task->id);
            return AP_FILTER_ERROR;
        }
    }
    return DECLINED;
}

