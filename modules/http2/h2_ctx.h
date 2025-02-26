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

#ifndef __mod_h2__h2_ctx__
#define __mod_h2__h2_ctx__

struct h2_session;
struct h2_task;
struct h2_config;

/**
 * The h2 module context associated with a connection. 
 *
 * It keeps track of the different types of connections:
 * - those from clients that use HTTP/2 protocol
 * - those from clients that do not use HTTP/2
 * - those created by ourself to perform work on HTTP/2 streams
 */
typedef struct h2_ctx {
    const char *protocol;           /* the protocol negotiated */
    struct h2_session *session;     /* the session established */
    struct h2_task *task;           /* the h2_task executing or NULL */
    const char *hostname;           /* hostname negotiated via SNI, optional */
    server_rec *server;             /* httpd server config selected. */
    const struct h2_config *config; /* effective config in this context */
} h2_ctx;

/**
 * Get (or create) a h2 context record for this connection.
 * @param c the connection to look at
 * @param create != 0 iff missing context shall be created
 * @return h2 context of this connection
 */
h2_ctx *h2_ctx_get(const conn_rec *c, int create);
void h2_ctx_clear(const conn_rec *c);

h2_ctx *h2_ctx_rget(const request_rec *r);
h2_ctx *h2_ctx_create_for(const conn_rec *c, struct h2_task *task);


/* Set the h2 protocol established on this connection context or
 * NULL when other protocols are in place.
 */
h2_ctx *h2_ctx_protocol_set(h2_ctx *ctx, const char *proto);

/* Update the server_rec relevant for this context. A server for
 * a connection may change during SNI handling, for example.
 */
h2_ctx *h2_ctx_server_update(h2_ctx *ctx, server_rec *s);

void h2_ctx_session_set(h2_ctx *ctx, struct h2_session *session);

/**
 * Get the h2 protocol negotiated for this connection, or NULL.
 */
const char *h2_ctx_protocol_get(const conn_rec *c);

struct h2_session *h2_ctx_get_session(conn_rec *c);
struct h2_task *h2_ctx_get_task(conn_rec *c);


#endif /* defined(__mod_h2__h2_ctx__) */
