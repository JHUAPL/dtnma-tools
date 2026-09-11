/*
 * Copyright (c) 2011-2026 The Johns Hopkins University Applied Physics
 * Laboratory LLC.
 *
 * This file is part of the Delay-Tolerant Networking Management
 * Architecture (DTNMA) Tools package.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "nm_rest.h"

#include "cace/ari/cbor.h"
#include "cace/ari/text.h"
#include "cace/ari/text_util.h"
#include "cace/util/defs.h"
#include "cace/util/logging.h"
#include "cace/util/mutex.h"

#include <m-bstring.h>
// CivetWeb includes
#include <civetweb.h>
#include <cjson/cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#if POSTGRESQL_FOUND
#include "nm_sql.h"
#endif

/// Chunking size for receiving request bodies
#define REQUEST_BODY_CHUNK 4096

#define BASE_API_URI      "/nm/api"
#define AGENTS_IDX_PREFIX BASE_API_URI "/agents/idx/"
#define AGENTS_EID_PREFIX BASE_API_URI "/agents/eid/"

static bool isContentType(const char *ctype, const char *match)
{
    if (!ctype)
    {
        return false;
    }
    size_t ctype_len = strlen(ctype);
    size_t match_len = strlen(match);
    if (0 != strncasecmp(ctype, match, match_len))
    {
        return false;
    }
    if (ctype_len > match_len)
    {
        if (ctype[match_len] != ';')
        {
            return false;
        }
    }
    return true;
}

static int readRequstBody(struct mg_connection *conn, m_bstring_t data)
{
    uint8_t chunk[REQUEST_BODY_CHUNK];

    while (true)
    {
        int got = mg_read(conn, chunk, sizeof(chunk));
        if (got == 0)
        {
            break;
        }
        else if (got < 0)
        {
            return HTTP_BAD_REQUEST;
        }
        m_bstring_push_back_bytes(data, got, chunk);
    }
    return 0;
}

/*** Start Common API Functions ***/
static size_t SendJSON(struct mg_connection *conn, const cJSON *json_obj)
{
    char  *json_str     = cJSON_PrintUnformatted(json_obj);
    size_t json_str_len = strlen(json_str);

    /* Send HTTP message header */
    mg_send_http_ok(conn, "application/json; charset=utf-8", json_str_len);

    /* Send HTTP message content */
    mg_write(conn, json_str, json_str_len);

    /* Free string allocated by cJSON_Print* */
    cJSON_free(json_str);

    return json_str_len;
}

static int log_message(const struct mg_connection *conn _U_, const char *message)
{
    CACE_LOG_INFO("%s", message);
    return 1;
}

/*** Start REST API Handlers ***/

static int versionHandler(struct mg_connection *conn, void *cbdata _U_)
{
    static const char *allow_val = "OPTIONS,GET";

    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (0 == strcasecmp(ri->request_method, "OPTIONS"))
    {
        mg_response_header_start(conn, HTTP_NO_CONTENT);
        mg_response_header_add(conn, "Allow", allow_val, -1);
        mg_response_header_send(conn);
        return HTTP_NO_CONTENT;
    }
    else if (0 == strcasecmp(ri->request_method, "GET"))
    {
        cJSON *obj = cJSON_CreateObject();
        if (!obj)
        {
            /* insufficient memory? */
            mg_send_http_error(conn, HTTP_INTERNAL_ERROR, "Server error");
            return HTTP_INTERNAL_ERROR;
        }

        cJSON_AddStringToObject(obj, "civetweb_version", CIVETWEB_VERSION);
        cJSON_AddStringToObject(obj, "ari_version", "draft-ietf-dtn-ari-09");
        cJSON_AddStringToObject(obj, "amp_version", "draft-ietf-dtn-amp-04");
        cJSON_AddStringToObject(obj, "build_date", __DATE__);
        cJSON_AddStringToObject(obj, "build_time", __TIME__);
        SendJSON(conn, obj);
        cJSON_Delete(obj);

        return HTTP_OK;
    }
    else
    {
        mg_response_header_start(conn, HTTP_METHOD_NOT_ALLOWED);
        mg_response_header_add(conn, "Allow", allow_val, -1);
        mg_response_header_send(conn);
        return HTTP_METHOD_NOT_ALLOWED;
    }
}

static int agentsGetHandler(struct mg_connection *conn)
{
    refdm_mgr_t *mgr = mg_get_user_data(mg_get_context(conn));

    cJSON *obj = cJSON_CreateObject();
    if (!obj)
    {
        /* insufficient memory? */
        mg_send_http_error(conn, HTTP_INTERNAL_ERROR, "Server error");
        return HTTP_INTERNAL_ERROR;
    }

    cJSON *agentList = cJSON_AddArrayToObject(obj, "agents");
    if (agentList == NULL)
    {
        cJSON_Delete(obj);
        mg_send_http_error(conn, HTTP_INTERNAL_ERROR, "Server error");
        return HTTP_INTERNAL_ERROR;
    }

    CACE_MUTEX_LOCK(&mgr->agent_mutex);
    refdm_agent_list_it_t agent_it;
    for (refdm_agent_list_it(agent_it, mgr->agent_list); !refdm_agent_list_end_p(agent_it);
         refdm_agent_list_next(agent_it))
    {
        const refdm_agent_t *agent = *refdm_agent_list_ref(agent_it);

        cJSON *agentObj = cJSON_CreateObject();
        cJSON_AddStringToObject(agentObj, "name", m_string_get_cstr(agent->eid));
        {
#if POSTGRESQL_FOUND
            int32_t idx = refdm_db_fetch_agent_idx(m_string_get_cstr(agent->eid));

            size_t count;
            int    ecode = refdm_db_fetch_rptset_count(idx, &count);
            if (ecode != RET_PASS)
            {
                CACE_LOG_ERR("Failed to obtain RPTSET count for %s", m_string_get_cstr(agent->eid));
            }
            else
            {
                cJSON_AddNumberToObject(agentObj, "rpts_count", count);
            }
#else
            size_t count = cace_ari_list_size(agent->rptsets);
            cJSON_AddNumberToObject(agentObj, "rpts_count", count);
#endif
        }
        cJSON_AddItemToArray(agentList, agentObj);
    }
    CACE_MUTEX_UNLOCK(&mgr->agent_mutex);

    SendJSON(conn, obj);
    cJSON_Delete(obj);

    return HTTP_OK;
}

