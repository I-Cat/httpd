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
#include <stddef.h>

#include <apr_atomic.h>
#include <apr_strings.h>

#include <httpd.h>
#include <http_core.h>
#include <http_connection.h>
#include <http_protocol.h>
#include <http_request.h>
#include <http_log.h>
#include <http_vhost.h>
#include <util_filter.h>
#include <ap_mpm.h>
#include <mod_core.h>
#include <scoreboard.h>

#include "h2_private.h"
#include "h2.h"
#include "h2_bucket_beam.h"
#include "h2_conn.h"
#include "h2_config.h"
#include "h2_ctx.h"
#include "h2_from_h1.h"
#include "h2_h2.h"
#include "h2_mplx.h"
#include "h2_request.h"
#include "h2_headers.h"
#include "h2_session.h"
#include "h2_stream.h"
#include "h2_task.h"
#include "h2_util.h"

static void H2_TASK_OUT_LOG(int lvl, h2_task *task, apr_bucket_brigade *bb, 
                            const char *tag)
{
    if (APLOG_C_IS_LEVEL(task->c, lvl)) {
        conn_rec *c = task->c;
        char buffer[4 * 1024];
        const char *line = "(null)";
        apr_size_t len, bmax = sizeof(buffer)/sizeof(buffer[0]);
        
        len = h2_util_bb_print(buffer, bmax, tag, "", bb);
        ap_log_cerror(APLOG_MARK, lvl, 0, c, "bb_dump(%s): %s", 
                      task->id, len? buffer : line);
    }
}

/*******************************************************************************
 * task input handling
 ******************************************************************************/

static int input_ser_header(void *ctx, const char *name, const char *value) 
{
    h2_task *task = ctx;
    apr_brigade_printf(task->input.bb, NULL, NULL, "%s: %s\r\n", name, value);
    return 1;
}

/*******************************************************************************
 * task output handling
 ******************************************************************************/

static apr_status_t open_output(h2_task *task)
{
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, task->c, APLOGNO(03348)
                  "h2_task(%s): open output to %s %s %s",
                  task->id, task->request->method, 
                  task->request->authority, 
                  task->request->path);
    task->output.opened = 1;
    return h2_mplx_t_out_open(task->mplx, task->stream_id, task->output.beam);
}

static void output_consumed(void *ctx, h2_bucket_beam *beam, apr_off_t length)
{
    h2_task *task = ctx;
    if (task && h2_task_logio_add_bytes_out) {
        h2_task_logio_add_bytes_out(task->c, length);
    }
}

static apr_status_t send_out(h2_task *task, apr_bucket_brigade* bb, int block)
{
    apr_off_t written, left;
    apr_status_t status;

    apr_brigade_length(bb, 0, &written);
    H2_TASK_OUT_LOG(APLOG_TRACE2, task, bb, "h2_task send_out");
    h2_beam_log(task->output.beam, task->c, APLOG_TRACE2, "send_out(before)");

    status = h2_beam_send(task->output.beam, bb, 
                          block? APR_BLOCK_READ : APR_NONBLOCK_READ);
    h2_beam_log(task->output.beam, task->c, APLOG_TRACE2, "send_out(after)");
    
    if (APR_STATUS_IS_EAGAIN(status)) {
        apr_brigade_length(bb, 0, &left);
        written -= left;
        status = APR_SUCCESS;
    }
    if (status == APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, task->c, 
                      "h2_task(%s): send_out done", task->id);
    }
    else {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, task->c,
                      "h2_task(%s): send_out (%ld bytes)", 
                      task->id, (long)written);
    }
    return status;
}

/* Bring the data from the brigade (which represents the result of the
 * request_rec out filter chain) into the h2_mplx for further sending
 * on the master connection. 
 */
