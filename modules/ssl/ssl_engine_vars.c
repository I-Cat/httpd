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

/*                      _             _
 *  _ __ ___   ___   __| |    ___ ___| |  mod_ssl
 * | '_ ` _ \ / _ \ / _` |   / __/ __| |  Apache Interface to OpenSSL
 * | | | | | | (_) | (_| |   \__ \__ \ |
 * |_| |_| |_|\___/ \__,_|___|___/___/_|
 *                      |_____|
 *  ssl_engine_vars.c
 *  Variable Lookup Facility
 */
                             /* ``Those of you who think they
                                  know everything are very annoying
                                  to those of us who do.''
                                                  -- Unknown       */
#include "ssl_private.h"
#include "mod_ssl.h"
#include "ap_expr.h"

#include "apr_time.h"

/*  _________________________________________________________________
**
**  Variable Lookup
**  _________________________________________________________________
*/

static const char *ssl_var_lookup_ssl(apr_pool_t *p, const SSLConnRec *sslconn, request_rec *r, const char *var);
static const char *ssl_var_lookup_ssl_cert(apr_pool_t *p, request_rec *r, X509 *xs, const char *var);
static const char *ssl_var_lookup_ssl_cert_dn(apr_pool_t *p, X509_NAME *xsname, const char *var);
static const char *ssl_var_lookup_ssl_cert_san(apr_pool_t *p, X509 *xs, const char *var);
static const char *ssl_var_lookup_ssl_cert_valid(apr_pool_t *p, ASN1_TIME *tm);
static const char *ssl_var_lookup_ssl_cert_remain(apr_pool_t *p, ASN1_TIME *tm);
static const char *ssl_var_lookup_ssl_cert_serial(apr_pool_t *p, X509 *xs);
static const char *ssl_var_lookup_ssl_cert_chain(apr_pool_t *p, STACK_OF(X509) *sk, const char *var, int pem);
static const char *ssl_var_lookup_ssl_cert_rfc4523_cea(apr_pool_t *p, SSL *ssl);
static const char *ssl_var_lookup_ssl_cert_verify(apr_pool_t *p, const SSLConnRec *sslconn);
static const char *ssl_var_lookup_ssl_cipher(apr_pool_t *p, const SSLConnRec *sslconn, const char *var);
static void  ssl_var_lookup_ssl_cipher_bits(SSL *ssl, int *usekeysize, int *algkeysize);
static const char *ssl_var_lookup_ssl_version(const char *var);
static const char *ssl_var_lookup_ssl_compress_meth(SSL *ssl);

static const SSLConnRec *ssl_get_effective_config(conn_rec *c)
{
    const SSLConnRec *sslconn = myConnConfig(c);
    if (!(sslconn && sslconn->ssl) && c->master) {
        /* use master connection if no SSL defined here */
        sslconn = myConnConfig(c->master);
    }
    return sslconn;
}

static int ssl_is_https(conn_rec *c)
{
    const SSLConnRec *sslconn = ssl_get_effective_config(c);
    return sslconn && sslconn->ssl;
}

/* Returns certificate data, either PEM encoded if 'pem' is non-zero,
 * else plain base64-encoded DER. */
static const char *ssl_var_lookup_ssl_cert_data(apr_pool_t *p, X509 *xs,
                                                int pem)
{
    BIO *bio;

    if ((bio = BIO_new(BIO_s_mem())) == NULL)
        return NULL;

    if (pem) {
        PEM_write_bio_X509(bio, xs);
    }
    else {
        BIO *b64 = BIO_new(BIO_f_base64());
        if (b64 == NULL) {
            BIO_free(bio);
            return NULL;
        }
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
        b64 = BIO_push(b64, bio);
        i2d_X509_bio(b64, xs);
        BIO_flush(b64); /* ensures trailing bytes are padded */
        BIO_pop(b64);
        BIO_free(b64);
    }

    return modssl_bio_free_read(p, bio);
}

/* SSLv3 uses 36 bytes for Finishd messages, TLS1.0 12 bytes,
 * So tls-unique is max 36 bytes, however with tls-server-end-point,
 * the CB data is the certificate signature, so we use the maximum
 * hash size known to the library (currently 64).
 * */
#define TLS_CB_MAX EVP_MAX_MD_SIZE
#define TLS_UNIQUE_PREFIX "tls-unique:"
#define TLS_SERVER_END_POINT_PREFIX "tls-server-end-point:"

static apr_status_t ssl_get_tls_cb(apr_pool_t *p, conn_rec *c, const char *type,
                                   unsigned char **buf, apr_size_t *size)
{
    const SSLConnRec *sslconn = ssl_get_effective_config(c);
    const char *prefix;
    apr_size_t preflen;
    const unsigned char *data;
    unsigned char cb[TLS_CB_MAX], *retbuf;
    unsigned int l = 0;
    X509 *x = NULL;

    if (!sslconn || !sslconn->ssl) {
        return APR_EGENERAL;
    }
    if (strcEQ(type, "SERVER_TLS_UNIQUE")) {
        l = SSL_get_peer_finished(sslconn->ssl, cb, TLS_CB_MAX);
    }
    else if (strcEQ(type, "CLIENT_TLS_UNIQUE")) {
        l = SSL_get_finished(sslconn->ssl, cb, TLS_CB_MAX);
    }
    else if (strcEQ(type, "SERVER_TLS_SERVER_END_POINT")) {
        x = SSL_get_certificate(sslconn->ssl);
        /* Increase refcount so X509_free below works for both client
         * and server cases. */
        if (x) X509_up_ref(x);
    }
    else if (strcEQ(type, "CLIENT_TLS_SERVER_END_POINT")) {
        x = SSL_get_peer_certificate(sslconn->ssl);
    }
    if (l > 0) {
        preflen = sizeof(TLS_UNIQUE_PREFIX) -1;
        prefix = TLS_UNIQUE_PREFIX;
        data = cb;
    } 
    else if (x != NULL) {
        const EVP_MD *md;

#if MODSSL_USE_OPENSSL_PRE_1_1_API
        md = EVP_get_digestbynid(OBJ_obj2nid(x->sig_alg->algorithm));
#else
        md = EVP_get_digestbynid(X509_get_signature_nid(x));
#endif
        /* Override digest as specified by RFC 5929 section 4.1. */
        if (md == NULL || md == EVP_md5() || md == EVP_sha1()) {
            md = EVP_sha256();
        }
        if (!X509_digest(x, md, cb, &l)) {
            return APR_EGENERAL;
        }

        preflen = sizeof(TLS_SERVER_END_POINT_PREFIX) - 1;
        prefix = TLS_SERVER_END_POINT_PREFIX;
        data = cb;

        X509_free(x);
    } 
    else {
        return APR_EGENERAL;
    }

    retbuf = apr_palloc(p, preflen + l);
    memcpy(retbuf, prefix, preflen);
    memcpy(&retbuf[preflen], data, l);
    *size = preflen + l;
    *buf = retbuf;

    return APR_SUCCESS;
}

