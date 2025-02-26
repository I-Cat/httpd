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
 * mod_expires.c
 * version 0.0.11
 * status beta
 *
 * Andrew Wilson <Andrew.Wilson@cm.cf.ac.uk> 26.Jan.96
 *
 * This module allows you to control the form of the Expires: header
 * that Apache issues for each access.  Directives can appear in
 * configuration files or in .htaccess files so expiry semantics can
 * be defined on a per-directory basis.
 *
 * DIRECTIVE SYNTAX
 *
 * Valid directives are:
 *
 *     ExpiresActive on | off
 *     ExpiresDefault <code><seconds>
 *     ExpiresByType type/encoding <code><seconds>
 *
 * Valid values for <code> are:
 *
 *     'M'      expires header shows file modification date + <seconds>
 *     'A'      expires header shows access time + <seconds>
 *
 *              [I'm not sure which of these is best under different
 *              circumstances, I guess it's for other people to explore.
 *              The effects may be indistinguishable for a number of cases]
 *
 * <seconds> should be an integer value [acceptable to atoi()]
 *
 * There is NO space between the <code> and <seconds>.
 *
 * For example, a directory which contains information which changes
 * frequently might contain:
 *
 *     # reports generated by cron every hour.  don't let caches
 *     # hold onto stale information
 *     ExpiresDefault M3600
 *
 * Another example, our html pages can change all the time, the gifs
 * tend not to change often:
 *
 *     # pages are hot (1 week), images are cold (1 month)
 *     ExpiresByType text/html A604800
 *     ExpiresByType image/gif A2592000
 *
 * Expires can be turned on for all URLs on the server by placing the
 * following directive in a conf file:
 *
 *     ExpiresActive on
 *
 * ExpiresActive can also appear in .htaccess files, enabling the
 * behaviour to be turned on or off for each chosen directory.
 *
 *     # turn off Expires behaviour in this directory
 *     # and subdirectories
 *     ExpiresActive off
 *
 * Directives defined for a directory are valid in subdirectories
 * unless explicitly overridden by new directives in the subdirectory
 * .htaccess files.
 *
 * ALTERNATIVE DIRECTIVE SYNTAX
 *
 * Directives can also be defined in a more readable syntax of the form:
 *
 *     ExpiresDefault "<base> [plus] {<num> <type>}*"
 *     ExpiresByType type/encoding "<base> [plus] {<num> <type>}*"
 *
 * where <base> is one of:
 *      access
 *      now             equivalent to 'access'
 *      modification
 *
 * where the 'plus' keyword is optional
 *
 * where <num> should be an integer value [acceptable to atoi()]
 *
 * where <type> is one of:
 *      years
 *      months
 *      weeks
 *      days
 *      hours
 *      minutes
 *      seconds
 *
 * For example, any of the following directives can be used to make
 * documents expire 1 month after being accessed, by default:
 *
 *      ExpiresDefault "access plus 1 month"
 *      ExpiresDefault "access plus 4 weeks"
 *      ExpiresDefault "access plus 30 days"
 *
 * The expiry time can be fine-tuned by adding several '<num> <type>'
 * clauses:
 *
 *      ExpiresByType text/html "access plus 1 month 15 days 2 hours"
 *      ExpiresByType image/gif "modification plus 5 hours 3 minutes"
 *
 * ---
 *
 * Change-log:
 * 29.Jan.96    Hardened the add_* functions.  Server will now bail out
 *              if bad directives are given in the conf files.
 * 02.Feb.96    Returns DECLINED if not 'ExpiresActive on', giving other
 *              expires-aware modules a chance to play with the same
 *              directives. [Michael Rutman]
 * 03.Feb.96    Call tzset() before localtime().  Trying to get the module
 *              to work properly in non GMT timezones.
 * 12.Feb.96    Modified directive syntax to allow more readable commands:
 *                ExpiresDefault "now plus 10 days 20 seconds"
 *                ExpiresDefault "access plus 30 days"
 *                ExpiresDefault "modification plus 1 year 10 months 30 days"
 * 13.Feb.96    Fix call to table_get() with NULL 2nd parameter [Rob Hartill]
 * 19.Feb.96    Call gm_timestr_822() to get time formatted correctly, can't
 *              rely on presence of HTTP_TIME_FORMAT in Apache 1.1+.
 * 21.Feb.96    This version (0.0.9) reverses assumptions made in 0.0.8
 *              about star/star handlers.  Reverting to 0.0.7 behaviour.
 * 08.Jun.96    allows ExpiresDefault to be used with responses that use
 *              the DefaultType by not DECLINING, but instead skipping
 *              the table_get check and then looking for an ExpiresDefault.
 *              [Rob Hartill]
 * 04.Nov.96    'const' definitions added.
 *
 * TODO
 * add support for Cache-Control: max-age=20 from the HTTP/1.1
 * proposal (in this case, a ttl of 20 seconds) [ask roy]
 * add per-file expiry and explicit expiry times - duplicates some
 * of the mod_cern_meta.c functionality.  eg:
 *              ExpiresExplicit index.html "modification plus 30 days"
 *
 * BUGS
 * Hi, welcome to the internet.
 */