static apr_status_t secondary_out(h2_task *task, ap_filter_t* f, 
                                  apr_bucket_brigade* bb)
{
    apr_bucket *b;
    apr_status_t rv = APR_SUCCESS;
    int flush = 0, blocking;
    
send:
    /* we send block once we opened the output, so someone is there reading it */
    blocking = task->output.opened;
    for (b = APR_BRIGADE_FIRST(bb);
         b != APR_BRIGADE_SENTINEL(bb);
         b = APR_BUCKET_NEXT(b)) {
        if (APR_BUCKET_IS_FLUSH(b) || APR_BUCKET_IS_EOS(b) || AP_BUCKET_IS_EOR(b)) {
            flush = 1;
            break;
        }
    }
    
    if (task->output.bb && !APR_BRIGADE_EMPTY(task->output.bb)) {
        /* still have data buffered from previous attempt.
         * setaside and append new data and try to pass the complete data */
        if (!APR_BRIGADE_EMPTY(bb)) {
            if (APR_SUCCESS != (rv = ap_save_brigade(f, &task->output.bb, &bb, task->pool))) {
                goto out;
            }
        }
        rv = send_out(task, task->output.bb, blocking);
    }
    else {
        /* no data buffered previously, pass brigade directly */
        rv = send_out(task, bb, blocking);

        if (APR_SUCCESS == rv && !APR_BRIGADE_EMPTY(bb)) {
            /* output refused to buffer it all, time to open? */
            if (!task->output.opened && APR_SUCCESS == (rv = open_output(task))) {
                /* Make another attempt to send the data. With the output open,
                 * the call might be blocking and send all data, so we do not need
                 * to save the brigade */
                goto send;
            }
            else if (blocking && flush) {
                /* Need to keep on doing this. */
                goto send;
            }
            
            if (APR_SUCCESS == rv) {
                /* could not write all, buffer the rest */
                ap_log_cerror(APLOG_MARK, APLOG_DEBUG, rv, task->c, APLOGNO(03405)
                              "h2_secondary_out(%s): saving brigade", task->id);
                ap_assert(NULL);
                rv = ap_save_brigade(f, &task->output.bb, &bb, task->pool);
                flush = 1;
            }
        }
    }
    
    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, task->c,
                  "h2_secondary_out(%s): buffered=%d", task->id, task->output.buffered);
    if (APR_SUCCESS == rv && !task->output.opened && (flush || !task->output.buffered)) {
        /* got a flush or could not write all, time to tell someone to read */
        rv = open_output(task);
    }
out:
    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, rv, task->c, 
                  "h2_secondary_out(%s): secondary_out leave", task->id);    
    return rv;
}

static apr_status_t output_finish(h2_task *task)
{
    if (!task->output.opened) {
        return open_output(task);
    }
    return APR_SUCCESS;
}

/*******************************************************************************
 * task secondary connection filters
 ******************************************************************************/