/// Sanity-check URI structure
static bool isEidValid(const m_string_t eid)
{
    size_t sep = m_string_search_char(eid, ':', 0);
    // missing, scheme less than 1 byte, or SSP less than 1 byte
    if ((sep == M_STRING_FAILURE) || (sep < 1) || (m_string_size(eid) - sep <= 1))
    {
        return false;
    }

    // valid scheme characters
    for (size_t ix = 0; ix < sep; ++ix)
    {
        const char schar = m_string_get_char(eid, ix);
        if (!(isalpha(schar) || isdigit(schar) || (schar == '+') || (schar == '-') || (schar == '.')))
        {
            return false;
        }
    }

    return true;
}

// This may be called via POST /agents
static int agentsPostHandler(struct mg_connection *conn)
{
    const char *ctype = mg_get_header(conn, "content-type");
    if (!isContentType(ctype, "text/plain"))
    {
        mg_send_http_error(conn, HTTP_UNSUP_MEDIA_TYPE, "Only text/plain supported");
        return HTTP_UNSUP_MEDIA_TYPE;
    }

    // Request body contains direct EID text
    m_bstring_t body;
    m_bstring_init(body);
    int retval = readRequstBody(conn, body);

    m_string_t eid;
    m_string_init(eid);
    if (!retval)
    {
        // guarantee null terminator for text conversion
        m_bstring_push_back(body, '\0');

        m_string_set_cstr(eid, (const char *)m_bstring_view(body, 0, m_bstring_size(body)));
        m_string_strim(eid);

        if (!isEidValid(eid))
        {
            mg_send_http_error(conn, HTTP_UNPROCESSABLE_CNT, "Invalid URI in body");
            retval = HTTP_UNPROCESSABLE_CNT;
        }
    }

    bool is_new = false;
    if (!retval)
    {
        refdm_mgr_t *mgr = mg_get_user_data(mg_get_context(conn));

        refdm_agent_t *agent = refdm_mgr_agent_add(mgr, m_string_get_cstr(eid), &is_new);
        if (!agent)
        {
            mg_send_http_error(conn, HTTP_INTERNAL_ERROR, "Unable to register agent");
            retval = HTTP_INTERNAL_ERROR;
        }
    }

    if (!retval)
    { // the agent is valid
        retval = is_new ? HTTP_CREATED : HTTP_SEE_OTHER;

        mg_response_header_start(conn, retval);
        {
            size_t perc_eid_len = 3 * m_string_size(eid) + 1;
            char   perc_eid[perc_eid_len];
            memset(perc_eid, 0, perc_eid_len);
            mg_url_encode(m_string_get_cstr(eid), perc_eid, perc_eid_len);

            m_string_t url;
            m_string_init_printf(url, "./agents/eid/%s/", perc_eid);
            mg_response_header_add(conn, "Location", m_string_get_cstr(url), (int)m_string_size(url));
            m_string_clear(url);
        }
        mg_response_header_send(conn);
    }

    m_string_clear(eid);
    m_bstring_clear(body);
    return retval;
}

static int agentsHandler(struct mg_connection *conn, void *cbdata _U_)
{
    static const char *allow_val = "OPTIONS,GET,POST";

    const struct mg_request_info *ri = mg_get_request_info(conn);
    CACE_LOG_DEBUG("Handling %s to %s", ri->request_method, ri->local_uri);

    if (0 == strcasecmp(ri->request_method, "OPTIONS"))
    {
        mg_response_header_start(conn, HTTP_NO_CONTENT);
        mg_response_header_add(conn, "Allow", allow_val, -1);
        mg_response_header_send(conn);
        return HTTP_NO_CONTENT;
    }
    else if (0 == strcasecmp(ri->request_method, "GET"))
    {
        return agentsGetHandler(conn);
    }
    else if (0 == strcasecmp(ri->request_method, "POST"))
    {
        return agentsPostHandler(conn);
    }
    else
    {
        mg_response_header_start(conn, HTTP_METHOD_NOT_ALLOWED);
        mg_response_header_add(conn, "Allow", allow_val, -1);
        mg_response_header_send(conn);
        return HTTP_METHOD_NOT_ALLOWED;
    }
}

static int agentParseCbor(struct mg_connection *conn, cace_ari_list_t tosend)
{
    int retval = 0;

    // Request body contains binary-encoded ARIs concatenated
    m_bstring_t body;
    m_bstring_init(body);
    retval = readRequstBody(conn, body);

    if (!retval)
    {
        // iterate until all read or an error occurs
        size_t curs   = 0;
        size_t remain = m_bstring_size(body);

        while (remain > 0)
        {
            size_t used = 0;
            char  *errm = NULL;

            cace_ari_t *item = cace_ari_list_push_back_new(tosend);

            cace_data_t databuf;
            cace_data_init_view(&databuf, remain, (cace_data_ptr_t)m_bstring_view(body, curs, remain));

            int res = cace_ari_cbor_decode(item, &databuf, &used, &errm);
            cace_data_deinit(&databuf);
            if (res)
            {
                mg_send_http_error(conn, HTTP_BAD_REQUEST, "Error decoding execution ARI: %s", errm);
                retval = HTTP_BAD_REQUEST;

                CACE_FREE(errm);
                errm = NULL;
                break;
            }
            if (cace_log_is_enabled_for(LOG_DEBUG))
            {
                m_string_t buf;
                m_string_init(buf);
                cace_ari_text_encode(buf, item, CACE_ARI_TEXT_ENC_OPTS_DEFAULT);
                CACE_LOG_DEBUG("decoded ARI as %s", m_string_get_cstr(buf));
                m_string_clear(buf);
            }

            if (!cace_ari_is_lit_typed(item, CACE_ARI_TYPE_EXECSET))
            {
                mg_send_http_error(conn, HTTP_BAD_REQUEST, "One value is not an EXECSET");
                retval = HTTP_BAD_REQUEST;
            }

            curs += used;
            remain -= used;
        }
    }

    m_bstring_clear(body);
    return retval;
}