#include "apr.h"
#include "apr_strings.h"
#include "apr_lib.h"

#define APR_WANT_STRFUNC
#include "apr_want.h"

#include "ap_config.h"
#include "httpd.h"
#include "http_config.h"
#include "http_log.h"
#include "http_request.h"
#include "http_protocol.h"

typedef struct {
    int active;
    int wildcards;
    char *expiresdefault;
    apr_table_t *expiresbytype;
} expires_dir_config;

/* from mod_dir, why is this alias used?
 */
#define DIR_CMD_PERMS OR_INDEXES

#define ACTIVE_ON       1
#define ACTIVE_OFF      0
#define ACTIVE_DONTCARE 2

module AP_MODULE_DECLARE_DATA expires_module;

static void *create_dir_expires_config(apr_pool_t *p, char *dummy)
{
    expires_dir_config *new =
    (expires_dir_config *) apr_pcalloc(p, sizeof(expires_dir_config));
    new->active = ACTIVE_DONTCARE;
    new->wildcards = 0;
    new->expiresdefault = NULL;
    new->expiresbytype = apr_table_make(p, 4);
    return (void *) new;
}

static const char *set_expiresactive(cmd_parms *cmd, void *in_dir_config, int arg)
{
    expires_dir_config *dir_config = in_dir_config;

    /* if we're here at all it's because someone explicitly
     * set the active flag
     */
    dir_config->active = ACTIVE_ON;
    if (arg == 0) {
        dir_config->active = ACTIVE_OFF;
    }
    return NULL;
}

/* check_code() parse 'code' and return NULL or an error response
 * string.  If we return NULL then real_code contains code converted
 * to the cnnnn format.
 */
static char *check_code(apr_pool_t *p, const char *code, char **real_code)
{
    char *word;
    char base = 'X';
    int modifier = 0;
    int num = 0;
    int factor;

    /* 0.0.4 compatibility?
     */
    if ((code[0] == 'A') || (code[0] == 'M')) {
        *real_code = (char *)code;
        return NULL;
    }

    /* <base> [plus] {<num> <type>}*
     */

    /* <base>
     */
    word = ap_getword_conf(p, &code);
    if (!strncasecmp(word, "now", 1) ||
        !strncasecmp(word, "access", 1)) {
        base = 'A';
    }
    else if (!strncasecmp(word, "modification", 1)) {
        base = 'M';
    }
    else {
        return apr_pstrcat(p, "bad expires code, unrecognised <base> '",
                       word, "'", NULL);
    }

    /* [plus]
     */
    word = ap_getword_conf(p, &code);
    if (!strncasecmp(word, "plus", 1)) {
        word = ap_getword_conf(p, &code);
    }

    /* {<num> <type>}*
     */
    while (word[0]) {
        /* <num>
         */
        if (apr_isdigit(word[0])) {
            num = atoi(word);
        }
        else {
            return apr_pstrcat(p, "bad expires code, numeric value expected <num> '",
                           word, "'", NULL);
        }

        /* <type>
         */
        word = ap_getword_conf(p, &code);
        if (word[0] == '\0') {
            return apr_pstrcat(p, "bad expires code, missing <type>", NULL);
        }

        if (!strncasecmp(word, "years", 1)) {
            factor = 60 * 60 * 24 * 365;
        }
        else if (!strncasecmp(word, "months", 2)) {
            factor = 60 * 60 * 24 * 30;
        }
        else if (!strncasecmp(word, "weeks", 1)) {
            factor = 60 * 60 * 24 * 7;
        }
        else if (!strncasecmp(word, "days", 1)) {
            factor = 60 * 60 * 24;
        }
        else if (!strncasecmp(word, "hours", 1)) {
            factor = 60 * 60;
        }
        else if (!strncasecmp(word, "minutes", 2)) {
            factor = 60;
        }
        else if (!strncasecmp(word, "seconds", 1)) {
            factor = 1;
        }
        else {
            return apr_pstrcat(p, "bad expires code, unrecognised <type>",
                           "'", word, "'", NULL);
        }

        modifier = modifier + factor * num;

        /* next <num>
         */
        word = ap_getword_conf(p, &code);
    }

    *real_code = apr_psprintf(p, "%c%d", base, modifier);

    return NULL;
}