static apr_status_t h2_filter_secondary_in(ap_filter_t* f,
                                           apr_bucket_brigade* bb,
                                           ap_input_mode_t mode,
                                           apr_read_type_e block,
                                           apr_off_t readbytes)
{
    h2_task *task;
    apr_status_t status = APR_SUCCESS;
    apr_bucket *b, *next;
    apr_off_t bblen;
    const int trace1 = APLOGctrace1(f->c);
    apr_size_t rmax = ((readbytes <= APR_SIZE_MAX)? 
                       (apr_size_t)readbytes : APR_SIZE_MAX);
    
    task = h2_ctx_get_task(f->c);
    ap_assert(task);

    if (trace1) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, f->c,
                      "h2_secondary_in(%s): read, mode=%d, block=%d, readbytes=%ld", 
                      task->id, mode, block, (long)readbytes);
    }
    
    if (mode == AP_MODE_INIT) {
        return ap_get_brigade(f->c->input_filters, bb, mode, block, readbytes);
    }
    
    if (f->c->aborted) {
        return APR_ECONNABORTED;
    }
    
    if (!task->input.bb) {
        return APR_EOF;
    }
    
    /* Cleanup brigades from those nasty 0 length non-meta buckets
     * that apr_brigade_split_line() sometimes produces. */
    for (b = APR_BRIGADE_FIRST(task->input.bb);
         b != APR_BRIGADE_SENTINEL(task->input.bb); b = next) {
        next = APR_BUCKET_NEXT(b);
        if (b->length == 0 && !APR_BUCKET_IS_METADATA(b)) {
            apr_bucket_delete(b);
        } 
    }
    
    while (APR_BRIGADE_EMPTY(task->input.bb)) {
        /* Get more input data for our request. */
        if (trace1) {
            ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, f->c,
                          "h2_secondary_in(%s): get more data from mplx, block=%d, "
                          "readbytes=%ld", task->id, block, (long)readbytes);
        }
        if (task->input.beam) {
            status = h2_beam_receive(task->input.beam, task->input.bb, block, 
                                     128*1024, NULL);
        }
        else {
            status = APR_EOF;
        }
        
        if (trace1) {
            ap_log_cerror(APLOG_MARK, APLOG_TRACE2, status, f->c,
                          "h2_secondary_in(%s): read returned", task->id);
        }
        if (APR_STATUS_IS_EAGAIN(status) 
            && (mode == AP_MODE_GETLINE || block == APR_BLOCK_READ)) {
            /* chunked input handling does not seem to like it if we
             * return with APR_EAGAIN from a GETLINE read... 
             * upload 100k test on test-ser.example.org hangs */
            status = APR_SUCCESS;
        }
        else if (APR_STATUS_IS_EOF(status)) {
            break;
        }
        else if (status != APR_SUCCESS) {
            return status;
        }

        if (trace1) {
            h2_util_bb_log(f->c, task->stream_id, APLOG_TRACE2, 
                        "input.beam recv raw", task->input.bb);
        }
        if (h2_task_logio_add_bytes_in) {
            apr_brigade_length(bb, 0, &bblen);
            h2_task_logio_add_bytes_in(f->c, bblen);
        }
    }
    
    /* Nothing there, no more data to get. Return. */
    if (status == APR_EOF && APR_BRIGADE_EMPTY(task->input.bb)) {
        return status;
    }

    if (trace1) {
        h2_util_bb_log(f->c, task->stream_id, APLOG_TRACE2, 
                    "task_input.bb", task->input.bb);
    }
           
    if (APR_BRIGADE_EMPTY(task->input.bb)) {
        if (trace1) {
            ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, f->c,
                          "h2_secondary_in(%s): no data", task->id);
        }
        return (block == APR_NONBLOCK_READ)? APR_EAGAIN : APR_EOF;
    }
    
    if (mode == AP_MODE_EXHAUSTIVE) {
        /* return all we have */
        APR_BRIGADE_CONCAT(bb, task->input.bb);
    }
    else if (mode == AP_MODE_READBYTES) {
        status = h2_brigade_concat_length(bb, task->input.bb, rmax);
    }
    else if (mode == AP_MODE_SPECULATIVE) {
        status = h2_brigade_copy_length(bb, task->input.bb, rmax);
    }
    else if (mode == AP_MODE_GETLINE) {
        /* we are reading a single LF line, e.g. the HTTP headers. 
         * this has the nasty side effect to split the bucket, even
         * though it ends with CRLF and creates a 0 length bucket */
        status = apr_brigade_split_line(bb, task->input.bb, block, 
                                        HUGE_STRING_LEN);
        if (APLOGctrace1(f->c)) {
            char buffer[1024];
            apr_size_t len = sizeof(buffer)-1;
            apr_brigade_flatten(bb, buffer, &len);
            buffer[len] = 0;
            if (trace1) {
                ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, f->c,
                              "h2_secondary_in(%s): getline: %s",
                              task->id, buffer);
            }
        }
    }
    else {
        /* Hmm, well. There is mode AP_MODE_EATCRLF, but we chose not
         * to support it. Seems to work. */
        ap_log_cerror(APLOG_MARK, APLOG_ERR, APR_ENOTIMPL, f->c,
                      APLOGNO(03472) 
                      "h2_secondary_in(%s), unsupported READ mode %d", 
                      task->id, mode);
        status = APR_ENOTIMPL;
    }
    
    if (trace1) {
        apr_brigade_length(bb, 0, &bblen);
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, f->c,
                      "h2_secondary_in(%s): %ld data bytes", task->id, (long)bblen);
    }
    return status;
}