static int agentParseHex(struct mg_connection *conn, cace_ari_list_t tosend)
{
    int retval = 0;
    int res;

    // Request body contains hex-encoded binary-encoded ARIs as lines
    m_bstring_t body;
    m_bstring_init(body);
    retval = readRequstBody(conn, body);

    if (!retval)
    {
        m_bstring_push_back(body, '\0');

        m_string_t hexbuf;
        m_string_init(hexbuf);
        cace_data_t databuf;
        cace_data_init(&databuf);
        char *errm = NULL;

        static const char *arisep = " \f\n\r\t\v"; // Identical to isspace()

        const size_t body_len = m_bstring_size(body);
        const char  *curs     = (const char *)m_bstring_view(body, 0, body_len);
        const char  *end      = curs + body_len;
        while (!retval && (curs < end))
        {
            size_t part_len = strcspn(curs, arisep);
            if (part_len == 0)
            {
                break;
            }
            // skip over optional prefix
            if (strncasecmp(curs, "0x", 2) == 0)
            {
                curs += 2;
                part_len -= 2;
            }

            m_string_set_cstrn(hexbuf, curs, part_len);
            CACE_LOG_DEBUG("Handling message part %s", m_string_get_cstr(hexbuf));

            // replace the databuf contents
            res = cace_base16_decode(&databuf, hexbuf);
            if (res)
            {
                mg_send_http_error(conn, HTTP_BAD_REQUEST, "One input line does not contain base-16 encoded text: %s",
                                   m_string_get_cstr(hexbuf));
                retval = HTTP_BAD_REQUEST;
            }

            cace_ari_t *item = cace_ari_list_push_back_new(tosend);

            res = cace_ari_cbor_decode(item, &databuf, NULL, &errm);
            if (res)
            {
                mg_send_http_error(conn, HTTP_BAD_REQUEST, "Error decoding execution ARI: %s", errm);
                retval = HTTP_BAD_REQUEST;

                CACE_FREE(errm);
                errm = NULL;
            }
            if (cace_log_is_enabled_for(LOG_DEBUG))
            {
                m_string_t buf;
                m_string_init(buf);
                cace_ari_text_encode(buf, item, CACE_ARI_TEXT_ENC_OPTS_DEFAULT);
                CACE_LOG_DEBUG("decoded ARI as %s", m_string_get_cstr(buf));
                m_string_clear(buf);
            }

            if (!cace_ari_is_lit_typed(item, CACE_ARI_TYPE_EXECSET))
            {
                mg_send_http_error(conn, HTTP_BAD_REQUEST, "One value is not an EXECSET");
                retval = HTTP_BAD_REQUEST;
            }

            curs += part_len;
            size_t sep_len = strspn(curs, arisep);
            curs += sep_len;
        }

        cace_data_deinit(&databuf);
        m_string_clear(hexbuf);
    }

    m_bstring_clear(body);
    return retval;
}

static int agentParseText(struct mg_connection *conn, cace_ari_list_t tosend)
{
    int retval = 0;
    int res;

    // Request body contains URI text-encoded ARIs as lines
    m_bstring_t body;
    m_bstring_init(body);
    retval = readRequstBody(conn, body);

    if (!retval)
    {
        m_bstring_push_back(body, '\0');

        m_string_t linebuf;
        m_string_init(linebuf);
        char *errm = NULL;

        static const char *ctrlsep = "\n\r"; // only newline

        const size_t body_len = m_bstring_size(body);
        const char  *curs     = (const char *)m_bstring_view(body, 0, body_len);
        const char  *end      = curs + body_len;
        while (!retval && (curs < end))
        {
            size_t part_len = strcspn(curs, ctrlsep);
            if (part_len == 0)
            {
                break;
            }

            m_string_set_cstrn(linebuf, curs, part_len);
            CACE_LOG_DEBUG("Handling message line %s", m_string_get_cstr(linebuf));

            cace_ari_t *eset = cace_ari_list_push_back_new(tosend);

#if ARI_TEXT_PARSE
            res = cace_ari_text_decode(eset, linebuf, &errm);
            if (res)
            {
                mg_send_http_error(conn, HTTP_BAD_REQUEST, "Error decoding execution ARI: %s", errm);
                retval = HTTP_BAD_REQUEST;

                CACE_FREE(errm);
                errm = NULL;
            }
#endif
            if (!cace_ari_is_lit_typed(eset, CACE_ARI_TYPE_EXECSET))
            {
                mg_send_http_error(conn, HTTP_BAD_REQUEST, "One value is not an EXECSET");
                retval = HTTP_BAD_REQUEST;
            }

            // FIXME: what is this? ui_postprocess_ctrl(id);

            curs += part_len;
            size_t sep_len = strspn(curs, ctrlsep);
            curs += sep_len;
        }

        m_string_clear(linebuf);
    }

    m_bstring_clear(body);
    return retval;
}