static const char var_interface[] = "mod_ssl/" AP_SERVER_BASEREVISION;
static char var_library_interface[] = MODSSL_LIBRARY_TEXT;
static char *var_library = NULL;

static apr_array_header_t *expr_peer_ext_list_fn(ap_expr_eval_ctx_t *ctx,
                                                 const void *dummy,
                                                 const char *arg)
{
    return ssl_ext_list(ctx->p, ctx->c, 1, arg);
}

static const char *expr_var_fn(ap_expr_eval_ctx_t *ctx, const void *data)
{
    const char *var = data;
    const SSLConnRec *sslconn = ssl_get_effective_config(ctx->c);

    return sslconn ? ssl_var_lookup_ssl(ctx->p, sslconn, ctx->r, var) : NULL;
}

static const char *expr_func_fn(ap_expr_eval_ctx_t *ctx, const void *data,
                                const char *arg)
{
    const char *var = arg;

    return var ? ssl_var_lookup(ctx->p, ctx->s, ctx->c, ctx->r, var) : NULL;
}

static int ssl_expr_lookup(ap_expr_lookup_parms *parms)
{
    switch (parms->type) {
    case AP_EXPR_FUNC_VAR:
        /* for now, we just handle everything that starts with SSL_, but
         * register our hook as APR_HOOK_LAST
         * XXX: This can be optimized
         */
        if (strcEQn(parms->name, "SSL_", 4)) {
            *parms->func = expr_var_fn;
            *parms->data = parms->name + 4;
            return OK;
        }
        break;
    case AP_EXPR_FUNC_STRING:
        /* Function SSL() is implemented by us.
         */
        if (strcEQ(parms->name, "SSL")) {
            *parms->func = expr_func_fn;
            *parms->data = NULL;
            return OK;
        }
        break;
    case AP_EXPR_FUNC_LIST:
        if (strcEQ(parms->name, "PeerExtList")) {
            *parms->func = expr_peer_ext_list_fn;
            *parms->data = "PeerExtList";
            return OK;
        }
        break;
    }
    return DECLINED;
}


void ssl_var_register(apr_pool_t *p)
{
    char *cp, *cp2;

    APR_REGISTER_OPTIONAL_FN(ssl_is_https);
    APR_REGISTER_OPTIONAL_FN(ssl_get_tls_cb);
    APR_REGISTER_OPTIONAL_FN(ssl_var_lookup);
    APR_REGISTER_OPTIONAL_FN(ssl_ext_list);

    /* Perform once-per-process library version determination: */
    var_library = apr_pstrdup(p, MODSSL_LIBRARY_DYNTEXT);

    if ((cp = strchr(var_library, ' ')) != NULL) {
        *cp = '/';
        if ((cp2 = strchr(cp, ' ')) != NULL)
            *cp2 = NUL;
    }

    if ((cp = strchr(var_library_interface, ' ')) != NULL) {
        *cp = '/';
        if ((cp2 = strchr(cp, ' ')) != NULL)
            *cp2 = NUL;
    }

    ap_hook_expr_lookup(ssl_expr_lookup, NULL, NULL, APR_HOOK_MIDDLE);
}