static apr_status_t h2_filter_secondary_output(ap_filter_t* filter,
                                               apr_bucket_brigade* brigade)
{
    h2_task *task = h2_ctx_get_task(filter->c);
    apr_status_t status;
    
    ap_assert(task);
    status = secondary_out(task, filter, brigade);
    if (status != APR_SUCCESS) {
        h2_task_rst(task, H2_ERR_INTERNAL_ERROR);
    }
    return status;
}

static apr_status_t h2_filter_parse_h1(ap_filter_t* f, apr_bucket_brigade* bb)
{
    h2_task *task = h2_ctx_get_task(f->c);
    apr_status_t status;
    
    ap_assert(task);
    /* There are cases where we need to parse a serialized http/1.1 
     * response. One example is a 100-continue answer in serialized mode
     * or via a mod_proxy setup */
    while (bb && !task->c->aborted && !task->output.sent_response) {
        status = h2_from_h1_parse_response(task, f, bb);
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, status, f->c,
                      "h2_task(%s): parsed response", task->id);
        if (APR_BRIGADE_EMPTY(bb) || status != APR_SUCCESS) {
            return status;
        }
    }
    
    return ap_pass_brigade(f->next, bb);
}

/*******************************************************************************
 * task things
 ******************************************************************************/
 
int h2_task_can_redo(h2_task *task) {
    if (task->input.beam && h2_beam_was_received(task->input.beam)) {
        /* cannot repeat that. */
        return 0;
    }
    return (!strcmp("GET", task->request->method)
            || !strcmp("HEAD", task->request->method)
            || !strcmp("OPTIONS", task->request->method));
}

int h2_task_has_started(h2_task *task)
{
    return task && task->started_at != 0;
}

void h2_task_redo(h2_task *task)
{
    task->started_at = 0;
    task->worker_done = 0;
    task->rst_error = 0;
}

void h2_task_rst(h2_task *task, int error)
{
    task->rst_error = error;
    if (task->input.beam) {
        h2_beam_leave(task->input.beam);
    }
    if (!task->worker_done) {
        h2_beam_abort(task->output.beam);
    }
    if (task->c) {
        task->c->aborted = 1;
    }
}

/*******************************************************************************
 * Register various hooks
 */
static const char *const mod_ssl[]        = { "mod_ssl.c", NULL};
static int h2_task_pre_conn(conn_rec* c, void *arg);
static int h2_task_process_conn(conn_rec* c);

APR_OPTIONAL_FN_TYPE(ap_logio_add_bytes_in) *h2_task_logio_add_bytes_in;
APR_OPTIONAL_FN_TYPE(ap_logio_add_bytes_out) *h2_task_logio_add_bytes_out;

void h2_task_register_hooks(void)
{
    /* This hook runs on new connections before mod_ssl has a say.
     * Its purpose is to prevent mod_ssl from touching our pseudo-connections
     * for streams.
     */
    ap_hook_pre_connection(h2_task_pre_conn,
                           NULL, mod_ssl, APR_HOOK_FIRST);
    /* When the connection processing actually starts, we might 
     * take over, if the connection is for a task.
     */
    ap_hook_process_connection(h2_task_process_conn, 
                               NULL, NULL, APR_HOOK_FIRST);

    ap_register_input_filter("H2_SECONDARY_IN", h2_filter_secondary_in,
                             NULL, AP_FTYPE_NETWORK);
    ap_register_output_filter("H2_SECONDARY_OUT", h2_filter_secondary_output,
                              NULL, AP_FTYPE_NETWORK);
    ap_register_output_filter("H2_PARSE_H1", h2_filter_parse_h1,
                              NULL, AP_FTYPE_NETWORK);

    ap_register_input_filter("H2_REQUEST", h2_filter_request_in,
                             NULL, AP_FTYPE_PROTOCOL);
    ap_register_output_filter("H2_RESPONSE", h2_filter_headers_out,
                              NULL, AP_FTYPE_PROTOCOL);
    ap_register_output_filter("H2_TRAILERS_OUT", h2_filter_trailers_out,
                              NULL, AP_FTYPE_PROTOCOL);
}