static int agentSendItems(struct mg_connection *conn, refdm_agent_t *agent, cace_ari_list_t tosend)
{
    int retval = 0;
    CACE_LOG_INFO("Sending message with %d EXECSETs", cace_ari_list_size(tosend));

    if (cace_ari_list_empty_p(tosend))
    {
        mg_send_http_error(conn, HTTP_UNPROCESSABLE_CNT, "No items to send");
        retval = HTTP_UNPROCESSABLE_CNT;
    }

    if (!retval)
    {
        refdm_mgr_t *mgr = mg_get_user_data(mg_get_context(conn));

        cace_amm_msg_if_metadata_t meta;
        cace_amm_msg_if_metadata_init(&meta);
        cace_ari_set_tstr(&meta.dest, m_string_get_cstr(agent->eid), true);

        struct timespec timeout = { .tv_sec = 5 };

        int res = (mgr->mif.send)(tosend, &meta, &timeout, mgr->mif.ctx);
        cace_amm_msg_if_metadata_deinit(&meta);
        if (res)
        {
            CACE_LOG_ERR("failed to send with status %d", res);
            mg_send_http_error(conn, HTTP_GATEWAY_TIMEOUT, "Failed sending to agent");
            retval = HTTP_GATEWAY_TIMEOUT;
        }
    }

    if (!retval)
    {
        // add EXECSETs to Database after successful send
#if POSTGRESQL_FOUND
        /* Copy the message group to the database tables */
        CACE_LOG_INFO("logging EXECSETs in db started");
        // add all execset
        cace_ari_list_it_t it;
        for (cace_ari_list_it(it, tosend); !cace_ari_list_end_p(it); cace_ari_list_next(it))
        {
            const cace_ari_t *curr_set = cace_ari_list_cref(it);
            refdm_db_insert_execset(curr_set, agent);
        }
        CACE_LOG_INFO("logging EXECSETs in db finished");
#endif // POSTGRESQL_FOUND

        CACE_LOG_INFO("Successfully sent EXECSETs");

        mg_response_header_start(conn, HTTP_NO_CONTENT);
        mg_response_header_send(conn);
        retval = HTTP_NO_CONTENT;
    }

    cace_ari_list_clear(tosend);
    return retval;
}

static int agentShowReports(struct mg_connection *conn, refdm_agent_t *agent, const char *form)
{
    CHKRET(agent, HTTP_INTERNAL_ERROR);
    CHKRET(form, HTTP_INTERNAL_ERROR);

    struct tm mgr_time;
    // Flag that defines if the rptsets came from a remote source (i.e. a database). If this
    // is set to true, then the variable ptr_rptsets should be cleared within this method.
    bool             is_remote_rptsets = false;
    cace_ari_list_t *ptr_rptsets       = NULL;

#if POSTGRESQL_FOUND
    // Synthesize the rptsets (on the stack)
    cace_ari_list_t rptsets;
    cace_ari_list_init(rptsets);

    int32_t idx = refdm_db_fetch_agent_idx(m_string_get_cstr(agent->eid));
    // Retrieve the rptsets from the remote (database) source
    int ecode = refdm_db_fetch_rptset_list(idx, &rptsets, &mgr_time);
    if (ecode != 0)
    {
        cace_ari_list_clear(rptsets);

        mg_send_http_error(conn, HTTP_INTERNAL_ERROR, "Database error encountered.");
        return HTTP_INTERNAL_ERROR;
    }

    // Set the prt_rptsets to point to the stack
    ptr_rptsets       = &rptsets;
    is_remote_rptsets = true;
#else  // POSTGRESQL_FOUND
    // Set the prt_rptsets to point to the local copy on the agent
    ptr_rptsets = &agent->rptsets;
    gmtime_r(&agent->mgr_time, &mgr_time);
    is_remote_rptsets = false;
#endif // POSTGRESQL_FOUND

    // Return no content if there are no reports
    if (cace_ari_list_empty_p(*ptr_rptsets))
    {
        // Clear the internals of data pointed to by ptr_rptsets (if it was sourced remotely).
        if (is_remote_rptsets == true)
        {
            cace_ari_list_clear(*ptr_rptsets);
        }

        mg_response_header_start(conn, HTTP_NO_CONTENT);
        mg_response_header_send(conn);
        return HTTP_NO_CONTENT;
    }

    int         retval = 0;
    const char *ctype  = NULL;
    size_t      clen   = 0;
    // Optional one of text or bytes body
    m_string_t body_text;
    m_string_init(body_text);
    m_bstring_t body_bytes;
    m_bstring_init(body_bytes);

    if ((strcasecmp(form, "uri") == 0) || (strcasecmp(form, "text") == 0))
    {
        /* Iterate through all RPTSET for this agent in one buffer */
        cace_ari_list_it_t rpt_it;
        for (cace_ari_list_it(rpt_it, *ptr_rptsets); !cace_ari_list_end_p(rpt_it); cace_ari_list_next(rpt_it))
        {
            const cace_ari_t *val = cace_ari_list_cref(rpt_it);

            m_string_t uristr;
            m_string_init(uristr);
            int enc_ret = cace_ari_text_encode(uristr, val, CACE_ARI_TEXT_ENC_OPTS_DEFAULT);

            m_string_cat(body_text, uristr);
            m_string_clear(uristr);
            m_string_cat_cstr(body_text, "\r\n"); // HTTP convention

            if (enc_ret)
            {
                mg_send_http_error(conn, HTTP_INTERNAL_ERROR, "encoding failure");
                retval = HTTP_INTERNAL_ERROR;
                break;
            }
        }

        ctype = "text/uri-list";
        clen  = m_string_size(body_text);
    }
    else if (strcasecmp(form, "cbor") == 0)
    {
        /* Iterate through all RPTSET for this agent. */
        cace_ari_list_it_t rpt_it;
        for (cace_ari_list_it(rpt_it, *ptr_rptsets); !cace_ari_list_end_p(rpt_it); cace_ari_list_next(rpt_it))
        {
            const cace_ari_t *val = cace_ari_list_cref(rpt_it);

            cace_data_t bytestr;
            cace_data_init(&bytestr);
            int enc_ret = cace_ari_cbor_encode(&bytestr, val);

            m_bstring_push_back_bytes(body_bytes, bytestr.len, bytestr.ptr);
            cace_data_deinit(&bytestr);

            if (enc_ret)
            {
                mg_send_http_error(conn, HTTP_INTERNAL_ERROR, "encoding failure");
                retval = HTTP_INTERNAL_ERROR;
                break;
            }
        }

        ctype = "application/cbor-seq";
        clen  = m_bstring_size(body_bytes);
    }
    else if ((strcasecmp(form, "cborhex") == 0) || (strcasecmp(form, "hex") == 0))
    {
        /* Iterate through all RPTSET for this agent. */
        cace_ari_list_it_t rpt_it;
        for (cace_ari_list_it(rpt_it, *ptr_rptsets); !cace_ari_list_end_p(rpt_it); cace_ari_list_next(rpt_it))
        {
            const cace_ari_t *val = cace_ari_list_cref(rpt_it);

            cace_data_t bytestr;
            cace_data_init(&bytestr);
            int enc_ret = cace_ari_cbor_encode(&bytestr, val);

            m_string_t hexstr;
            m_string_init(hexstr);
            int hex_ret = cace_base16_encode(hexstr, &bytestr, false);
            cace_data_deinit(&bytestr);

            m_string_cat(body_text, hexstr);
            m_string_clear(hexstr);
            m_string_cat_cstr(body_text, "\r\n"); // HTTP convention

            if (enc_ret || hex_ret)
            {
                mg_send_http_error(conn, HTTP_INTERNAL_ERROR, "encoding failure");
                retval = HTTP_INTERNAL_ERROR;
                break;
            }
        }

        ctype = "text/plain";
        clen  = m_string_size(body_text);
    }

    if (!retval)
    {
        mg_response_header_start(conn, HTTP_OK);
        mg_response_header_add(conn, "Content-Type", ctype, -1);

        char   buf[64];
        size_t buf_used = snprintf(buf, sizeof(buf), "%zu", clen);
        mg_response_header_add(conn, "Content-Length", buf, (int)buf_used);

        buf_used = strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &mgr_time);
        mg_response_header_add(conn, "Last-Modified", buf, (int)buf_used);

        mg_response_header_send(conn);
        if (!m_string_empty_p(body_text))
        {
            mg_write(conn, m_string_get_cstr(body_text), clen);
        }
        else if (!m_bstring_empty_p(body_bytes))
        {
            mg_write(conn, m_bstring_view(body_bytes, 0, clen), clen);
        }
        retval = HTTP_OK;
    }

    // Release the resources of rptsets (if it is sourced remotely)
    if (is_remote_rptsets == true)
    {
        cace_ari_list_clear(*ptr_rptsets);
    }

    m_bstring_clear(body_bytes);
    m_string_clear(body_text);
    return retval;
}

