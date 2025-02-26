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

#include "httpd.h"
#include "http_core.h"
#include "http_config.h"
#include "http_protocol.h"
#include "http_request.h"
#include "http_log.h"
#include "apr_strings.h"

/**
 * This module makes it easy to restrict what HTTP methods can be ran against
 * a server.
 *
 * It provides one command:
 *    AllowMethods
 * This command takes a list of HTTP methods to allow.
 *
 *  The most common configuration should be like this:
 *   <Directory />
 *    AllowMethods GET HEAD OPTIONS
 *   </Directory>
 *   <Directory /special/cgi-bin>
 *      AllowMethods GET HEAD OPTIONS POST
 *   </Directory>
 *  Non-matching methods will be returned a status 405 (method not allowed)
 *
 *  To allow all methods, and effectively turn off mod_allowmethods, use:
 *    AllowMethods reset
 */

typedef struct am_conf_t {
    int allowed_set;            /* AllowMethods has been set/changed flag */
    int enforce_methods;        /* Enforce AllowMethods flag              */
    ap_method_mask_t add;       /* Methods Added by +METHOD mask          */
    ap_method_mask_t remove;    /* Methods Removed by -METHOD mask        */
    ap_method_mask_t allowed;   /* Allowed Methods mask                   */
} am_conf_t;

module AP_MODULE_DECLARE_DATA allowmethods_module;

static int am_check_access(request_rec *r)
{
    int method = r->method_number;
    am_conf_t *conf;

    conf = (am_conf_t *) ap_get_module_config(r->per_dir_config,
                                              &allowmethods_module);

    if (!conf || conf->enforce_methods == 0) {
        return DECLINED;
    }

    r->allowed = conf->allowed;

    if (conf->allowed & (AP_METHOD_BIT << method)) {
        return DECLINED;
    }

    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(01623)
                  "client method denied by server configuration: '%s' to %s%s",
                  r->method,
                  r->filename ? "" : "uri ",
                  r->filename ? r->filename : r->uri);

    return HTTP_METHOD_NOT_ALLOWED;
}

static void *am_create_conf(apr_pool_t *p, char *dummy)
{
    am_conf_t *conf = apr_pcalloc(p, sizeof(am_conf_t));

    conf->allowed         = INT_MAX;
    conf->allowed_set     = 0;
    conf->add             = 0;
    conf->remove          = 0;
    conf->enforce_methods = 0;
    return conf;
}

static void *am_merge_conf(apr_pool_t *pool, void *a, void *b)
{
    am_conf_t *base = (am_conf_t *)a;
    am_conf_t *add = (am_conf_t *)b;
    am_conf_t *conf = apr_palloc(pool, sizeof(am_conf_t));

    if (add->allowed_set) {
        conf->add    = 0;
        conf->remove = 0;

        /* Add/Remove AllowedMethods set with + or - */
        if( add->add || add->remove ) {
            conf->allowed = base->allowed | add->allowed;

            if( add->add ) {
                conf->allowed |= add->add;
            }
            if( add->remove ) {
                conf->allowed &= ~(add->remove);
            }

            conf->enforce_methods = 1;
            conf->allowed_set     = 1;
        }
        /* Straightforward AllowMethods settings, just set it */
        else
        {
            conf->allowed          = add->allowed;
            conf->allowed_set      = add->allowed_set;
            conf->enforce_methods  = add->enforce_methods;
        }
    }
    else {
        conf->allowed         = base->allowed;
        conf->add             = base->add;
        conf->remove          = base->remove;
        conf->allowed_set     = base->allowed_set;
        conf->enforce_methods = base->enforce_methods;
    }

    return conf;
}

static const char *am_allowmethods(cmd_parms *cmd, void *d, int argc,
                                   char *const argv[])
{
    int i;
    am_conf_t *conf = (am_conf_t *)d;
    int merge = 0;
    int first = 1;
    char *method;
    char action = '\0';

    if (argc == 0) {
        return "AllowMethods: No method or 'reset' keyword given";
    }
    if (argc == 1) {
        if (!ap_cstr_casecmp(argv[0], "reset")) {
            conf->allowed         = 0;
            conf->enforce_methods = 0;
            conf->add             = 0;
            conf->remove          = 0;
            conf->allowed_set     = 1;
            return NULL;
        }
    }

    conf->allowed = 0;
    conf->add     = 0;
    conf->remove  = 0;

    for (i = 0; i < argc; i++) {
        int m;
        char *w = argv[i];

        if( *w == '-' || *w == '+' ) {
            if (!merge && !first) {
                return "Either all methods in AllowMethods must start with + or -, or no methods may.";
            }

            action = *(w++);
            method = w;
            merge = 1;
        }
        else if (merge) {
            return "Either all methods in AllowMethods must start with + or -, or no methods may.";
        }
        else {
            method = w;
        }

        m = ap_method_number_of((char const*)method);

        if (m == M_INVALID) {
            return apr_pstrcat(cmd->pool, "AllowMethods: Invalid Method '",
                               method, "'", NULL);
        }

        if (action == '-' ) {
            conf->remove  |=  (AP_METHOD_BIT << m);
            conf->add     &= ~(AP_METHOD_BIT << m);
            conf->allowed &= ~(AP_METHOD_BIT << m);
        }
        else if (action == '+' ) {
            conf->remove  &= ~(AP_METHOD_BIT << m);
            conf->add     |=  (AP_METHOD_BIT << m);
            conf->allowed |=  (AP_METHOD_BIT << m);
        }
        else {
            conf->allowed |= (AP_METHOD_BIT << m);
        }

        first = 0;
    }
    conf->allowed_set     = 1;
    conf->enforce_methods = 1;

    return NULL;
}

static void am_register_hooks(apr_pool_t * p)
{
    ap_hook_access_checker(am_check_access, NULL, NULL, APR_HOOK_REALLY_FIRST);
}

static const command_rec am_cmds[] = {
    AP_INIT_TAKE_ARGV("AllowMethods", am_allowmethods, NULL,
                      ACCESS_CONF,
                      "only allow specific methods"),
    {NULL}
};

AP_DECLARE_MODULE(allowmethods) = {
    STANDARD20_MODULE_STUFF,
    am_create_conf,
    am_merge_conf,
    NULL,
    NULL,
    am_cmds,
    am_register_hooks,
};