/* This function must remain safe to use for a non-SSL connection. */
const char *ssl_var_lookup(apr_pool_t *p, server_rec *s,
                           conn_rec *c, request_rec *r,
                           const char *var)
{
    const char *result = NULL;
    apr_time_exp_t tm;

    /*
     * Request dependent stuff
     */
    if (r != NULL) {
        switch (var[0]) {
        case 'H':
        case 'h':
            if (strcEQ(var, "HTTP_USER_AGENT"))
                result = apr_table_get(r->headers_in, "User-Agent");
            else if (strcEQ(var, "HTTP_REFERER"))
                result = apr_table_get(r->headers_in, "Referer");
            else if (strcEQ(var, "HTTP_COOKIE"))
                result = apr_table_get(r->headers_in, "Cookie");
            else if (strcEQ(var, "HTTP_FORWARDED"))
                result = apr_table_get(r->headers_in, "Forwarded");
            else if (strcEQ(var, "HTTP_HOST"))
                result = apr_table_get(r->headers_in, "Host");
            else if (strcEQ(var, "HTTP_PROXY_CONNECTION"))
                result = apr_table_get(r->headers_in, "Proxy-Connection");
            else if (strcEQ(var, "HTTP_ACCEPT"))
                result = apr_table_get(r->headers_in, "Accept");
            else if (strlen(var) > 5 && strcEQn(var, "HTTP:", 5))
                /* all other headers from which we are still not know about */
                result = apr_table_get(r->headers_in, var+5);
            break;

        case 'R':
        case 'r':
            if (strcEQ(var, "REQUEST_METHOD"))
                result = r->method;
            else if (strcEQ(var, "REQUEST_SCHEME"))
                result = ap_http_scheme(r);
            else if (strcEQ(var, "REQUEST_URI"))
                result = r->uri;
            else if (strcEQ(var, "REQUEST_FILENAME"))
                result = r->filename;
            else if (strcEQ(var, "REMOTE_ADDR"))
                result = r->useragent_ip;
            else if (strcEQ(var, "REMOTE_HOST"))
                result = ap_get_useragent_host(r, REMOTE_NAME, NULL);
            else if (strcEQ(var, "REMOTE_IDENT"))
                result = ap_get_remote_logname(r);
            else if (strcEQ(var, "REMOTE_USER"))
                result = r->user;
            break;

        case 'S':
        case 's':
            if (strcEQn(var, "SSL", 3)) break; /* shortcut common case */

            if (strcEQ(var, "SERVER_ADMIN"))
                result = r->server->server_admin;
            else if (strcEQ(var, "SERVER_NAME"))
                result = ap_get_server_name_for_url(r);
            else if (strcEQ(var, "SERVER_PORT"))
                result = apr_psprintf(p, "%u", ap_get_server_port(r));
            else if (strcEQ(var, "SERVER_PROTOCOL"))
                result = r->protocol;
            else if (strcEQ(var, "SCRIPT_FILENAME"))
                result = r->filename;
            break;

        default:
            if (strcEQ(var, "PATH_INFO"))
                result = r->path_info;
            else if (strcEQ(var, "QUERY_STRING"))
                result = r->args;
            else if (strcEQ(var, "IS_SUBREQ"))
                result = (r->main != NULL ? "true" : "false");
            else if (strcEQ(var, "DOCUMENT_ROOT"))
                result = ap_document_root(r);
            else if (strcEQ(var, "AUTH_TYPE"))
                result = r->ap_auth_type;
            else if (strcEQ(var, "THE_REQUEST"))
                result = r->the_request;
            else if (strlen(var) > 4 && strcEQn(var, "ENV:", 4)) {
                result = apr_table_get(r->notes, var+4);
                if (result == NULL)
                    result = apr_table_get(r->subprocess_env, var+4);
            }
            break;
        }
    }

    /*
     * Connection stuff
     */
    if (result == NULL && c != NULL) {
        const SSLConnRec *sslconn = ssl_get_effective_config(c);
        if (strlen(var) > 4 && strcEQn(var, "SSL_", 4)
            && sslconn && sslconn->ssl)
            result = ssl_var_lookup_ssl(p, sslconn, r, var+4);
        else if (strcEQ(var, "HTTPS")) {
            if (sslconn && sslconn->ssl)
                result = "on";
            else
                result = "off";
        }
    }

    /*
     * Totally independent stuff
     */
    if (result == NULL) {
        if (strlen(var) > 12 && strcEQn(var, "SSL_VERSION_", 12))
            result = ssl_var_lookup_ssl_version(var+12);
        else if (strcEQ(var, "SERVER_SOFTWARE"))
            result = ap_get_server_banner();
        else if (strcEQ(var, "API_VERSION")) {
            result = apr_itoa(p, MODULE_MAGIC_NUMBER_MAJOR);
        }
        else if (strcEQ(var, "TIME_YEAR")) {
            apr_time_exp_lt(&tm, apr_time_now());
            result = apr_psprintf(p, "%02d%02d",
                                 (tm.tm_year / 100) + 19, tm.tm_year % 100);
        }
#define MKTIMESTR(format, tmfield) \
            apr_time_exp_lt(&tm, apr_time_now()); \
            result = apr_psprintf(p, format, tm.tmfield);
        else if (strcEQ(var, "TIME_MON")) {
            MKTIMESTR("%02d", tm_mon+1)
        }
        else if (strcEQ(var, "TIME_DAY")) {
            MKTIMESTR("%02d", tm_mday)
        }
        else if (strcEQ(var, "TIME_HOUR")) {
            MKTIMESTR("%02d", tm_hour)
        }
        else if (strcEQ(var, "TIME_MIN")) {
            MKTIMESTR("%02d", tm_min)
        }
        else if (strcEQ(var, "TIME_SEC")) {
            MKTIMESTR("%02d", tm_sec)
        }
        else if (strcEQ(var, "TIME_WDAY")) {
            MKTIMESTR("%d", tm_wday)
        }
        else if (strcEQ(var, "TIME")) {
            apr_time_exp_lt(&tm, apr_time_now());
            result = apr_psprintf(p,
                        "%02d%02d%02d%02d%02d%02d%02d", (tm.tm_year / 100) + 19,
                        (tm.tm_year % 100), tm.tm_mon+1, tm.tm_mday,
                        tm.tm_hour, tm.tm_min, tm.tm_sec);
        }
        /* all other env-variables from the parent Apache process */
        else if (strlen(var) > 4 && strcEQn(var, "ENV:", 4)) {
            result = getenv(var+4);
        }
    }

    return result ? result : "";
}