/// Characters disallowed in URI segments (per RFC 3986) to know where they end
static const char *uri_seg_sep = "/?#";

static int getSegmentDecoded(struct mg_connection *conn, const char *prefix, m_string_t seg)
{
    const struct mg_request_info *ri = mg_get_request_info(conn);

    // Ignore common prefix with trailing slash
    const char *suburi_begin = ri->local_uri + strlen(prefix);

    m_string_t rawbuf;
    m_string_init_set_cstr(rawbuf, suburi_begin);
    size_t seg_len = m_string_cspn(rawbuf, uri_seg_sep);
    m_string_left(rawbuf, seg_len);

    if (m_string_empty_p(rawbuf))
    {
        mg_send_http_error(conn, HTTP_NOT_FOUND, "Missing or empty segment");
        return HTTP_NOT_FOUND;
    }

    // possible to be the same length, with trailing null, but no longer
    char decbuf[seg_len + 1];
    int  got = mg_url_decode(m_string_get_cstr(rawbuf), seg_len, decbuf, seg_len + 1, 0);
    m_string_clear(rawbuf);
    if (got <= 0)
    {
        mg_send_http_error(conn, HTTP_BAD_REQUEST, "Invalid segment percent encoding");
        return HTTP_BAD_REQUEST;
    }

    m_string_set_cstr(seg, decbuf);
    return 0;
}

static int getAgentFromEid(struct mg_connection *conn, const char *prefix, refdm_agent_t **agent)
{
    m_string_t eid;
    m_string_init(eid);
    int res = getSegmentDecoded(conn, prefix, eid);
    if (res)
    {
        m_string_clear(eid);
        return res;
    }

    refdm_mgr_t *mgr = mg_get_user_data(mg_get_context(conn));

    *agent = refdm_mgr_agent_get_eid(mgr, m_string_get_cstr(eid));
    CACE_LOG_DEBUG("Lookup agent %s result %p", m_string_get_cstr(eid), *agent);
    m_string_clear(eid);
    if (*agent == NULL)
    {
        mg_send_http_error(conn, HTTP_NOT_FOUND, "Unknown agent EID: %s", m_string_get_cstr(eid));
        return HTTP_NOT_FOUND;
    }

    return 0;
}

static int getFormParam(struct mg_connection *conn, char *form, size_t form_len)
{
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (ri->query_string)
    {
        int res = mg_get_var(ri->query_string, strlen(ri->query_string), "form", form, form_len);
        if ((res == -2)
            || !((strcasecmp(form, "uri") == 0) || (strcasecmp(form, "text") == 0) || (strcasecmp(form, "cbor") == 0)
                 || (strcasecmp(form, "cborhex") == 0) || (strcasecmp(form, "hex") == 0)))
        {
            mg_send_http_error(conn, HTTP_BAD_REQUEST, "Form parameter must be either uri, cbor, or cborhex");
            return HTTP_BAD_REQUEST;
        }
    }
    CACE_LOG_DEBUG("Parsed form=%s", form);

    return 0;
}

/** The ./send resource of either agent form.
 */