/* post config init */
apr_status_t h2_task_init(apr_pool_t *pool, server_rec *s)
{
    h2_task_logio_add_bytes_in = APR_RETRIEVE_OPTIONAL_FN(ap_logio_add_bytes_in);
    h2_task_logio_add_bytes_out = APR_RETRIEVE_OPTIONAL_FN(ap_logio_add_bytes_out);

    return APR_SUCCESS;
}

static int h2_task_pre_conn(conn_rec* c, void *arg)
{
    h2_ctx *ctx;
    
    if (!c->master) {
        return OK;
    }
    
    ctx = h2_ctx_get(c, 0);
    (void)arg;
    if (ctx->task) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                      "h2_secondary(%s), pre_connection, adding filters", c->log_id);
        ap_add_input_filter("H2_SECONDARY_IN", NULL, NULL, c);
        ap_add_output_filter("H2_PARSE_H1", NULL, NULL, c);
        ap_add_output_filter("H2_SECONDARY_OUT", NULL, NULL, c);
    }
    return OK;
}

h2_task *h2_task_create(conn_rec *secondary, int stream_id,
                        const h2_request *req, h2_mplx *m,
                        h2_bucket_beam *input, 
                        apr_interval_time_t timeout,
                        apr_size_t output_max_mem)
{
    apr_pool_t *pool;
    h2_task *task;
    
    ap_assert(secondary);
    ap_assert(req);

    apr_pool_create(&pool, secondary->pool);
    apr_pool_tag(pool, "h2_task");
    task = apr_pcalloc(pool, sizeof(h2_task));
    if (task == NULL) {
        return NULL;
    }
    task->id          = "000";
    task->stream_id   = stream_id;
    task->c           = secondary;
    task->mplx        = m;
    task->pool        = pool;
    task->request     = req;
    task->timeout     = timeout;
    task->input.beam  = input;
    task->output.max_buffer = output_max_mem;

    return task;
}

void h2_task_destroy(h2_task *task)
{
    if (task->output.beam) {
        h2_beam_log(task->output.beam, task->c, APLOG_TRACE2, "task_destroy");
        h2_beam_destroy(task->output.beam);
        task->output.beam = NULL;
    }
    
    if (task->eor) {
        apr_bucket_destroy(task->eor);
    }
    if (task->pool) {
        apr_pool_destroy(task->pool);
    }
}