static const char *set_expiresbytype(cmd_parms *cmd, void *in_dir_config,
                                     const char *mime, const char *code)
{
    expires_dir_config *dir_config = in_dir_config;
    char *response, *real_code;
    const char *check;

    check = ap_strrchr_c(mime, '/');
    if (check == NULL) {
        return "Invalid mimetype: should contain a slash";
    }
    if ((strlen(++check) == 1) && (*check == '*')) {
        dir_config->wildcards = 1;
    }

    if ((response = check_code(cmd->pool, code, &real_code)) == NULL) {
        apr_table_setn(dir_config->expiresbytype, mime, real_code);
        return NULL;
    }
    return apr_pstrcat(cmd->pool,
                 "'ExpiresByType ", mime, " ", code, "': ", response, NULL);
}

static const char *set_expiresdefault(cmd_parms *cmd, void *in_dir_config,
                                      const char *code)
{
    expires_dir_config * dir_config = in_dir_config;
    char *response, *real_code;

    if ((response = check_code(cmd->pool, code, &real_code)) == NULL) {
        dir_config->expiresdefault = real_code;
        return NULL;
    }
    return apr_pstrcat(cmd->pool,
                   "'ExpiresDefault ", code, "': ", response, NULL);
}

static const command_rec expires_cmds[] =
{
    AP_INIT_FLAG("ExpiresActive", set_expiresactive, NULL, DIR_CMD_PERMS,
                 "Limited to 'on' or 'off'"),
    AP_INIT_TAKE2("ExpiresByType", set_expiresbytype, NULL, DIR_CMD_PERMS,
                  "a MIME type followed by an expiry date code"),
    AP_INIT_TAKE1("ExpiresDefault", set_expiresdefault, NULL, DIR_CMD_PERMS,
                  "an expiry date code"),
    {NULL}
};

static void *merge_expires_dir_configs(apr_pool_t *p, void *basev, void *addv)
{
    expires_dir_config *new = (expires_dir_config *) apr_pcalloc(p, sizeof(expires_dir_config));
    expires_dir_config *base = (expires_dir_config *) basev;
    expires_dir_config *add = (expires_dir_config *) addv;

    if (add->active == ACTIVE_DONTCARE) {
        new->active = base->active;
    }
    else {
        new->active = add->active;
    }

    if (add->expiresdefault != NULL) {
        new->expiresdefault = add->expiresdefault;
    }
    else {
        new->expiresdefault = base->expiresdefault;
    }
    new->wildcards = add->wildcards;
    new->expiresbytype = apr_table_overlay(p, add->expiresbytype,
                                        base->expiresbytype);
    return new;
}

/*
 * Handle the setting of the expiration response header fields according
 * to our criteria.
 */

static int set_expiration_fields(request_rec *r, const char *code,
                                 apr_table_t *t)
{
    apr_time_t base;
    apr_time_t additional;
    apr_time_t expires;
    int additional_sec;
    char *timestr;

    switch (code[0]) {
    case 'M':
        if (r->finfo.filetype == APR_NOFILE) {
            /* file doesn't exist on disk, so we can't do anything based on
             * modification time.  Note that this does _not_ log an error.
             */
            return DECLINED;
        }
        base = r->finfo.mtime;
        additional_sec = atoi(&code[1]);
        additional = apr_time_from_sec(additional_sec);
        break;
    case 'A':
        /* there's been some discussion and it's possible that
         * 'access time' will be stored in request structure
         */
        base = r->request_time;
        additional_sec = atoi(&code[1]);
        additional = apr_time_from_sec(additional_sec);
        break;
    default:
        /* expecting the add_* routines to be case-hardened this
         * is just a reminder that module is beta
         */
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(01500)
                    "internal error: bad expires code: %s", r->filename);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    expires = base + additional;
    if (expires < r->request_time) {
        expires = r->request_time;
    }
    apr_table_mergen(t, "Cache-Control",
                     apr_psprintf(r->pool, "max-age=%" APR_TIME_T_FMT,
                                  apr_time_sec(expires - r->request_time)));
    timestr = apr_palloc(r->pool, APR_RFC822_DATE_LEN);
    apr_rfc822_date(timestr, expires);
    apr_table_setn(t, "Expires", timestr);
    return OK;
}