static const char *ssl_var_lookup_ssl(apr_pool_t *p, const SSLConnRec *sslconn,
                                      request_rec *r, const char *var)
{
    const char *result;
    X509 *xs;
    STACK_OF(X509) *sk;
    SSL *ssl;

    result = NULL;

    ssl = sslconn->ssl;
    if (strlen(var) > 8 && strcEQn(var, "VERSION_", 8)) {
        result = ssl_var_lookup_ssl_version(var+8);
    }
    else if (ssl != NULL && strcEQ(var, "PROTOCOL")) {
        result = SSL_get_version(ssl);
    }
    else if (ssl != NULL && strcEQ(var, "SESSION_ID")) {
        char buf[MODSSL_SESSION_ID_STRING_LEN];
        SSL_SESSION *pSession = SSL_get_session(ssl);
        if (pSession) {
            IDCONST unsigned char *id;
            unsigned int idlen;

#ifdef OPENSSL_NO_SSL_INTERN
            id = (unsigned char *)SSL_SESSION_get_id(pSession, &idlen);
#else
            id = pSession->session_id;
            idlen = pSession->session_id_length;
#endif

            result = apr_pstrdup(p, modssl_SSL_SESSION_id2sz(id, idlen,
                                                             buf, sizeof(buf)));
        }
    }
    else if(ssl != NULL && strcEQ(var, "SESSION_RESUMED")) {
        if (SSL_session_reused(ssl) == 1)
            result = "Resumed";
        else
            result = "Initial";
    }
    else if (ssl != NULL && strlen(var) >= 6 && strcEQn(var, "CIPHER", 6)) {
        result = ssl_var_lookup_ssl_cipher(p, sslconn, var+6);
    }
    else if (ssl != NULL && strlen(var) > 18 && strcEQn(var, "CLIENT_CERT_CHAIN_", 18)) {
        sk = SSL_get_peer_cert_chain(ssl);
        result = ssl_var_lookup_ssl_cert_chain(p, sk, var+18, 1);
    }
    else if (ssl != NULL && strlen(var) > 21 && strcEQn(var, "CLIENT_B64CERT_CHAIN_", 21)) {
        sk = SSL_get_peer_cert_chain(ssl);
        result = ssl_var_lookup_ssl_cert_chain(p, sk, var+21, 0);
    }
    else if (ssl != NULL && strcEQ(var, "CLIENT_CERT_RFC4523_CEA")) {
        result = ssl_var_lookup_ssl_cert_rfc4523_cea(p, ssl);
    }
    else if (ssl != NULL && strcEQ(var, "CLIENT_VERIFY")) {
        result = ssl_var_lookup_ssl_cert_verify(p, sslconn);
    }
    else if (ssl != NULL && strlen(var) > 7 && strcEQn(var, "CLIENT_", 7)) {
        if ((xs = SSL_get_peer_certificate(ssl)) != NULL) {
            result = ssl_var_lookup_ssl_cert(p, r, xs, var+7);
            X509_free(xs);
        }
    }
    else if (ssl != NULL && strlen(var) > 7 && strcEQn(var, "SERVER_", 7)) {
        if ((xs = SSL_get_certificate(ssl)) != NULL) {
            result = ssl_var_lookup_ssl_cert(p, r, xs, var+7);
            /* SSL_get_certificate is different from SSL_get_peer_certificate.
             * No need to X509_free(xs).
             */
        }
    }
    else if (ssl != NULL && strcEQ(var, "COMPRESS_METHOD")) {
        result = ssl_var_lookup_ssl_compress_meth(ssl);
    }
#ifdef HAVE_TLSEXT
    else if (ssl != NULL && strcEQ(var, "TLS_SNI")) {
        result = apr_pstrdup(p, SSL_get_servername(ssl,
                                                   TLSEXT_NAMETYPE_host_name));
    }
#endif
    else if (ssl != NULL && strcEQ(var, "SECURE_RENEG")) {
        int flag = 0;
#ifdef SSL_get_secure_renegotiation_support
        flag = SSL_get_secure_renegotiation_support(ssl);
#endif
        result = flag ? "true" : "false";
    }
#ifdef HAVE_SRP
    else if (ssl != NULL && strcEQ(var, "SRP_USER")) {
        if ((result = SSL_get_srp_username(ssl)) != NULL) {
            result = apr_pstrdup(p, result);
        }
    }
    else if (ssl != NULL && strcEQ(var, "SRP_USERINFO")) {
        if ((result = SSL_get_srp_userinfo(ssl)) != NULL) {
            result = apr_pstrdup(p, result);
        }
    }
#endif

    return result;
}

static const char *ssl_var_lookup_ssl_cert_dn_oneline(apr_pool_t *p, request_rec *r,
                                                X509_NAME *xsname)
{
    char *result = NULL;
    SSLDirConfigRec *dc;
    int legacy_format = 0;
    if (r) {
        dc = myDirConfig(r);
        legacy_format = dc->nOptions & SSL_OPT_LEGACYDNFORMAT;
    }
    if (legacy_format) {
        char *cp = X509_NAME_oneline(xsname, NULL, 0);
        result = apr_pstrdup(p, cp);
        OPENSSL_free(cp);
    }
    else {
        BIO* bio;
        unsigned long flags = XN_FLAG_RFC2253 & ~ASN1_STRFLGS_ESC_MSB;

        if ((bio = BIO_new(BIO_s_mem())) == NULL)
            return NULL;
        X509_NAME_print_ex(bio, xsname, 0, flags);

        result = modssl_bio_free_read(p, bio);
    }
    return result;
}

static const char *ssl_var_lookup_ssl_cert(apr_pool_t *p, request_rec *r, X509 *xs,
                                           const char *var)
{
    const char *result;
    X509_NAME *xsname;
    int nid;

    result = NULL;

    if (strcEQ(var, "M_VERSION")) {
        result = apr_psprintf(p, "%lu", X509_get_version(xs)+1);
    }
    else if (strcEQ(var, "M_SERIAL")) {
        result = ssl_var_lookup_ssl_cert_serial(p, xs);
    }
    else if (strcEQ(var, "V_START")) {
        result = ssl_var_lookup_ssl_cert_valid(p, X509_get_notBefore(xs));
    }
    else if (strcEQ(var, "V_END")) {
        result = ssl_var_lookup_ssl_cert_valid(p, X509_get_notAfter(xs));
    }
    else if (strcEQ(var, "V_REMAIN")) {
        result = ssl_var_lookup_ssl_cert_remain(p, X509_get_notAfter(xs));
    }
    else if (*var && strcEQ(var+1, "_DN")) {
        if (*var == 'S')
            xsname = X509_get_subject_name(xs);
        else if (*var == 'I')
            xsname = X509_get_issuer_name(xs);
        else
            return NULL;
        result = ssl_var_lookup_ssl_cert_dn_oneline(p, r, xsname);
    }
    else if (strlen(var) > 5 && strcEQn(var+1, "_DN_", 4)) {
        if (*var == 'S')
            xsname = X509_get_subject_name(xs);
        else if (*var == 'I')
            xsname = X509_get_issuer_name(xs);
        else
            return NULL;
        result = ssl_var_lookup_ssl_cert_dn(p, xsname, var+5);
    }
    else if (strlen(var) > 4 && strcEQn(var, "SAN_", 4)) {
        result = ssl_var_lookup_ssl_cert_san(p, xs, var+4);
    }
    else if (strcEQ(var, "A_SIG")) {
#if MODSSL_USE_OPENSSL_PRE_1_1_API
        nid = OBJ_obj2nid((ASN1_OBJECT *)(xs->cert_info->signature->algorithm));
#else
        const ASN1_OBJECT *paobj;
        X509_ALGOR_get0(&paobj, NULL, NULL, X509_get0_tbs_sigalg(xs));
        nid = OBJ_obj2nid(paobj);
#endif
        result = (nid == NID_undef) ? "UNKNOWN" : OBJ_nid2ln(nid);
    }
    else if (strcEQ(var, "A_KEY")) {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        nid = OBJ_obj2nid((ASN1_OBJECT *)(xs->cert_info->key->algor->algorithm));
#else
        ASN1_OBJECT *paobj;
        X509_PUBKEY_get0_param(&paobj, NULL, 0, NULL, X509_get_X509_PUBKEY(xs));
        nid = OBJ_obj2nid(paobj);
#endif
        result = (nid == NID_undef) ? "UNKNOWN" : OBJ_nid2ln(nid);
    }
    else if (strcEQ(var, "CERT")) {
        result = ssl_var_lookup_ssl_cert_data(p, xs, 1);
    }
    else if (strcEQ(var, "B64CERT")) {
        result = ssl_var_lookup_ssl_cert_data(p, xs, 0);
    }

    return result;
}