static int agentAnySendHandler(struct mg_connection *conn, refdm_agent_t *agent)
{
    static const char *allow_val = "OPTIONS,POST";

    const struct mg_request_info *ri = mg_get_request_info(conn);

    char form[10] = ""; // size enough to hold valid values

    int retval = getFormParam(conn, form, sizeof(form));
    if (retval)
    {
        return retval;
    }

    if (0 == strcasecmp(ri->request_method, "OPTIONS"))
    {
        mg_response_header_start(conn, HTTP_NO_CONTENT);
        mg_response_header_add(conn, "Allow", allow_val, -1);
        mg_response_header_send(conn);
        return HTTP_NO_CONTENT;
    }
    else if (0 == strcasecmp(ri->request_method, "POST"))
    {
        cace_ari_list_t tosend;
        cace_ari_list_init(tosend);

        const char *ctype = mg_get_header(conn, "content-type");
        // Use content type when unambiguous
        if (strlen(form) == 0)
        {
            if (isContentType(ctype, "text/uri-list"))
            {
                retval = agentParseText(conn, tosend);
            }
            else if (isContentType(ctype, "application/cbor-seq") || isContentType(ctype, "application/cbor"))
            {
                retval = agentParseCbor(conn, tosend);
            }
            else
            {
                mg_send_http_error(conn, HTTP_UNSUP_MEDIA_TYPE,
                                   "Must supply a valid media type or use query parameter \"form\"");
                retval = HTTP_UNSUP_MEDIA_TYPE;
            }
        }
        if ((strcasecmp(form, "uri") == 0) || (strcasecmp(form, "text") == 0))
        {
            // either is acceptable
            if (!(isContentType(ctype, "text/uri-list") || isContentType(ctype, "text/plain")))
            {
                mg_send_http_error(conn, HTTP_UNSUP_MEDIA_TYPE, "Only text/uri-list or text/plain supported");
                retval = HTTP_UNSUP_MEDIA_TYPE;
            }
            if (!retval)
            {
                retval = agentParseText(conn, tosend);
            }
        }
        else if (strcasecmp(form, "cbor") == 0)
        {
            if (!(isContentType(ctype, "application/cbor-seq") || isContentType(ctype, "application/cbor")))
            {
                mg_send_http_error(conn, HTTP_UNSUP_MEDIA_TYPE,
                                   "Only application/cbor-seq or application/cbor supported");
                retval = HTTP_UNSUP_MEDIA_TYPE;
            }
            if (!retval)
            {
                retval = agentParseCbor(conn, tosend);
            }
        }
        else if ((strcasecmp(form, "cborhex") == 0) || (strcasecmp(form, "hex") == 0))
        {
            if (!isContentType(ctype, "text/plain"))
            {
                mg_send_http_error(conn, HTTP_UNSUP_MEDIA_TYPE, "Only text/plain supported");
                retval = HTTP_UNSUP_MEDIA_TYPE;
            }
            if (!retval)
            {
                retval = agentParseHex(conn, tosend);
            }
        }

        if (retval)
        {
            cace_ari_list_clear(tosend);
            return retval;
        }
        return agentSendItems(conn, agent, tosend);
    }
    else
    {
        mg_response_header_start(conn, HTTP_METHOD_NOT_ALLOWED);
        mg_response_header_add(conn, "Allow", allow_val, -1);
        mg_response_header_send(conn);
        return HTTP_METHOD_NOT_ALLOWED;
    }
}

static int agentAnyInfoHandler(struct mg_connection *conn, refdm_agent_t *agent _U_)
{

    const struct mg_request_info *ri = mg_get_request_info(conn);

    size_t uri_len = strlen(ri->local_uri);
    if ((uri_len > 1) && (ri->local_uri[uri_len - 1] != '/'))
    {
        // append the canonical slash
        char suffix_eid[uri_len + 2];
        snprintf(suffix_eid, sizeof(suffix_eid), "%s/", ri->local_uri);
        mg_send_http_redirect(conn, suffix_eid, HTTP_PERM_REDIRECT);
        return HTTP_PERM_REDIRECT;
    }

    mg_response_header_start(conn, HTTP_NO_CONTENT);
    mg_response_header_send(conn);
    return HTTP_NO_CONTENT;
}

/** Handler /agents/eid/$eid/ - Determine if agent is registered
 */
static int agentEidInfoHandler(struct mg_connection *conn, void *cbdata _U_)
{
    const struct mg_request_info *ri = mg_get_request_info(conn);
    CACE_LOG_DEBUG("Handling %s to %s", ri->request_method, ri->local_uri);
    refdm_agent_t *agent = NULL;

    int retval = getAgentFromEid(conn, AGENTS_EID_PREFIX, &agent);
    if (retval)
    {
        return retval;
    }
    return agentAnyInfoHandler(conn, agent);
}

/** Handler /agents/eid/$eid/send - Send EXECSET encoded according to query key "form"
 */
static int agentEidSendHandler(struct mg_connection *conn, void *cbdata _U_)
{
    const struct mg_request_info *ri = mg_get_request_info(conn);
    CACE_LOG_DEBUG("Handling %s to %s", ri->request_method, ri->local_uri);
    refdm_agent_t *agent = NULL;

    int retval = getAgentFromEid(conn, AGENTS_EID_PREFIX, &agent);
    if (retval)
    {
        return retval;
    }

    return agentAnySendHandler(conn, agent);
}

/** Handler /agents/eid/$eid/clear_reports - Clear all received reports for this agent.
 */
static int agentEidClearReportsHandler(struct mg_connection *conn, void *cbdata _U_)
{
    static const char *allow_val = "OPTIONS,POST";

    const struct mg_request_info *ri = mg_get_request_info(conn);
    CACE_LOG_DEBUG("Handling %s to %s", ri->request_method, ri->local_uri);
    refdm_agent_t *agent = NULL;

    int res = getAgentFromEid(conn, AGENTS_EID_PREFIX, &agent);
    if (res)
    {
        return res;
    }

    if (0 == strcasecmp(ri->request_method, "OPTIONS"))
    {
        mg_response_header_start(conn, HTTP_NO_CONTENT);
        mg_response_header_add(conn, "Allow", allow_val, -1);
        mg_response_header_send(conn);
        return HTTP_NO_CONTENT;
    }
    else if (0 == strcasecmp(ri->request_method, "POST"))
    {
        refdm_mgr_t *mgr = mg_get_user_data(mg_get_context(conn));
        refdm_mgr_clear_reports(mgr, agent);

        mg_response_header_start(conn, HTTP_NO_CONTENT);
        mg_response_header_send(conn);
        return HTTP_NO_CONTENT;
    }
    else
    {
        mg_response_header_start(conn, HTTP_METHOD_NOT_ALLOWED);
        mg_response_header_add(conn, "Allow", allow_val, -1);
        mg_response_header_send(conn);
        return HTTP_METHOD_NOT_ALLOWED;
    }
}