/*
 * Output filter to set the Expires response header field
 * according to the content-type of the response -- if it hasn't
 * already been set.
 */
static apr_status_t expires_filter(ap_filter_t *f,
                                   apr_bucket_brigade *b)
{
    request_rec *r;
    expires_dir_config *conf;
    const char *expiry;
    apr_table_t *t;

    /* Don't add Expires headers to errors */
    if (ap_is_HTTP_ERROR(f->r->status)) {
        ap_remove_output_filter(f);
        return ap_pass_brigade(f->next, b);
    }

    r = f->r;
    conf = (expires_dir_config *) ap_get_module_config(r->per_dir_config,
                                                       &expires_module);

    /*
     * Check to see which output header table we should use;
     * mod_cgi loads script fields into r->err_headers_out,
     * for instance.
     */
    expiry = apr_table_get(r->err_headers_out, "Expires");
    if (expiry != NULL) {
        t = r->err_headers_out;
    }
    else {
        expiry = apr_table_get(r->headers_out, "Expires");
        t = r->headers_out;
    }
    if (expiry == NULL) {
        /*
         * No expiration has been set, so we can apply any managed by
         * this module.  First, check to see if there is an applicable
         * ExpiresByType directive.
         */
        expiry = apr_table_get(conf->expiresbytype,
                               ap_field_noparam(r->pool, r->content_type));
        if (expiry == NULL) {
            int usedefault = 1;
            /*
             * See if we have a wildcard entry for the major type.
             */
            if (conf->wildcards) {
                char *checkmime;
                char *spos;
                checkmime = apr_pstrdup(r->pool, r->content_type);
                spos = checkmime ? ap_strchr(checkmime, '/') : NULL;
                if (spos != NULL) {
                    /*
                     * Without a '/' character, nothing we have will match.
                     * However, we have one.
                     */
                    if (strlen(++spos) > 0) {
                        *spos++ = '*';
                        *spos = '\0';
                    }
                    else {
                        checkmime = apr_pstrcat(r->pool, checkmime, "*", NULL);
                    }
                    expiry = apr_table_get(conf->expiresbytype, checkmime);
                    usedefault = (expiry == NULL);
                }
            }
            if (usedefault) {
                /*
                 * Use the ExpiresDefault directive
                 */
                expiry = conf->expiresdefault;
            }
        }
        if (expiry != NULL) {
            set_expiration_fields(r, expiry, t);
        }
    }
    ap_remove_output_filter(f);
    return ap_pass_brigade(f->next, b);
}

static void expires_insert_filter(request_rec *r)
{
    expires_dir_config *conf;

    /* Don't add Expires headers to errors */
    if (ap_is_HTTP_ERROR(r->status)) {
        return;
    }
    /* Say no to subrequests */
    if (r->main != NULL) {
        return;
    }
    conf = (expires_dir_config *) ap_get_module_config(r->per_dir_config,
                                                       &expires_module);

    /* Check to see if the filter is enabled and if there are any applicable
     * config directives for this directory scope
     */
    if (conf->active != ACTIVE_ON ||
        (apr_is_empty_table(conf->expiresbytype) && !conf->expiresdefault)) {
        return;
    }
    ap_add_output_filter("MOD_EXPIRES", NULL, r, r->connection);
}

static void register_hooks(apr_pool_t *p)
{
    /* mod_expires needs to run *before* the cache save filter which is
     * AP_FTYPE_CONTENT_SET-1.  Otherwise, our expires won't be honored.
     */
    ap_register_output_filter("MOD_EXPIRES", expires_filter, NULL,
                              AP_FTYPE_CONTENT_SET-2);
    ap_hook_insert_error_filter(expires_insert_filter, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_insert_filter(expires_insert_filter, NULL, NULL, APR_HOOK_MIDDLE);
}

AP_DECLARE_MODULE(expires) =
{
    STANDARD20_MODULE_STUFF,
    create_dir_expires_config,  /* dir config creater */
    merge_expires_dir_configs,  /* dir merger --- default is to override */
    NULL,                       /* server config */
    NULL,                       /* merge server configs */
    expires_cmds,               /* command apr_table_t */
    register_hooks              /* register hooks */
};
