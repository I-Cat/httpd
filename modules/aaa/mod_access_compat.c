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

/*
 * Security options etc.
 *
 * Module derived from code originally written by Rob McCool
 *
 */

#include "apr_strings.h"
#include "apr_network_io.h"
#include "apr_md5.h"

#define APR_WANT_STRFUNC
#define APR_WANT_BYTEFUNC
#include "apr_want.h"

#include "ap_config.h"
#include "httpd.h"
#include "http_core.h"
#include "http_config.h"
#include "http_log.h"
#include "http_protocol.h"
#include "http_request.h"

#include "mod_auth.h"

#if APR_HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

enum allowdeny_type {
    T_ENV,
    T_NENV,
    T_ALL,
    T_IP,
    T_HOST,
    T_FAIL
};

typedef struct {
    ap_method_mask_t limited;
    union {
        char *from;
        apr_ipsubnet_t *ip;
    } x;
    enum allowdeny_type type;
} allowdeny;

/* things in the 'order' array */
#define DENY_THEN_ALLOW 0
#define ALLOW_THEN_DENY 1
#define MUTUAL_FAILURE 2

typedef struct {
    int order[METHODS];
    apr_array_header_t *allows;
    apr_array_header_t *denys;
    int *satisfy; /* for every method one */
} access_compat_dir_conf;

module AP_MODULE_DECLARE_DATA access_compat_module;

static void *create_access_compat_dir_config(apr_pool_t *p, char *dummy)
{
    int i;
    access_compat_dir_conf *conf =
        (access_compat_dir_conf *)apr_pcalloc(p, sizeof(access_compat_dir_conf));

    for (i = 0; i < METHODS; ++i) {
        conf->order[i] = DENY_THEN_ALLOW;
    }
    conf->allows = apr_array_make(p, 1, sizeof(allowdeny));
    conf->denys = apr_array_make(p, 1, sizeof(allowdeny));
    conf->satisfy = apr_palloc(p, sizeof(*conf->satisfy) * METHODS);
    for (i = 0; i < METHODS; ++i) {
        conf->satisfy[i] = SATISFY_NOSPEC;
    }

    return (void *)conf;
}

static const char *order(cmd_parms *cmd, void *dv, const char *arg)
{
    access_compat_dir_conf *d = (access_compat_dir_conf *) dv;
    int i, o;

    if (!strcasecmp(arg, "allow,deny"))
        o = ALLOW_THEN_DENY;
    else if (!strcasecmp(arg, "deny,allow"))
        o = DENY_THEN_ALLOW;
    else if (!strcasecmp(arg, "mutual-failure"))
        o = MUTUAL_FAILURE;
    else
        return "unknown order";

    for (i = 0; i < METHODS; ++i)
        if (cmd->limited & (AP_METHOD_BIT << i))
            d->order[i] = o;

    return NULL;
}

static const char *satisfy(cmd_parms *cmd, void *dv, const char *arg)
{
    access_compat_dir_conf *d = (access_compat_dir_conf *) dv;
    int satisfy = SATISFY_NOSPEC;
    int i;

    if (!strcasecmp(arg, "all")) {
        satisfy = SATISFY_ALL;
    }
    else if (!strcasecmp(arg, "any")) {
        satisfy = SATISFY_ANY;
    }
    else {
        return "Satisfy either 'any' or 'all'.";
    }

    for (i = 0; i < METHODS; ++i) {
        if (cmd->limited & (AP_METHOD_BIT << i)) {
            d->satisfy[i] = satisfy;
        }
    }

    return NULL;
}

static const char *allow_cmd(cmd_parms *cmd, void *dv, const char *from,
                             const char *where_c)
{
    access_compat_dir_conf *d = (access_compat_dir_conf *) dv;
    allowdeny *a;
    char *where = apr_pstrdup(cmd->pool, where_c);
    char *s;
    apr_status_t rv;

    if (strcasecmp(from, "from"))
        return "allow and deny must be followed by 'from'";

    a = (allowdeny *) apr_array_push(cmd->info ? d->allows : d->denys);
    a->x.from = where;
    a->limited = cmd->limited;

    if (!strncasecmp(where, "env=!", 5)) {
        a->type = T_NENV;
        a->x.from += 5;

    }
    else if (!strncasecmp(where, "env=", 4)) {
        a->type = T_ENV;
        a->x.from += 4;

    }
    else if (!strcasecmp(where, "all")) {
        a->type = T_ALL;
    }
    else if ((s = ap_strchr(where, '/'))) {
        *s++ = '\0';
        rv = apr_ipsubnet_create(&a->x.ip, where, s, cmd->pool);
        if(APR_STATUS_IS_EINVAL(rv)) {
            /* looked nothing like an IP address */
            return "An IP address was expected";
        }
        else if (rv != APR_SUCCESS) {
            return apr_psprintf(cmd->pool, "%pm", &rv);
        }
        a->type = T_IP;
    }
    else if (!APR_STATUS_IS_EINVAL(rv = apr_ipsubnet_create(&a->x.ip, where,
                                                            NULL, cmd->pool))) {
        if (rv != APR_SUCCESS)
            return apr_psprintf(cmd->pool, "%pm", &rv);
        a->type = T_IP;
    }
    else if (ap_strchr(where, '#')) {
        return "No comments are allowed here";
    }
    else { /* no slash, didn't look like an IP address => must be a host */
        a->type = T_HOST;
    }

    return NULL;
}