/* In this table, .extract is non-zero if RDNs using the NID should be
 * extracted to for the SSL_{CLIENT,SERVER}_{I,S}_DN_* environment
 * variables. */
static const struct {
    const char *name;
    int   nid;
    int   extract;
} ssl_var_lookup_ssl_cert_dn_rec[] = {
    { "C",     NID_countryName,            1 },
    { "ST",    NID_stateOrProvinceName,    1 }, /* officially    (RFC2156) */
    { "SP",    NID_stateOrProvinceName,    0 }, /* compatibility (SSLeay)  */
    { "L",     NID_localityName,           1 },
    { "O",     NID_organizationName,       1 },
    { "OU",    NID_organizationalUnitName, 1 },
    { "CN",    NID_commonName,             1 },
    { "T",     NID_title,                  1 },
    { "I",     NID_initials,               1 },
    { "G",     NID_givenName,              1 },
    { "S",     NID_surname,                1 },
    { "D",     NID_description,            1 },
#ifdef NID_userId
    { "UID",   NID_userId,                 1 },
#endif
    { "Email", NID_pkcs9_emailAddress,     1 },
    { NULL,    0,                          0 }
};

static const char *ssl_var_lookup_ssl_cert_dn(apr_pool_t *p, X509_NAME *xsname,
                                        const char *var)
{
    const char *ptr;
    const char *result;
    X509_NAME_ENTRY *xsne;
    int i, j, n, idx = 0, raw = 0;
    apr_size_t varlen;

    ptr = ap_strrchr_c(var, '_');
    if (ptr && ptr > var && strcmp(ptr + 1, "RAW") == 0) {
        var = apr_pstrmemdup(p, var, ptr - var);
        raw = 1;
    }
    
    /* if an _N suffix is used, find the Nth attribute of given name */
    ptr = ap_strchr_c(var, '_');
    if (ptr != NULL && strspn(ptr + 1, "0123456789") == strlen(ptr + 1)) {
        idx = atoi(ptr + 1);
        varlen = ptr - var;
    } else {
        varlen = strlen(var);
    }

    result = NULL;

    for (i = 0; ssl_var_lookup_ssl_cert_dn_rec[i].name != NULL; i++) {
        if (strEQn(var, ssl_var_lookup_ssl_cert_dn_rec[i].name, varlen)
            && strlen(ssl_var_lookup_ssl_cert_dn_rec[i].name) == varlen) {
            for (j = 0; j < X509_NAME_entry_count(xsname); j++) {
                xsne = X509_NAME_get_entry(xsname, j);

                n =OBJ_obj2nid((ASN1_OBJECT *)X509_NAME_ENTRY_get_object(xsne));

                if (n == ssl_var_lookup_ssl_cert_dn_rec[i].nid && idx-- == 0) {
                    result = modssl_X509_NAME_ENTRY_to_string(p, xsne, raw);
                    break;
                }
            }
            break;
        }
    }
    return result;
}

static const char *ssl_var_lookup_ssl_cert_san(apr_pool_t *p, X509 *xs, const char *var)
{
    int type;
    apr_size_t numlen;
    const char *onf = NULL;
    apr_array_header_t *entries;

    if (strcEQn(var, "Email_", 6)) {
        type = GEN_EMAIL;
        var += 6;
    }
    else if (strcEQn(var, "DNS_", 4)) {
        type = GEN_DNS;
        var += 4;
    }
    else if (strcEQn(var, "OTHER_", 6)) {
        type = GEN_OTHERNAME;
        var += 6;
        if (strEQn(var, "msUPN_", 6)) {
            var += 6;
            onf = "msUPN";
        }
        else if (strEQn(var, "dnsSRV_", 7)) {
            var += 7;
            onf = "id-on-dnsSRV";
        }
        else
           return NULL;
    }
    else
        return NULL;

    /* sanity check: number must be between 1 and 4 digits */
    numlen = strspn(var, "0123456789");
    if ((numlen < 1) || (numlen > 4) || (numlen != strlen(var)))
        return NULL;

    if (modssl_X509_getSAN(p, xs, type, onf, atoi(var), &entries))
        /* return the first entry from this 1-element array */
        return APR_ARRAY_IDX(entries, 0, char *);
    else
        return NULL;
}

static const char *ssl_var_lookup_ssl_cert_valid(apr_pool_t *p, ASN1_TIME *tm)
{
    BIO* bio;

    if ((bio = BIO_new(BIO_s_mem())) == NULL)
        return NULL;
    ASN1_TIME_print(bio, tm);

    return modssl_bio_free_read(p, bio);
}

#define DIGIT2NUM(x) (((x)[0] - '0') * 10 + (x)[1] - '0')

/* Return a string giving the number of days remaining until 'tm', or
 * "0" if this can't be determined. */