/** The ./reports resource of either agent form.
 */
static int agentAnyReportsHandler(struct mg_connection *conn, refdm_agent_t *agent)
{
    static const char *allow_val = "OPTIONS,GET";

    const struct mg_request_info *ri = mg_get_request_info(conn);

    char form[10] = "uri"; // size enough to hold valid values

    int retval = getFormParam(conn, form, sizeof(form));
    if (retval)
    {
        return retval;
    }

    if (0 == strcasecmp(ri->request_method, "OPTIONS"))
    {
        mg_response_header_start(conn, HTTP_NO_CONTENT);
        mg_response_header_add(conn, "Allow", allow_val, -1);
        mg_response_header_send(conn);
        return HTTP_NO_CONTENT;
    }
    else if (0 == strcasecmp(ri->request_method, "GET"))
    {
        retval = agentShowReports(conn, agent, form);
    }
    else
    {
        mg_response_header_start(conn, HTTP_METHOD_NOT_ALLOWED);
        mg_response_header_add(conn, "Allow", allow_val, -1);
        mg_response_header_send(conn);
        retval = HTTP_METHOD_NOT_ALLOWED;
    }
    return retval;
}

/** Handler /agents/eid/$eid/reports/hex - Retrieve array of reports in CBOR-encoded HEX form
 */
static int agentEidReportsHandler(struct mg_connection *conn, void *cbdata _U_)
{
    const struct mg_request_info *ri = mg_get_request_info(conn);
    CACE_LOG_DEBUG("Handling %s to %s", ri->request_method, ri->local_uri);
    refdm_agent_t *agent = NULL;

    int res = getAgentFromEid(conn, AGENTS_EID_PREFIX, &agent);
    if (res)
    {
        return res;
    }

    return agentAnyReportsHandler(conn, agent);
}

static int getAgentFromIdx(struct mg_connection *conn, const char *prefix, refdm_agent_t **agent)
{
    m_string_t idx;
    m_string_init(idx);
    int res = getSegmentDecoded(conn, prefix, idx);
    if (res)
    {
        m_string_clear(idx);
        return res;
    }

    const char *seg_idx_begin = m_string_get_cstr(idx);
    const char *seg_idx_end   = seg_idx_begin + m_string_size(idx);
    char       *seg_idx_used;
    size_t      index = strtoull(seg_idx_begin, &seg_idx_used, 10);
    m_string_clear(idx);
    if (seg_idx_used < seg_idx_end)
    {
        // the entire segment was not converted
        mg_send_http_error(conn, HTTP_NOT_FOUND, "Invalid agent index text: %s", seg_idx_begin);
        return HTTP_NOT_FOUND;
    }

    refdm_mgr_t *mgr = mg_get_user_data(mg_get_context(conn));

    *agent = refdm_mgr_agent_get_index(mgr, index);
    if (*agent == NULL)
    {
        mg_send_http_error(conn, HTTP_NOT_FOUND, "Unknown agent index: %zu", index);
        return HTTP_NOT_FOUND;
    }

    return 0;
}

/** Handler /agents/idx/$idx/hex - Send HEX-encoded CBOR Command (hex string as request body).
 */
static int agentIdxSendHandler(struct mg_connection *conn, void *cbdata _U_)
{
    const struct mg_request_info *ri = mg_get_request_info(conn);
    CACE_LOG_DEBUG("Handling %s to %s", ri->request_method, ri->local_uri);
    refdm_agent_t *agent = NULL;

    int res = getAgentFromIdx(conn, AGENTS_IDX_PREFIX, &agent);
    if (res)
    {
        return res;
    }

    return agentAnySendHandler(conn, agent);
}

/** Handler /agents/idx/$idx/ - Determine if agent is registered
 */
static int agentIdxInfoHandler(struct mg_connection *conn, void *cbdata _U_)
{
    const struct mg_request_info *ri = mg_get_request_info(conn);
    CACE_LOG_DEBUG("Handling %s to %s", ri->request_method, ri->local_uri);
    refdm_agent_t *agent = NULL;

    int res = getAgentFromIdx(conn, AGENTS_IDX_PREFIX, &agent);
    if (res)
    {
        return res;
    }
    return agentAnyInfoHandler(conn, agent);
}

/** Handler /agents/idx/$idx/clear_reports - Clear all received reports for this agent.
 */
static int agentIdxClearReportsHandler(struct mg_connection *conn, void *cbdata _U_)
{
    static const char *allow_val = "OPTIONS,POST";

    const struct mg_request_info *ri = mg_get_request_info(conn);
    CACE_LOG_DEBUG("Handling %s to %s", ri->request_method, ri->local_uri);
    refdm_agent_t *agent = NULL;

    int res = getAgentFromIdx(conn, AGENTS_IDX_PREFIX, &agent);
    if (res)
    {
        return res;
    }

    if (0 == strcasecmp(ri->request_method, "OPTIONS"))
    {
        mg_response_header_start(conn, HTTP_NO_CONTENT);
        mg_response_header_add(conn, "Allow", allow_val, -1);
        mg_response_header_send(conn);
        return HTTP_NO_CONTENT;
    }
    else if (0 == strcasecmp(ri->request_method, "POST"))
    {
        refdm_mgr_t *mgr = mg_get_user_data(mg_get_context(conn));
        refdm_mgr_clear_reports(mgr, agent);

        mg_response_header_start(conn, HTTP_NO_CONTENT);
        mg_response_header_send(conn);
        return HTTP_NO_CONTENT;
    }
    else
    {
        mg_response_header_start(conn, HTTP_METHOD_NOT_ALLOWED);
        mg_response_header_add(conn, "Allow", allow_val, -1);
        mg_response_header_send(conn);
        return HTTP_METHOD_NOT_ALLOWED;
    }
}