apr_status_t h2_task_do(h2_task *task, apr_thread_t *thread, int worker_id)
{
    conn_rec *c;
    
    ap_assert(task);
    c = task->c;
    task->worker_started = 1;
    
    if (c->master) {
        /* See the discussion at <https://github.com/icing/mod_h2/issues/195>
         *
         * Each conn_rec->id is supposed to be unique at a point in time. Since
         * some modules (and maybe external code) uses this id as an identifier
         * for the request_rec they handle, it needs to be unique for secondary 
         * connections also.
         *
         * The MPM module assigns the connection ids and mod_unique_id is using
         * that one to generate identifier for requests. While the implementation
         * works for HTTP/1.x, the parallel execution of several requests per
         * connection will generate duplicate identifiers on load.
         * 
         * The original implementation for secondary connection identifiers used 
         * to shift the master connection id up and assign the stream id to the 
         * lower bits. This was cramped on 32 bit systems, but on 64bit there was
         * enough space.
         * 
         * As issue 195 showed, mod_unique_id only uses the lower 32 bit of the
         * connection id, even on 64bit systems. Therefore collisions in request ids.
         *
         * The way master connection ids are generated, there is some space "at the
         * top" of the lower 32 bits on allmost all systems. If you have a setup
         * with 64k threads per child and 255 child processes, you live on the edge.
         *
         * The new implementation shifts 8 bits and XORs in the worker
         * id. This will experience collisions with > 256 h2 workers and heavy
         * load still. There seems to be no way to solve this in all possible 
         * configurations by mod_h2 alone. 
         */
        task->c->id = (c->master->id << 8)^worker_id;
        task->id = apr_psprintf(task->pool, "%ld-%d", c->master->id, 
                                task->stream_id);
    }
        
    h2_beam_create(&task->output.beam, c->pool, task->stream_id, "output", 
                   H2_BEAM_OWNER_SEND, 0, task->timeout);
    if (!task->output.beam) {
        return APR_ENOMEM;
    }
    
    h2_beam_buffer_size_set(task->output.beam, task->output.max_buffer);
    h2_beam_send_from(task->output.beam, task->pool);
    h2_beam_on_consumed(task->output.beam, NULL, output_consumed, task);

    h2_ctx_create_for(c, task);
    apr_table_setn(c->notes, H2_TASK_ID_NOTE, task->id);

    h2_secondary_run_pre_connection(c, ap_get_conn_socket(c));            

    task->input.bb = apr_brigade_create(task->pool, c->bucket_alloc);
    if (task->request->serialize) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                      "h2_task(%s): serialize request %s %s", 
                      task->id, task->request->method, task->request->path);
        apr_brigade_printf(task->input.bb, NULL, 
                           NULL, "%s %s HTTP/1.1\r\n", 
                           task->request->method, task->request->path);
        apr_table_do(input_ser_header, task, task->request->headers, NULL);
        apr_brigade_puts(task->input.bb, NULL, NULL, "\r\n");
    }
    
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                  "h2_task(%s): process connection", task->id);
                  
    task->c->current_thread = thread; 
    ap_run_process_connection(c);
    
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                  "h2_task(%s): processing done", task->id);
    return output_finish(task);
}

static apr_status_t h2_task_process_request(h2_task *task, conn_rec *c)
{
    const h2_request *req = task->request;
    conn_state_t *cs = c->cs;
    request_rec *r;

    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                  "h2_task(%s): create request_rec", task->id);
    r = h2_request_create_rec(req, c);
    if (r && (r->status == HTTP_OK)) {
        /* set timeouts for virtual host of request */
        if (task->timeout != r->server->timeout) {
            task->timeout = r->server->timeout;
            h2_beam_timeout_set(task->output.beam, task->timeout);
            if (task->input.beam) {
                h2_beam_timeout_set(task->input.beam, task->timeout);
            }
        }
        
        ap_update_child_status(c->sbh, SERVER_BUSY_WRITE, r);
        
        if (cs) {
            cs->state = CONN_STATE_HANDLER;
        }
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                      "h2_task(%s): start process_request", task->id);
    
        /* Add the raw bytes of the request (e.g. header frame lengths to
         * the logio for this request. */
        if (req->raw_bytes && h2_task_logio_add_bytes_in) {
            h2_task_logio_add_bytes_in(c, req->raw_bytes);
        }
        
        ap_process_request(r);
        
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                      "h2_task(%s): process_request done", task->id);
        
        /* After the call to ap_process_request, the
         * request pool may have been deleted.  We set
         * r=NULL here to ensure that any dereference
         * of r that might be added later in this function
         * will result in a segfault immediately instead
         * of nondeterministic failures later.
         */
        if (cs) 
            cs->state = CONN_STATE_WRITE_COMPLETION;
        r = NULL;
    }
    else if (!r) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                      "h2_task(%s): create request_rec failed, r=NULL", task->id);
    }
    else {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                      "h2_task(%s): create request_rec failed, r->status=%d", 
                      task->id, r->status);
    }

    return APR_SUCCESS;
}

static int h2_task_process_conn(conn_rec* c)
{
    h2_ctx *ctx;
    
    if (!c->master) {
        return DECLINED;
    }
    
    ctx = h2_ctx_get(c, 0);
    if (ctx->task) {
        if (!ctx->task->request->serialize) {
            ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c, 
                          "h2_h2, processing request directly");
            h2_task_process_request(ctx->task, c);
            return DONE;
        }
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c, 
                      "h2_task(%s), serialized handling", ctx->task->id);
    }
    else {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c, 
                      "secondary_conn(%ld): has no task", c->id);
    }
    return DECLINED;
}