static char its_an_allow;

static const command_rec access_compat_cmds[] =
{
    AP_INIT_TAKE1("order", order, NULL, OR_LIMIT,
                  "'allow,deny', 'deny,allow', or 'mutual-failure'"),
    AP_INIT_ITERATE2("allow", allow_cmd, &its_an_allow, OR_LIMIT,
                     "'from' followed by hostnames or IP-address wildcards"),
    AP_INIT_ITERATE2("deny", allow_cmd, NULL, OR_LIMIT,
                     "'from' followed by hostnames or IP-address wildcards"),
    AP_INIT_TAKE1("Satisfy", satisfy, NULL, OR_AUTHCFG,
                  "access policy if both allow and require used ('all' or 'any')"),
    {NULL}
};

static int in_domain(const char *domain, const char *what)
{
    int dl = strlen(domain);
    int wl = strlen(what);

    if ((wl - dl) >= 0) {
        if (strcasecmp(domain, &what[wl - dl]) != 0) {
            return 0;
        }

        /* Make sure we matched an *entire* subdomain --- if the user
         * said 'allow from good.com', we don't want people from nogood.com
         * to be able to get in.
         */

        if (wl == dl) {
            return 1;                /* matched whole thing */
        }
        else {
            return (domain[0] == '.' || what[wl - dl - 1] == '.');
        }
    }
    else {
        return 0;
    }
}

static int find_allowdeny(request_rec *r, apr_array_header_t *a, int method)
{

    allowdeny *ap = (allowdeny *) a->elts;
    ap_method_mask_t mmask = (AP_METHOD_BIT << method);
    int i;
    int gothost = 0;
    const char *remotehost = NULL;

    for (i = 0; i < a->nelts; ++i) {
        if (!(mmask & ap[i].limited)) {
            continue;
        }

        switch (ap[i].type) {
        case T_ENV:
            if (apr_table_get(r->subprocess_env, ap[i].x.from)) {
                return 1;
            }
            break;

        case T_NENV:
            if (!apr_table_get(r->subprocess_env, ap[i].x.from)) {
                return 1;
            }
            break;

        case T_ALL:
            return 1;

        case T_IP:
            if (apr_ipsubnet_test(ap[i].x.ip, r->useragent_addr)) {
                return 1;
            }
            break;

        case T_HOST:
            if (!gothost) {
                int remotehost_is_ip;

                remotehost = ap_get_useragent_host(r, REMOTE_DOUBLE_REV,
                                                   &remotehost_is_ip);

                if ((remotehost == NULL) || remotehost_is_ip) {
                    gothost = 1;
                }
                else {
                    gothost = 2;
                }
            }

            if ((gothost == 2) && in_domain(ap[i].x.from, remotehost)) {
                return 1;
            }
            break;

        case T_FAIL:
            /* do nothing? */
            break;
        }
    }

    return 0;
}

static int access_compat_ap_satisfies(request_rec *r)
{
    access_compat_dir_conf *conf = (access_compat_dir_conf *)
        ap_get_module_config(r->per_dir_config, &access_compat_module);

    return conf->satisfy[r->method_number];
}

static int check_dir_access(request_rec *r)
{
    int method = r->method_number;
    int ret = OK;
    access_compat_dir_conf *a = (access_compat_dir_conf *)
        ap_get_module_config(r->per_dir_config, &access_compat_module);

    if (a->order[method] == ALLOW_THEN_DENY) {
        ret = HTTP_FORBIDDEN;
        if (find_allowdeny(r, a->allows, method)) {
            ret = OK;
        }
        if (find_allowdeny(r, a->denys, method)) {
            ret = HTTP_FORBIDDEN;
        }
    }
    else if (a->order[method] == DENY_THEN_ALLOW) {
        if (find_allowdeny(r, a->denys, method)) {
            ret = HTTP_FORBIDDEN;
        }
        if (find_allowdeny(r, a->allows, method)) {
            ret = OK;
        }
    }
    else {
        if (find_allowdeny(r, a->allows, method)
            && !find_allowdeny(r, a->denys, method)) {
            ret = OK;
        }
        else {
            ret = HTTP_FORBIDDEN;
        }
    }

    if (ret == HTTP_FORBIDDEN) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(01797)
                      "client denied by server configuration: %s%s",
                      r->filename ? "" : "uri ",
                      r->filename ? r->filename : r->uri);
    }

    return ret;
}

static void register_hooks(apr_pool_t *p)
{
    APR_REGISTER_OPTIONAL_FN(access_compat_ap_satisfies);

    /* This can be access checker since we don't require r->user to be set. */
    ap_hook_check_access(check_dir_access, NULL, NULL, APR_HOOK_MIDDLE,
                         AP_AUTH_INTERNAL_PER_CONF);
}

AP_DECLARE_MODULE(access_compat) =
{
    STANDARD20_MODULE_STUFF,
    create_access_compat_dir_config,   /* dir config creater */
    NULL,                           /* dir merger --- default is to override */
    NULL,                           /* server config */
    NULL,                           /* merge server config */
    access_compat_cmds,
    register_hooks                  /* register hooks */
};