/** Handler /agents/idx/$idx/reports/hex - Retrieve array of reports in CBOR-encoded HEX form
 */
static int agentIdxReportsHandler(struct mg_connection *conn, void *cbdata _U_)
{
    const struct mg_request_info *ri = mg_get_request_info(conn);
    CACE_LOG_DEBUG("Handling %s to %s", ri->request_method, ri->local_uri);
    refdm_agent_t *agent = NULL;

    int res = getAgentFromIdx(conn, AGENTS_IDX_PREFIX, &agent);
    if (res)
    {
        return res;
    }

    return agentAnyReportsHandler(conn, agent);
}

void refdm_nm_rest_get_docroot(m_string_t docroot_path)
{
    const char *dirs = getenv("XDG_DATA_DIRS");
    if (!dirs || (dirs[0] == '\0'))
    {
        dirs = "/usr/local/share/:/usr/share/";
    }

    size_t len = 0;
    // stop on the first found path
    for (const char *curs = dirs; *curs != '\0'; curs += len)
    {
        // skip duplicate separators
        len = strspn(curs, ":");
        if (len > 0)
        {
            continue;
        }

        len = strcspn(curs, ":");
        if (len == 0)
        {
            // nothing left
            break;
        }
        // must have absolute path
        if (*curs != '/')
        {
            CACE_LOG_WARNING("ignoring non-absolute path %.*s", (int)len, curs);
            continue;
        }

        // need trailing separator
        const char *sep = (*(curs + len - 1) == '/') ? "" : "/";

        // append required suffix
        m_string_printf(docroot_path, "%.*s%srefdm", (int)len, curs, sep);
        { // check it
            struct stat stats;
            if (stat(m_string_get_cstr(docroot_path), &stats) == 0)
            {
                if (S_ISDIR(stats.st_mode))
                {
                    CACE_LOG_DEBUG("Using docroot path %s", m_string_get_cstr(docroot_path));
                    return;
                }
                else
                {
                    CACE_LOG_WARNING("ignoring non-directory path %s", m_string_get_cstr(docroot_path));
                }
            }
            else
            {
                CACE_LOG_DEBUG("ignoring missing path %s", m_string_get_cstr(docroot_path));
            }
        }

        m_string_reset(docroot_path);
        // try next part
    }
    // none found, leave empty
    CACE_LOG_WARNING("No docroot found from XDG_DATA_DIRS");
}

int refdm_nm_rest_start(struct mg_context **ctx, refdm_mgr_t *mgr)
{
    CHKERR1(ctx);
    CHKERR1(mgr);

    struct mg_callbacks callbacks;
    unsigned            features = 0;

    /* Check if libcivetweb has been built with all required features. */
    unsigned got = mg_init_library(features);
    if (got != features)
    {
        CACE_LOG_ERR("REST server failed to start with requested feature flags %u got %u", features, got);
        return 2;
    }

    /* Callback will print error messages to console */
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.log_message = log_message;

    char port_buf[6]; // holds uint16_t
    snprintf(port_buf, sizeof(port_buf), "%" PRIu16, mgr->rest_listen_port);
    // options ingested and copied where necessary during startup
    const char *options[] = { "listening_ports",
                              port_buf,
                              "document_root",
                              m_string_get_cstr(mgr->docroot_path),
                              "request_timeout_ms",
                              "10000",
                              "decode_url",
                              "no", // percent decode in handler functions
                              "enable_auth_domain_check",
                              "no",
                              0 };

    /* Start CivetWeb web server */
    *ctx = mg_start(&callbacks, (void *)mgr, options);

    /* Check return value: */
    if (*ctx == NULL)
    {
        CACE_LOG_ERR("Cannot start CivetWeb - mg_start failed.\n");
        return 4;
    }

    /* Add URL Handlers to override document_root */
    mg_set_request_handler(*ctx, BASE_API_URI "/version$", versionHandler, 0);
    mg_set_request_handler(*ctx, BASE_API_URI "/agents$", agentsHandler, 0);

    mg_set_request_handler(*ctx, AGENTS_EID_PREFIX "*$", agentEidInfoHandler, 0);
    mg_set_request_handler(*ctx, AGENTS_EID_PREFIX "*/$", agentEidInfoHandler, 0);
    mg_set_request_handler(*ctx, AGENTS_EID_PREFIX "*/clear_reports$", agentEidClearReportsHandler, 0);
    mg_set_request_handler(*ctx, AGENTS_EID_PREFIX "*/send$", agentEidSendHandler, 0);
    mg_set_request_handler(*ctx, AGENTS_EID_PREFIX "*/reports$", agentEidReportsHandler, 0);

    mg_set_request_handler(*ctx, AGENTS_IDX_PREFIX "*$", agentIdxInfoHandler, 0);
    mg_set_request_handler(*ctx, AGENTS_IDX_PREFIX "*/$", agentIdxInfoHandler, 0);
    mg_set_request_handler(*ctx, AGENTS_IDX_PREFIX "*/clear_reports$", agentIdxClearReportsHandler, 0);
    mg_set_request_handler(*ctx, AGENTS_IDX_PREFIX "*/send$", agentIdxSendHandler, 0);
    mg_set_request_handler(*ctx, AGENTS_IDX_PREFIX "*/reports$", agentIdxReportsHandler, 0);

    CACE_LOG_INFO("REST API Server Started on port %s", port_buf);
    return 0;
}

void refdm_nm_rest_stop(struct mg_context *ctx)
{
    CHKVOID(ctx);
    mg_stop(ctx);
}