static const char *ssl_var_lookup_ssl_cert_remain(apr_pool_t *p, ASN1_TIME *tm)
{
    apr_time_t then, now = apr_time_now();
    apr_time_exp_t exp = {0};
    long diff;
    unsigned char *dp;

    /* Fail if the time isn't a valid ASN.1 TIME; RFC3280 mandates
     * that the seconds digits are present even though ASN.1
     * doesn't. */
    if ((tm->type == V_ASN1_UTCTIME && tm->length < 11) ||
        (tm->type == V_ASN1_GENERALIZEDTIME && tm->length < 13) ||
        !ASN1_TIME_check(tm)) {
        return "0";
    }

    if (tm->type == V_ASN1_UTCTIME) {
        exp.tm_year = DIGIT2NUM(tm->data);
        if (exp.tm_year <= 50) exp.tm_year += 100;
        dp = tm->data + 2;
    } else {
        exp.tm_year = DIGIT2NUM(tm->data) * 100 + DIGIT2NUM(tm->data + 2) - 1900;
        dp = tm->data + 4;
    }

    exp.tm_mon = DIGIT2NUM(dp) - 1;
    exp.tm_mday = DIGIT2NUM(dp + 2) + 1;
    exp.tm_hour = DIGIT2NUM(dp + 4);
    exp.tm_min = DIGIT2NUM(dp + 6);
    exp.tm_sec = DIGIT2NUM(dp + 8);

    if (apr_time_exp_gmt_get(&then, &exp) != APR_SUCCESS) {
        return "0";
    }

    diff = (long)((apr_time_sec(then) - apr_time_sec(now)) / (60*60*24));

    return diff > 0 ? apr_ltoa(p, diff) : "0";
}

static const char *ssl_var_lookup_ssl_cert_serial(apr_pool_t *p, X509 *xs)
{
    BIO *bio;

    if ((bio = BIO_new(BIO_s_mem())) == NULL)
        return NULL;
    i2a_ASN1_INTEGER(bio, X509_get_serialNumber(xs));

    return modssl_bio_free_read(p, bio);
}

static const char *ssl_var_lookup_ssl_cert_chain(apr_pool_t *p, STACK_OF(X509) *sk,
                                                 const char *var, int pem)
{
    const char *result;
    X509 *xs;
    int n;

    result = NULL;

    if (strspn(var, "0123456789") == strlen(var)) {
        n = atoi(var);
        if (n < sk_X509_num(sk)) {
            xs = sk_X509_value(sk, n);
            result = ssl_var_lookup_ssl_cert_data(p, xs, pem);
        }
    }

    return result;
}

static const char *ssl_var_lookup_ssl_cert_rfc4523_cea(apr_pool_t *p, SSL *ssl)
{
    char *result;
    X509 *xs;

    ASN1_INTEGER *serialNumber;

    if (!(xs = SSL_get_peer_certificate(ssl))) {
        return NULL;
    }

    result = NULL;

    serialNumber = X509_get_serialNumber(xs);
    if (serialNumber) {
        X509_NAME *issuer = X509_get_issuer_name(xs);
        if (issuer) {
            BIGNUM *bn = ASN1_INTEGER_to_BN(serialNumber, NULL);
            char *decimal = BN_bn2dec(bn);
            result = apr_pstrcat(p, "{ serialNumber ", decimal,
                    ", issuer rdnSequence:\"",
                    modssl_X509_NAME_to_string(p, issuer, 0), "\" }", NULL);
            OPENSSL_free(decimal);
            BN_free(bn);
        }
    }

    X509_free(xs);
    return result;
}

static const char *ssl_var_lookup_ssl_cert_verify(apr_pool_t *p,
                                                  const SSLConnRec *sslconn)
{
    char *result;
    long vrc;
    const char *verr;
    const char *vinfo;
    SSL *ssl;
    X509 *xs;

    result = NULL;
    ssl   = sslconn->ssl;
    verr  = sslconn->verify_error;
    vinfo = sslconn->verify_info;
    vrc   = SSL_get_verify_result(ssl);
    xs    = SSL_get_peer_certificate(ssl);

    if (vrc == X509_V_OK && verr == NULL && xs == NULL)
        /* no client verification done at all */
        result = "NONE";
    else if (vrc == X509_V_OK && verr == NULL && vinfo == NULL && xs != NULL)
        /* client verification done successful */
        result = "SUCCESS";
    else if (vrc == X509_V_OK && vinfo != NULL && strEQ(vinfo, "GENEROUS"))
        /* client verification done in generous way */
        result = "GENEROUS";
    else
        /* client verification failed */
        result = apr_psprintf(p, "FAILED:%s",
                              verr ? verr : X509_verify_cert_error_string(vrc));

    if (xs)
        X509_free(xs);
    return result;
}

static const char *ssl_var_lookup_ssl_cipher(apr_pool_t *p,
                                             const SSLConnRec *sslconn,
                                             const char *var)
{
    const char *result;
    int usekeysize, algkeysize;
    SSL *ssl;

    result = NULL;

    ssl = sslconn->ssl;
    ssl_var_lookup_ssl_cipher_bits(ssl, &usekeysize, &algkeysize);

    if (ssl && strEQ(var, "")) {
        MODSSL_SSL_CIPHER_CONST SSL_CIPHER *cipher = SSL_get_current_cipher(ssl);

        if (cipher) {
            result = apr_pstrdup(p, SSL_CIPHER_get_name(cipher));
        }
    }
    else if (strcEQ(var, "_EXPORT"))
        result = (usekeysize < 56 ? "true" : "false");
    else if (strcEQ(var, "_USEKEYSIZE")) {
        result = apr_itoa(p, usekeysize);
    }
    else if (strcEQ(var, "_ALGKEYSIZE")) {
        result = apr_itoa(p, algkeysize);
    }

    return result;
}

static void ssl_var_lookup_ssl_cipher_bits(SSL *ssl, int *usekeysize, int *algkeysize)
{
    MODSSL_SSL_CIPHER_CONST SSL_CIPHER *cipher;

    *usekeysize = 0;
    *algkeysize = 0;
    if (ssl != NULL)
        if ((cipher = SSL_get_current_cipher(ssl)) != NULL)
            *usekeysize = SSL_CIPHER_get_bits(cipher, algkeysize);
    return;
}

static const char *ssl_var_lookup_ssl_version(const char *var)
{
    if (strEQ(var, "INTERFACE")) {
        return var_interface;
    }
    else if (strEQ(var, "LIBRARY_INTERFACE")) {
        return var_library_interface;
    }
    else if (strEQ(var, "LIBRARY")) {
        return var_library;
    }
    return NULL;
}

