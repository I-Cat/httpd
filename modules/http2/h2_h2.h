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

#ifndef __mod_h2__h2_h2__
#define __mod_h2__h2_h2__

/**
 * List of ALPN protocol identifiers that we support in cleartext
 * negotiations. NULL terminated.
 */
extern const char *h2_clear_protos[];

/**
 * List of ALPN protocol identifiers that we support in TLS encrypted 
 * negotiations. NULL terminated.
 */
extern const char *h2_tls_protos[];

/**
 * Provide a user readable description of the HTTP/2 error code-
 * @param h2_error http/2 error code, as in rfc 7540, ch. 7
 * @return textual description of code or that it is unknown.
 */
const char *h2_h2_err_description(unsigned int h2_error);

/*
 * One time, post config initialization.
 */
apr_status_t h2_h2_init(apr_pool_t *pool, server_rec *s);

/* Register apache hooks for h2 protocol
 */
void h2_h2_register_hooks(void);

/**
 * Check if the given connection fulfills the requirements as configured.
 * @param c the connection
 * @param require_all != 0 iff any missing connection properties make
 *    the test fail. For example, a cipher might not have been selected while
 *    the handshake is still ongoing.
 * @return != 0 iff connection requirements are met
 */
int h2_is_acceptable_connection(conn_rec *c, request_rec *r, int require_all);

/**
 * Check if the "Upgrade" HTTP/1.1 mode of protocol switching is enabled
 * for the given request.
 * @param r the request to check
 * @return != 0 iff Upgrade switching is enabled
 */
int h2_allows_h2_upgrade(request_rec *r);


#endif /* defined(__mod_h2__h2_h2__) */