/* Add each RDN in 'xn' to the table 't' where the NID is present in
 * 'nids', using key prefix 'pfx'.  */
static void extract_dn(apr_table_t *t, apr_hash_t *nids, const char *pfx,
                       X509_NAME *xn, apr_pool_t *p)
{
    X509_NAME_ENTRY *xsne;
    apr_hash_t *count;
    int i, nid;

    /* Hash of (int) NID -> (int *) counter to count each time an RDN
     * with the given NID has been seen. */
    count = apr_hash_make(p);

    /* For each RDN... */
    for (i = 0; i < X509_NAME_entry_count(xn); i++) {
         const char *tag;
         xsne = X509_NAME_get_entry(xn, i);

         /* Retrieve the nid, and check whether this is one of the nids
          * which are to be extracted. */
         nid = OBJ_obj2nid((ASN1_OBJECT *)X509_NAME_ENTRY_get_object(xsne));

         tag = apr_hash_get(nids, &nid, sizeof nid);
         if (tag) {
             const char *key;
             int *dup;
             char *value;

             /* Check whether a variable with this nid was already
              * been used; if so, use the foo_N=bar syntax. */
             dup = apr_hash_get(count, &nid, sizeof nid);
             if (dup) {
                 key = apr_psprintf(p, "%s%s_%d", pfx, tag, ++(*dup));
             }
             else {
                 /* Otherwise, use the plain foo=bar syntax. */
                 dup = apr_pcalloc(p, sizeof *dup);
                 apr_hash_set(count, &nid, sizeof nid, dup);
                 key = apr_pstrcat(p, pfx, tag, NULL);
             }
             value = modssl_X509_NAME_ENTRY_to_string(p, xsne, 0);
             apr_table_setn(t, key, value);
         }
    }
}

void modssl_var_extract_dns(apr_table_t *t, SSL *ssl, apr_pool_t *p)
{
    apr_hash_t *nids;
    unsigned n;
    X509 *xs;

    /* Build up a hash table of (int *)NID->(char *)short-name for all
     * the tags which are to be extracted: */
    nids = apr_hash_make(p);
    for (n = 0; ssl_var_lookup_ssl_cert_dn_rec[n].name; n++) {
        if (ssl_var_lookup_ssl_cert_dn_rec[n].extract) {
            apr_hash_set(nids, &ssl_var_lookup_ssl_cert_dn_rec[n].nid,
                         sizeof(ssl_var_lookup_ssl_cert_dn_rec[0].nid),
                         ssl_var_lookup_ssl_cert_dn_rec[n].name);
        }
    }

    /* Extract the server cert DNS -- note that the refcount does NOT
     * increase: */
    xs = SSL_get_certificate(ssl);
    if (xs) {
        extract_dn(t, nids, "SSL_SERVER_S_DN_", X509_get_subject_name(xs), p);
        extract_dn(t, nids, "SSL_SERVER_I_DN_", X509_get_issuer_name(xs), p);
    }

    /* Extract the client cert DNs -- note that the refcount DOES
     * increase: */
    xs = SSL_get_peer_certificate(ssl);
    if (xs) {
        extract_dn(t, nids, "SSL_CLIENT_S_DN_", X509_get_subject_name(xs), p);
        extract_dn(t, nids, "SSL_CLIENT_I_DN_", X509_get_issuer_name(xs), p);
        X509_free(xs);
    }
}

static void extract_san_array(apr_table_t *t, const char *pfx,
                              apr_array_header_t *entries, apr_pool_t *p)
{
    int i;

    for (i = 0; i < entries->nelts; i++) {
        const char *key = apr_psprintf(p, "%s_%d", pfx, i);
        apr_table_setn(t, key, APR_ARRAY_IDX(entries, i, const char *));
    }
}

void modssl_var_extract_san_entries(apr_table_t *t, SSL *ssl, apr_pool_t *p)
{
    X509 *xs;
    apr_array_header_t *entries;

    /* subjectAltName entries of the server certificate */
    xs = SSL_get_certificate(ssl);
    if (xs) {
        if (modssl_X509_getSAN(p, xs, GEN_EMAIL, NULL, -1, &entries)) {
            extract_san_array(t, "SSL_SERVER_SAN_Email", entries, p);
        }
        if (modssl_X509_getSAN(p, xs, GEN_DNS, NULL, -1, &entries)) {
            extract_san_array(t, "SSL_SERVER_SAN_DNS", entries, p);
        }
        if (modssl_X509_getSAN(p, xs, GEN_OTHERNAME, "id-on-dnsSRV", -1,
                               &entries)) {
            extract_san_array(t, "SSL_SERVER_SAN_OTHER_dnsSRV", entries, p);
        }
        /* no need to free xs (refcount does not increase) */
    }

    /* subjectAltName entries of the client certificate */
    xs = SSL_get_peer_certificate(ssl);
    if (xs) {
        if (modssl_X509_getSAN(p, xs, GEN_EMAIL, NULL, -1, &entries)) {
            extract_san_array(t, "SSL_CLIENT_SAN_Email", entries, p);
        }
        if (modssl_X509_getSAN(p, xs, GEN_DNS, NULL, -1, &entries)) {
            extract_san_array(t, "SSL_CLIENT_SAN_DNS", entries, p);
        }
        if (modssl_X509_getSAN(p, xs, GEN_OTHERNAME, "msUPN", -1, &entries)) {
            extract_san_array(t, "SSL_CLIENT_SAN_OTHER_msUPN", entries, p);
        }
        X509_free(xs);
    }
}

/* For an extension type which OpenSSL does not recognize, attempt to
 * parse the extension type as a primitive string.  This will fail for
 * any structured extension type per the docs.  Returns non-zero on
 * success and writes the string to the given bio. */
static int dump_extn_value(BIO *bio, ASN1_OCTET_STRING *str)
{
    const unsigned char *pp = str->data;
    ASN1_STRING *ret = ASN1_STRING_new();
    int rv = 0;

    /* This allows UTF8String, IA5String, VisibleString, or BMPString;
     * conversion to UTF-8 is forced. */
    if (d2i_DISPLAYTEXT(&ret, &pp, str->length)) {
        ASN1_STRING_print_ex(bio, ret, ASN1_STRFLGS_UTF8_CONVERT);
        rv = 1;
    }

    ASN1_STRING_free(ret);
    return rv;
}

apr_array_header_t *ssl_ext_list(apr_pool_t *p, conn_rec *c, int peer,
                                 const char *extension)
{
    const SSLConnRec *sslconn = ssl_get_effective_config(c);
    SSL *ssl = NULL;
    apr_array_header_t *array = NULL;
    X509 *xs = NULL;
    ASN1_OBJECT *oid = NULL;
    int count = 0, j;

    if (!sslconn || !sslconn->ssl || !extension) {
        return NULL;
    }
    ssl = sslconn->ssl;

    /* We accept the "extension" string to be converted as
     * a long name (nsComment), short name (DN) or
     * numeric OID (1.2.3.4).
     */
    oid = OBJ_txt2obj(extension, 0);
    if (!oid) {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, c, APLOGNO(01970)
                      "could not parse OID '%s'", extension);
        ERR_clear_error();
        return NULL;
    }

    xs = peer ? SSL_get_peer_certificate(ssl) : SSL_get_certificate(ssl);
    if (xs == NULL) {
        return NULL;
    }

    count = X509_get_ext_count(xs);
    /* Create an array large enough to accommodate every extension. This is
     * likely overkill, but safe.
     */
    array = apr_array_make(p, count, sizeof(char *));
    for (j = 0; j < count; j++) {
        X509_EXTENSION *ext = X509_get_ext(xs, j);

        if (OBJ_cmp(X509_EXTENSION_get_object(ext), oid) == 0) {
            BIO *bio = BIO_new(BIO_s_mem());

            /* We want to obtain a string representation of the extensions
             * value and add it to the array we're building.
             * X509V3_EXT_print() doesn't know about all the possible
             * data types, but the value is stored as an ASN1_OCTET_STRING
             * allowing us a fallback in case of X509V3_EXT_print
             * not knowing how to handle the data.
             */
            if (X509V3_EXT_print(bio, ext, 0, 0) == 1 ||
                dump_extn_value(bio, X509_EXTENSION_get_data(ext)) == 1) {
                BUF_MEM *buf;
                char **ptr = apr_array_push(array);
                BIO_get_mem_ptr(bio, &buf);
                *ptr = apr_pstrmemdup(p, buf->data, buf->length);
            } else {
                ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, c, APLOGNO(01971)
                              "Found an extension '%s', but failed to "
                              "create a string from it", extension);
            }
            BIO_vfree(bio);
        }
    }

    if (array->nelts == 0)
        array = NULL;

    if (peer) {
        /* only SSL_get_peer_certificate raises the refcount */
        X509_free(xs);
    }

    ASN1_OBJECT_free(oid);
    ERR_clear_error();
    return array;
}

static const char *ssl_var_lookup_ssl_compress_meth(SSL *ssl)
{
    const char *result = "NULL";
#ifndef OPENSSL_NO_COMP
    SSL_SESSION *pSession = SSL_get_session(ssl);

    if (pSession) {
#ifdef OPENSSL_NO_SSL_INTERN
        switch (SSL_SESSION_get_compress_id(pSession)) {
#else
        switch (pSession->compress_meth) {
#endif
        case 0:
            /* default "NULL" already set */
            break;

            /* Defined by RFC 3749, deflate is coded by "1" */
        case 1:
            result = "DEFLATE";
            break;

            /* IANA assigned compression number for LZS */
        case 0x40:
            result = "LZS";
            break;

        default:
            result = "UNKNOWN";
            break;
        }
    }
#endif
    return result;
}

/*  _________________________________________________________________
**
**  SSL Extension to mod_log_config
**  _________________________________________________________________
*/

#include "../../modules/loggers/mod_log_config.h"

static const char *ssl_var_log_handler_c(request_rec *r, char *a);
static const char *ssl_var_log_handler_x(request_rec *r, char *a);

/*
 * register us for the mod_log_config function registering phase
 * to establish %{...}c and to be able to expand %{...}x variables.
 */
void ssl_var_log_config_register(apr_pool_t *p)
{
    APR_OPTIONAL_FN_TYPE(ap_register_log_handler) *log_pfn_register;

    log_pfn_register = APR_RETRIEVE_OPTIONAL_FN(ap_register_log_handler);

    if (log_pfn_register) {
        log_pfn_register(p, "c", ssl_var_log_handler_c, 0);
        log_pfn_register(p, "x", ssl_var_log_handler_x, 0);
    }
    return;
}

/*
 * implement the %{..}c log function
 * (we are the only function)
 */
static const char *ssl_var_log_handler_c(request_rec *r, char *a)
{
    const SSLConnRec *sslconn = ssl_get_effective_config(r->connection);
    const char *result;

    if (sslconn == NULL || sslconn->ssl == NULL)
        return NULL;
    result = NULL;
    if (strEQ(a, "version"))
        result = ssl_var_lookup(r->pool, r->server, r->connection, r, "SSL_PROTOCOL");
    else if (strEQ(a, "cipher"))
        result = ssl_var_lookup(r->pool, r->server, r->connection, r, "SSL_CIPHER");
    else if (strEQ(a, "subjectdn") || strEQ(a, "clientcert"))
        result = ssl_var_lookup(r->pool, r->server, r->connection, r, "SSL_CLIENT_S_DN");
    else if (strEQ(a, "issuerdn") || strEQ(a, "cacert"))
        result = ssl_var_lookup(r->pool, r->server, r->connection, r, "SSL_CLIENT_I_DN");
    else if (strEQ(a, "errcode"))
        result = "-";
    else if (strEQ(a, "errstr"))
        result = sslconn->verify_error;
    if (result != NULL && result[0] == NUL)
        result = NULL;
    return result;
}

/*
 * extend the implementation of the %{..}x log function
 * (there can be more functions)
 */
static const char *ssl_var_log_handler_x(request_rec *r, char *a)
{
    const char *result;

    result = ssl_var_lookup(r->pool, r->server, r->connection, r, a);
    if (result != NULL && result[0] == NUL)
        result = NULL;
    return result;
}


