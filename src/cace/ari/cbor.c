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
/** @file
 * @ingroup group_ari
 * This file contains the definitions, prototypes, constants, and
 * other information necessary for the identification and
 * processing of AMM Resource Identifiers (ARIs). Every object in
 * the AMM can be uniquely identified using an ARI.
 */
#include "cbor.h"

#include "access.h"
#include "objpat.h"
#include "text_util.h"

#include "cace/util/defs.h"
#include "cace/util/logging.h"

#include <qcbor/qcbor_spiffy_decode.h>

int cace_ari_cbor_encode(cace_data_t *buf, const cace_ari_t *ari)
{
    CHKERR1(buf);
    CHKERR1(ari);

    QCBOREncodeContext encoder;
    QCBOREncode_Init(&encoder, SizeCalculateUsefulBuf);
    cace_ari_cbor_encode_stream(&encoder, ari);
    size_t needlen;
    if (QCBOR_SUCCESS != QCBOREncode_FinishGetSize(&encoder, &needlen))
    {
        CACE_LOG_WARNING("CBOR early encoding did not complete properly");
        return 2;
    }

    cace_data_copy_from(buf, needlen, NULL);
    QCBOREncode_Init(&encoder, (UsefulBuf) { buf->ptr, buf->len });
    cace_ari_cbor_encode_stream(&encoder, ari);
    UsefulBufC encdata;
    if (QCBOR_SUCCESS != QCBOREncode_Finish(&encoder, &encdata))
    {
        CACE_LOG_WARNING("CBOR late encoding did not complete properly");
        return 3;
    }
    if (encdata.len != needlen)
    {
        CACE_LOG_WARNING("CBOR encoding size mismatch");
    }

    return 0;
}

static int cace_ari_cbor_encode_idseg(QCBOREncodeContext *enc, const cace_ari_idseg_t *obj)
{
    switch (obj->form)
    {
        case CACE_ARI_IDSEG_NULL:
            QCBOREncode_AddNULL(enc);
            break;
        case CACE_ARI_IDSEG_TEXT:
        {
            // trim off trailing null
            const UsefulBufC buf = { .ptr = m_string_get_cstr(obj->as_text), .len = m_string_size(obj->as_text) };
            QCBOREncode_AddText(enc, buf);
            break;
        }
        case CACE_ARI_IDSEG_INT:
            QCBOREncode_AddInt64(enc, obj->as_int);
            break;
    }
    return 0;
}

static int cace_ari_cbor_decode_idseg(QCBORDecodeContext *dec, cace_ari_idseg_t *obj)
{
    QCBORItem decitem;
    QCBORDecode_VPeekNext(dec, &decitem);
    if (QCBORDecode_GetError(dec))
    {
        return 2;
    }

    switch (decitem.uDataType)
    {
        case QCBOR_TYPE_NULL:
        {
            obj->form = CACE_ARI_IDSEG_NULL;
            // pass the item
            QCBORDecode_GetNext(dec, &decitem);
            break;
        }
        case QCBOR_TYPE_TEXT_STRING:
        {
            obj->form = CACE_ARI_IDSEG_TEXT;
            UsefulBufC buf;
            QCBORDecode_GetTextString(dec, &buf);
            // add trailing null
            m_string_init_printf(obj->as_text, "%.*s", (int)buf.len, buf.ptr);
            break;
        }
        case QCBOR_TYPE_INT64:
            obj->form = CACE_ARI_IDSEG_INT;
            QCBORDecode_GetInt64(dec, &(obj->as_int));
            break;

        default:
            return 3;
    }
    return 0;
}

static int cace_ari_cbor_encode_optdate(QCBOREncodeContext *enc, const cace_ari_date_t *obj)
{
    if (obj->valid)
    {
        // text date
        m_string_t text;
        m_string_init(text);
        cace_date_encode(text, &(obj->parts), true);
        QCBOREncode_AddTDaysString(enc, QCBOR_ENCODE_AS_TAG, m_string_get_cstr(text));
        m_string_clear(text);
    }
    return 0;
}

static int cace_ari_cbor_decode_optdate(QCBORDecodeContext *dec, cace_ari_date_t *obj)
{
    QCBORItem decitem;
    QCBORDecode_VPeekNext(dec, &decitem);
    if (QCBORDecode_GetError(dec))
    {
        return 2;
    }

    int retval = 0;
    switch (decitem.uDataType)
    {
        case QCBOR_TYPE_DAYS_EPOCH:
            // actual read
            QCBORDecode_VGetNext(dec, &decitem);
            obj->valid = true;
            // FIXME decode
            break;
        case QCBOR_TYPE_DAYS_STRING:
        {
            // actual read
            QCBORDecode_VGetNext(dec, &decitem);
            obj->valid = true;

            // need to append null terminator
            cace_data_t text;
            cace_data_init(&text);
            cace_data_copy_from(&text, decitem.val.dateString.len, (cace_data_ptr_t)decitem.val.dateString.ptr);
            cace_data_append_byte(&text, '\0');
            if (cace_date_decode(&(obj->parts), &text))
            {
                retval = 3;
            }
            cace_data_deinit(&text);
            break;
        }
        default:
            // optional tagged value not present
            break;
    }
    return retval;
}

static int cace_ari_cbor_encode_timespec(QCBOREncodeContext *enc, const struct timespec *ts)
{
    if (ts->tv_nsec)
    {
        // naive encoding without check for truncation
        int64_t exp = -9;
        int64_t val = ts->tv_sec * CACE_ARI_SUBSEC_SCALE + ts->tv_nsec;
        while (val && (val % 10 == 0))
        {
            ++exp;
            val /= 10;
        }

        QCBOREncode_OpenArray(enc);
        QCBOREncode_AddInt64(enc, exp);
        QCBOREncode_AddInt64(enc, val);
        QCBOREncode_CloseArray(enc);
    }
    else
    {
        // pure integer seconds
        QCBOREncode_AddInt64(enc, ts->tv_sec);
    }
    return 0;
}

static int cace_ari_cbor_decode_timespec(QCBORDecodeContext *dec, struct timespec *ts)
{
    QCBORItem decitem;
    QCBORDecode_VPeekNext(dec, &decitem);
    if (QCBORDecode_GetError(dec))
    {
        return 2;
    }

    if (decitem.uDataType == QCBOR_TYPE_ARRAY)
    {
        // a decimal fraction
        int64_t exp;
        int64_t val;

        QCBORDecode_EnterArray(dec, NULL);
        QCBORDecode_GetInt64(dec, &exp);
        QCBORDecode_GetInt64(dec, &val);
        QCBORDecode_ExitArray(dec);
        if (QCBORDecode_GetError(dec))
        {
            return 2;
        }

        // Restrict exponent per ARI spec
        if (exp > 9 || exp < -9)
        {
            return 3;
        }

        // normalize to 1e-9 seconds
        while (exp > -9)
        {
            --exp;
            val *= 10;
        }
        time_t            fullsec = val / CACE_ARI_SUBSEC_SCALE;
        cace_ari_subsec_t subsec  = labs(val % CACE_ARI_SUBSEC_SCALE);
        if ((fullsec < 0) && (subsec > 0))
        {
            fullsec -= 1;
            subsec = 1000000000 - subsec;
        }
        *ts = (struct timespec) {
            .tv_sec  = fullsec,
            .tv_nsec = subsec,
        };
    }
    else
    {
        // pure integer seconds
        int64_t secs;
        QCBORDecode_GetInt64(dec, &secs);
        if (QCBORDecode_GetError(dec))
        {
            return 2;
        }

        *ts = (struct timespec) {
            .tv_sec = secs,
        };
    }
    return 0;
}

static int cace_ari_cbor_encode_ac(QCBOREncodeContext *enc, const cace_ari_ac_t *obj)
{
    int retval = 0;
    QCBOREncode_OpenArray(enc);

    cace_ari_list_it_t it;
    for (cace_ari_list_it(it, obj->items); !cace_ari_list_end_p(it); cace_ari_list_next(it))
    {
        const cace_ari_t *item = cace_ari_list_cref(it);
        if (cace_ari_cbor_encode_stream(enc, item))
        {
            retval = 2;
            break;
        }
    }

    QCBOREncode_CloseArray(enc);
    return retval;
}

static int cace_ari_cbor_decode_ac(QCBORDecodeContext *dec, cace_ari_ac_t *obj)
{
    QCBORDecode_EnterArray(dec, NULL);
    if (QCBORDecode_GetError(dec))
    {
        return 2;
    }

    int retval = 0;
    while (true)
    {
        cace_ari_t ari       = CACE_ARI_INIT_UNDEFINED;
        int        parse_res = cace_ari_cbor_decode_stream(dec, &ari);

        int  dec_res = QCBORDecode_GetAndResetError(dec);
        bool atend   = (dec_res == QCBOR_ERR_NO_MORE_ITEMS);
        if (!atend)
        {
            if (dec_res)
            {
                // not well-formed CBOR
                CACE_LOG_ERR("decoding AC failed with decoder error %d", dec_res);
                retval = 2;
            }
            else if (parse_res)
            {
                // well-formed CBOR but not valid ARI
                CACE_LOG_ERR("decoding AC failed with parser error %d", dec_res);
                retval = 3;
            }
        }

        if (retval || atend)
        {
            cace_ari_deinit(&ari);
            break;
        }

        // push only after fully reading
        cace_ari_t *item = cace_ari_list_push_back_new(obj->items);
        cace_ari_set_move(item, &ari);
    }
    QCBORDecode_ExitArray(dec);
    if (QCBORDecode_GetError(dec))
    {
        retval = 3;
    }
    return retval;
}

static int cace_ari_cbor_encode_am(QCBOREncodeContext *enc, const cace_ari_am_t *obj)
{
    int retval = 0;
    QCBOREncode_OpenMap(enc);

    cace_ari_tree_it_t it;
    for (cace_ari_tree_it(it, obj->items); !cace_ari_tree_end_p(it); cace_ari_tree_next(it))
    {
        const cace_ari_tree_subtype_ct *pair = cace_ari_tree_cref(it);
        if (cace_ari_cbor_encode_stream(enc, pair->key_ptr))
        {
            retval = 2;
            break;
        }
        if (cace_ari_cbor_encode_stream(enc, pair->value_ptr))
        {
            retval = 2;
            break;
        }
    }

    QCBOREncode_CloseMap(enc);
    return retval;
}

#include "text.h"
static int cace_ari_cbor_decode_am(QCBORDecodeContext *dec, cace_ari_am_t *obj)
{
    int retval = 0;

    // map as array of key+value
    QCBORDecode_EnterArray(dec, NULL);
    if (QCBORDecode_GetError(dec))
    {
        return 2;
    }

    while (true)
    {
        cace_ari_t key   = CACE_ARI_INIT_UNDEFINED;
        cace_ari_t value = CACE_ARI_INIT_UNDEFINED;

        int key_res = cace_ari_cbor_decode_stream(dec, &key);
        int dec_res = QCBORDecode_GetAndResetError(dec);
        // first item determines end-of-map
        bool atend = (dec_res == QCBOR_ERR_NO_MORE_ITEMS);

        // only care about value if key was successful decoding
        // but if key was set, the value *must* be present
        int value_res = 0;
        if (!key_res && (dec_res == QCBOR_SUCCESS))
        {
            value_res = cace_ari_cbor_decode_stream(dec, &value);
            dec_res   = QCBORDecode_GetError(dec);
        }

        if (!atend)
        {
            if (dec_res)
            {
                // not well-formed CBOR
                CACE_LOG_ERR("decoding AM failed with decoder error %d", dec_res);
                retval = 2;
            }
            else if (key_res || value_res)
            {
                // well-formed CBOR but not valid ARI
                CACE_LOG_ERR("decoding AM failed with parser error %d %d", key_res, value_res);
                retval = 3;
            }
            else if (key.is_ref || key.as_lit.has_ari_type)
            {
                // this should not be a key
                retval = 4;
            }
        }

        if (retval || atend)
        {
            cace_ari_deinit(&key);
            cace_ari_deinit(&value);
            break;
        }

        // push only after fully reading
        cace_ari_t *val = cace_ari_tree_safe_get(obj->items, key);
        cace_ari_set_move(val, &value);
        cace_ari_deinit(&key);
    }

    QCBORDecode_ExitArray(dec);
    if (QCBORDecode_GetError(dec))
    {
        retval = 3;
    }
    return retval;
}

static int cace_ari_cbor_encode_tbl(QCBOREncodeContext *enc, const cace_ari_tbl_t *obj)
{
    int retval = 0;
    QCBOREncode_OpenArray(enc);

    QCBOREncode_AddUInt64(enc, obj->ncols);

    cace_ari_array_it_t it;
    for (cace_ari_array_it(it, obj->items); !cace_ari_array_end_p(it); cace_ari_array_next(it))
    {
        const cace_ari_t *item = cace_ari_array_cref(it);
        if (cace_ari_cbor_encode_stream(enc, item))
        {
            retval = 2;
            break;
        }
    }

    QCBOREncode_CloseArray(enc);
    return retval;
}

static int cace_ari_cbor_decode_tbl(QCBORDecodeContext *dec, cace_ari_tbl_t *obj)
{
    int retval = 0;

    QCBORDecode_EnterArray(dec, NULL);
    if (QCBORDecode_GetError(dec))
    {
        return 2;
    }

    QCBORDecode_GetUInt64(dec, &(obj->ncols));
    if (QCBORDecode_GetError(dec))
    {
        return 2;
    }

    while (true)
    {
        cace_ari_t ari       = CACE_ARI_INIT_UNDEFINED;
        int        parse_res = cace_ari_cbor_decode_stream(dec, &ari);

        int  dec_res = QCBORDecode_GetAndResetError(dec);
        bool atend   = (dec_res == QCBOR_ERR_NO_MORE_ITEMS);
        if (!atend)
        {
            if (dec_res)
            {
                // not well-formed CBOR
                CACE_LOG_ERR("decoding TBL failed with decoder error %d", dec_res);
                retval = 2;
            }
            else if (parse_res)
            {
                // well-formed CBOR but not valid ARI
                CACE_LOG_ERR("decoding TBL failed with parser error %d", dec_res);
                retval = 3;
            }
        }

        if (retval || atend)
        {
            cace_ari_deinit(&ari);
            break;
        }
        {
            m_string_t buf;
            m_string_init(buf);
            cace_ari_text_encode(buf, &ari, CACE_ARI_TEXT_ENC_OPTS_DEFAULT);
            CACE_LOG_DEBUG("  item %s", m_string_get_cstr(buf));
            m_string_clear(buf);
        }

        // push only after fully reading
        cace_ari_array_push_move(obj->items, &ari);
    }
    CACE_LOG_DEBUG("  done with %zu", cace_ari_array_size(obj->items));

    if (((obj->ncols == 0) && !cace_ari_array_empty_p(obj->items))
        || ((obj->ncols != 0) && (cace_ari_array_size(obj->items) % obj->ncols != 0)))
    {
        CACE_LOG_ERR("decoding TBL inconsistent; got %zu cols and %zu items", obj->ncols,
                     cace_ari_array_size(obj->items));
        retval = 3; // Invalid ARI due to incomplete row data
    }

    QCBORDecode_ExitArray(dec);
    if (QCBORDecode_GetError(dec))
    {
        retval = 3;
    }
    return retval;
}

static int cace_ari_cbor_encode_execset(QCBOREncodeContext *enc, const cace_ari_execset_t *obj)
{
    int retval = 0;
    QCBOREncode_OpenArray(enc);

    if (cace_ari_cbor_encode_stream(enc, &(obj->nonce)))
    {
        return 2;
    }

    cace_ari_list_it_t it;
    for (cace_ari_list_it(it, obj->targets); !cace_ari_list_end_p(it); cace_ari_list_next(it))
    {
        const cace_ari_t *item = cace_ari_list_cref(it);
        if (cace_ari_cbor_encode_stream(enc, item))
        {
            retval = 2;
            break;
        }
    }

    QCBOREncode_CloseArray(enc);
    return retval;
}

static int cace_ari_cbor_decode_execset(QCBORDecodeContext *dec, cace_ari_execset_t *obj)
{
    int retval = 0;

    QCBORDecode_EnterArray(dec, NULL);
    if (QCBORDecode_GetError(dec))
    {
        return 2;
    }

    if (cace_ari_cbor_decode_stream(dec, &(obj->nonce)))
    {
        return 2;
    }

    if (obj->nonce.is_ref)
    {
        return 3;
    }
    else
    {
        switch (obj->nonce.as_lit.prim_type)
        {
            case CACE_ARI_PRIM_NULL:
            case CACE_ARI_PRIM_UINT64:
            case CACE_ARI_PRIM_BSTR:
                break;
            case CACE_ARI_PRIM_INT64:
                if (obj->nonce.as_lit.value.as_int64 < 0)
                {
                    return 3;
                }
                break;
            default:
                return 3;
        }
    }

    while (true)
    {
        cace_ari_t ari       = CACE_ARI_INIT_UNDEFINED;
        int        parse_res = cace_ari_cbor_decode_stream(dec, &ari);

        int  dec_res = QCBORDecode_GetAndResetError(dec);
        bool atend   = (dec_res == QCBOR_ERR_NO_MORE_ITEMS);
        if (!atend)
        {
            if (dec_res)
            {
                // not well-formed CBOR
                CACE_LOG_ERR("decoding EXECSET failed with decoder error %d", dec_res);
                retval = 2;
            }
            else if (parse_res)
            {
                // well-formed CBOR but not valid ARI
                CACE_LOG_ERR("decoding EXECSET failed with parser error %d", parse_res);
                retval = 3;
            }
        }

        if (retval || atend)
        {
            cace_ari_deinit(&ari);
            break;
        }

        // push only after fully reading
        cace_ari_t *item = cace_ari_list_push_back_new(obj->targets);
        cace_ari_set_move(item, &ari);
    }

    QCBORDecode_ExitArray(dec);
    if (QCBORDecode_GetError(dec))
    {
        retval = 3;
    }
    return retval;
}

static int cace_ari_cbor_encode_report(QCBOREncodeContext *enc, const cace_ari_report_t *obj)
{
    int retval = 0;
    QCBOREncode_OpenArray(enc);

    struct timespec ts;
    if (cace_ari_get_td(&(obj->reltime), &ts))
    {
        return 2;
    }
    if (cace_ari_cbor_encode_timespec(enc, &ts))
    {
        return 2;
    }

    if (cace_ari_cbor_encode_stream(enc, &(obj->source)))
    {
        return 2;
    }

    cace_ari_list_it_t it;
    for (cace_ari_list_it(it, obj->items); !cace_ari_list_end_p(it); cace_ari_list_next(it))
    {
        const cace_ari_t *item = cace_ari_list_cref(it);
        if (cace_ari_cbor_encode_stream(enc, item))
        {
            retval = 2;
            break;
        }
    }

    QCBOREncode_CloseArray(enc);
    return retval;
}

static int cace_ari_cbor_decode_report(QCBORDecodeContext *dec, cace_ari_report_t *obj)
{
    int retval = 0;

    QCBORDecode_EnterArray(dec, NULL);
    if (QCBORDecode_GetError(dec))
    {
        return 2;
    }

    {
        struct timespec ts;
        if (cace_ari_cbor_decode_timespec(dec, &ts))
        {
            return 2;
        }
        cace_ari_set_td(&(obj->reltime), ts);
    }

    if (cace_ari_cbor_decode_stream(dec, &(obj->source)))
    {
        return 2;
    }

    while (true)
    {
        cace_ari_t ari       = CACE_ARI_INIT_UNDEFINED;
        int        parse_res = cace_ari_cbor_decode_stream(dec, &ari);

        int  dec_res = QCBORDecode_GetAndResetError(dec);
        bool atend   = (dec_res == QCBOR_ERR_NO_MORE_ITEMS);
        if (!atend)
        {
            if (dec_res)
            {
                // not well-formed CBOR
                retval = 2;
            }
            else if (parse_res)
            {
                // well-formed CBOR but not valid ARI
                retval = 3;
            }
        }

        if (retval || atend)
        {
            cace_ari_deinit(&ari);
            break;
        }

        // push only after fully reading
        cace_ari_t *item = cace_ari_list_push_back_new(obj->items);
        cace_ari_set_move(item, &ari);
    }

    QCBORDecode_ExitArray(dec);
    if (QCBORDecode_GetError(dec))
    {
        retval = 3;
    }
    return retval;
}

static int cace_ari_cbor_encode_rptset(QCBOREncodeContext *enc, const cace_ari_rptset_t *obj)
{
    int retval = 0;
    QCBOREncode_OpenArray(enc);

    if (cace_ari_cbor_encode_stream(enc, &(obj->nonce)))
    {
        return 2;
    }

    struct timespec ts;
    if (cace_ari_get_tp(&(obj->reftime), &ts))
    {
        return 2;
    }
    if (cace_ari_cbor_encode_timespec(enc, &ts))
    {
        return 2;
    }

    cace_ari_report_list_it_t it;
    for (cace_ari_report_list_it(it, obj->reports); !cace_ari_report_list_end_p(it); cace_ari_report_list_next(it))
    {
        const cace_ari_report_t *rpt = cace_ari_report_list_cref(it);
        if (cace_ari_cbor_encode_report(enc, rpt))
        {
            retval = 2;
            break;
        }
    }

    QCBOREncode_CloseArray(enc);
    return retval;
}

static int cace_ari_cbor_decode_rptset(QCBORDecodeContext *dec, cace_ari_rptset_t *obj)
{
    int retval = 0;

    QCBORDecode_EnterArray(dec, NULL);
    if (QCBORDecode_GetError(dec))
    {
        return 2;
    }

    if (cace_ari_cbor_decode_stream(dec, &(obj->nonce)))
    {
        return 2;
    }

    if (obj->nonce.is_ref)
    {
        return 3;
    }
    else
    {
        switch (obj->nonce.as_lit.prim_type)
        {
            case CACE_ARI_PRIM_NULL:
            case CACE_ARI_PRIM_UINT64:
            case CACE_ARI_PRIM_BSTR:
                break;
            case CACE_ARI_PRIM_INT64:
                if (obj->nonce.as_lit.value.as_int64 < 0)
                {
                    return 3;
                }
                break;
            default:
                return 3;
        }
    }

    {
        struct timespec ts;
        if (cace_ari_cbor_decode_timespec(dec, &ts))
        {
            return 2;
        }
        cace_ari_set_tp(&(obj->reftime), ts);
    }

    while (true)
    {
        cace_ari_report_t rpt;
        cace_ari_report_init(&rpt);
        int parse_res = cace_ari_cbor_decode_report(dec, &rpt);

        int  dec_res = QCBORDecode_GetAndResetError(dec);
        bool atend   = (dec_res == QCBOR_ERR_NO_MORE_ITEMS);
        if (!atend)
        {
            if (dec_res)
            {
                // not well-formed CBOR
                CACE_LOG_ERR("decoding RPTSET failed with decoder error %d", dec_res);
                retval = 2;
            }
            else if (parse_res)
            {
                // well-formed CBOR but not valid ARI
                CACE_LOG_ERR("decoding RPTSET failed with parser error %d", parse_res);
                retval = 3;
            }
        }

        if (retval || atend)
        {
            cace_ari_report_deinit(&rpt);
            break;
        }

        // push only after fully reading
        cace_ari_report_list_push_back_move(obj->reports, &rpt);
    }

    QCBORDecode_ExitArray(dec);
    if (QCBORDecode_GetError(dec))
    {
        retval = 3;
    }
    return retval;
}

static void cace_ari_cbor_encode_objpat_part(QCBOREncodeContext *enc, const cace_ari_objpat_part_t *obj)
{
    const cace_util_range_int64_t *range_int64 = NULL;
    const m_string_t              *text        = NULL;
    if (cace_ari_objpat_part_cget_special(*obj))
    {
        QCBOREncode_AddBool(enc, true);
    }
    else if ((range_int64 = cace_ari_objpat_part_cget_range_int64(*obj)))
    {
        const cace_util_range_intvl_int64_t *single = NULL;
        if (cace_util_range_int64_size(*range_int64) == 1)
        {
            single = cace_util_range_int64_cmin(*range_int64);
            if (!(single->has_min && single->has_max && (single->i_min == single->i_max)))
            {
                single = NULL;
            }
        }

        if (single)
        {
            QCBOREncode_AddInt64(enc, single->i_min);
        }
        else
        {
            QCBOREncode_OpenArray(enc);

            // sentinel to indicate first item
            int64_t pos = INT64_MIN;

            cace_util_range_int64_it_t it;
            for (cace_util_range_int64_it(it, *range_int64); !cace_util_range_int64_end_p(it);
                 cace_util_range_int64_next(it))
            {
                const cace_util_range_intvl_int64_t *intvl = cace_util_range_int64_cref(it);

                if (pos < INT32_MIN)
                {
                    // initial position
                    if (intvl->has_min)
                    {
                        pos = intvl->i_min;
                        QCBOREncode_AddInt64(enc, pos);
                    }
                    else
                    {
                        pos = INT32_MIN;
                        QCBOREncode_AddNULL(enc);
                    }
                }
                else
                {
                    if (intvl->has_min)
                    {
                        int64_t excl = intvl->i_min - pos - 2;
                        QCBOREncode_AddInt64(enc, excl);
                        pos = intvl->i_min;
                    }
                    else
                    {
                        CACE_LOG_ERR("Invalid range interval with no maximum");
                        return;
                    }
                }

                if (intvl->has_max)
                {
                    int64_t incl = intvl->i_max - pos;
                    QCBOREncode_AddInt64(enc, incl);
                    pos = intvl->i_max;
                }
                else
                {
                    QCBOREncode_AddNULL(enc);
                    // can only occur on the last interval
                    break;
                }
            }
            QCBOREncode_CloseArray(enc);
        }
    }
    else if ((text = cace_ari_objpat_part_cget_text(*obj)))
    {
        // trim off trailing null
        const UsefulBufC buf = { .ptr = m_string_get_cstr(*text), .len = m_string_size(*text) };
        QCBOREncode_AddText(enc, buf);
    }
    else
    {
        CACE_LOG_ERR("Invalid OBJPAT part state");
    }
}

static int cace_ari_cbor_decode_objpat_part(QCBORDecodeContext *dec, cace_ari_objpat_part_t *obj)
{
    QCBORItem decitem;
    QCBORDecode_VPeekNext(dec, &decitem);
    if (QCBORDecode_GetError(dec))
    {
        return 3;
    }

    switch (decitem.uDataType)
    {
        case QCBOR_TYPE_TRUE:
            QCBORDecode_GetNext(dec, &decitem);
            cace_ari_objpat_part_set_special(*obj, true);
            break;
        case QCBOR_TYPE_TEXT_STRING:
        {
            UsefulBufC buf;
            QCBORDecode_GetTextString(dec, &buf);
            m_string_t text;
            // add trailing null
            m_string_init_printf(text, "%.*s", (int)buf.len, buf.ptr);
            cace_ari_objpat_part_move_text(*obj, text);
            break;
        }
        case QCBOR_TYPE_INT64:
        {
            // single int range
            int64_t buf;
            QCBORDecode_GetInt64(dec, &buf);

            cace_util_range_int64_t range;
            cace_util_range_int64_init(range);
            {
                cace_util_range_intvl_int64_t intvl;
                cace_util_range_intvl_int64_set_singleton(&intvl, buf);
                cace_util_range_int64_push(range, intvl);
            }
            cace_ari_objpat_part_move_range_int64(*obj, range);
            break;
        }
        case QCBOR_TYPE_ARRAY:
            // full int range
            QCBORDecode_EnterArray(dec, &decitem);
            const size_t array_len = decitem.val.uCount;
            if (QCBORDecode_GetError(dec) || (array_len == UINT16_MAX))
            {
                return 3;
            }

            cace_util_range_int64_t range;
            cace_util_range_int64_init(range);

            cace_util_range_intvl_int64_t intvl;

            // first item
            int64_t pos;
            QCBORDecode_VPeekNext(dec, &decitem);
            if (decitem.uDataType == QCBOR_TYPE_NULL)
            {
                QCBORDecode_GetNext(dec, &decitem);
                // leave infinite min but set implied position
                cace_util_range_intvl_int64_clear_min(&intvl);
                pos = INT32_MIN;
            }
            else
            {
                pos = 0;
                QCBORDecode_GetInt64(dec, &pos);
                cace_util_range_intvl_int64_set_min(&intvl, pos);
            }

            // first item above
            for (size_t ix = 1; ix < array_len - 1;)
            {
                int64_t incl = 0, excl = 0;
                QCBORDecode_GetInt64(dec, &incl);
                QCBORDecode_GetInt64(dec, &excl);
                ix += 2;

                pos += incl;
                cace_util_range_intvl_int64_set_max(&intvl, pos);
                cace_util_range_int64_push(range, intvl);

                pos += excl + 2;
                cace_util_range_intvl_int64_set_min(&intvl, pos);
            }

            // last item to complete interval
            QCBORDecode_VPeekNext(dec, &decitem);
            if (decitem.uDataType == QCBOR_TYPE_NULL)
            {
                QCBORDecode_GetNext(dec, &decitem);
                // leave infinite min but set implied position
                cace_util_range_intvl_int64_clear_max(&intvl);
            }
            else
            {
                int64_t incl = 0;
                QCBORDecode_GetInt64(dec, &incl);
                cace_util_range_intvl_int64_set_max(&intvl, pos + incl);
            }
            cace_util_range_int64_push(range, intvl);

            cace_ari_objpat_part_move_range_int64(*obj, range);
            QCBORDecode_ExitArray(dec);
            if (QCBORDecode_GetError(dec))
            {
                return 2;
            }
            break;
        default:
            CACE_LOG_ERR("Invalid OBJPAT part type: %d", decitem.uDataType);
            return 4;
    }

    return 0;
}

static int cace_ari_cbor_encode_objpat(QCBOREncodeContext *enc, const cace_ari_objpat_t *obj)
{
    int retval = 0;
    QCBOREncode_OpenArray(enc);

    cace_ari_cbor_encode_objpat_part(enc, &obj->org_pat);
    cace_ari_cbor_encode_objpat_part(enc, &obj->model_pat);
    cace_ari_cbor_encode_objpat_part(enc, &obj->type_pat);
    cace_ari_cbor_encode_objpat_part(enc, &obj->obj_pat);

    QCBOREncode_CloseArray(enc);
    return retval;
}

static int cace_ari_cbor_decode_objpat(QCBORDecodeContext *dec, cace_ari_objpat_t *obj)
{
    int retval = 0;

    QCBORDecode_EnterArray(dec, NULL);
    if (QCBORDecode_GetError(dec))
    {
        return 2;
    }

    cace_ari_cbor_decode_objpat_part(dec, &obj->org_pat);
    cace_ari_cbor_decode_objpat_part(dec, &obj->model_pat);
    cace_ari_cbor_decode_objpat_part(dec, &obj->type_pat);
    cace_ari_cbor_decode_objpat_part(dec, &obj->obj_pat);

    QCBORDecode_ExitArray(dec);
    if (QCBORDecode_GetError(dec))
    {
        retval = 3;
    }
    return retval;
}

static int cace_ari_cbor_encode_primval(QCBOREncodeContext *enc, const cace_ari_lit_t *obj)
{
    switch (obj->prim_type)
    {
        case CACE_ARI_PRIM_UNDEFINED:
            QCBOREncode_AddUndef(enc);
            break;
        case CACE_ARI_PRIM_NULL:
            QCBOREncode_AddNULL(enc);
            break;
        case CACE_ARI_PRIM_BOOL:
            QCBOREncode_AddBool(enc, obj->value.as_bool);
            break;
        case CACE_ARI_PRIM_UINT64:
            QCBOREncode_AddUInt64(enc, obj->value.as_uint64);
            break;
        case CACE_ARI_PRIM_INT64:
            QCBOREncode_AddInt64(enc, obj->value.as_int64);
            break;
        case CACE_ARI_PRIM_FLOAT64:
            QCBOREncode_AddDouble(enc, obj->value.as_float64);
            break;
        case CACE_ARI_PRIM_TSTR:
        {
            // trim off trailing null
            const UsefulBufC buf = { .ptr = obj->value.as_data.ptr, .len = obj->value.as_data.len - 1 };
            QCBOREncode_AddText(enc, buf);
            break;
        }
        case CACE_ARI_PRIM_BSTR:
        {
            const UsefulBufC buf = { .ptr = obj->value.as_data.ptr, .len = obj->value.as_data.len };
            QCBOREncode_AddBytes(enc, buf);
            break;
        }
        case CACE_ARI_PRIM_TIMESPEC:
            if (cace_ari_cbor_encode_timespec(enc, &(obj->value.as_timespec)))
            {
                return 2;
            }
            break;
        case CACE_ARI_PRIM_OTHER:
            // This must have an cace_ari_lit_t::ari_type
            return 1;
    }
    return 0;
}

int cace_ari_cbor_encode_stream(QCBOREncodeContext *enc, const cace_ari_t *ari)
{
    CHKERR1(enc);
    CHKERR1(ari);
    if (ari->is_ref)
    {
        const cace_ari_ref_t *obj = &(ari->as_ref);

        QCBOREncode_OpenArray(enc);
        cace_ari_cbor_encode_idseg(enc, &(obj->objpath.org_id));
        cace_ari_cbor_encode_idseg(enc, &(obj->objpath.model_id));
        cace_ari_cbor_encode_optdate(enc, &(obj->objpath.model_rev));
        // prefer enumerated object type
        if (obj->objpath.has_ari_type)
        {
            QCBOREncode_AddInt64(enc, obj->objpath.ari_type);
        }
        else
        {
            cace_ari_cbor_encode_idseg(enc, &(obj->objpath.type_id));
        }
        cace_ari_cbor_encode_idseg(enc, &(obj->objpath.obj_id));

        switch (obj->params.state)
        {
            case CACE_ARI_PARAMS_NONE:
                // absense of a value, not a null/undefined
                break;
            case CACE_ARI_PARAMS_AC:
                cace_ari_cbor_encode_ac(enc, obj->params.as_ac);
                break;
            case CACE_ARI_PARAMS_AM:
                cace_ari_cbor_encode_am(enc, obj->params.as_am);
                break;
        }
        QCBOREncode_CloseArray(enc);
    }
    else
    {
        const cace_ari_lit_t *obj = &(ari->as_lit);

        if (!(obj->has_ari_type))
        {
            // no enclosing array, and only a subset of types allowed
            // per `lit-notype` rule in Section 5.2 of ARI @cite draft-ietf-dtn-ari
            if (cace_ari_cbor_encode_primval(enc, obj))
            {
                return 2;
            }
        }
        else
        {
            QCBOREncode_OpenArray(enc);
            QCBOREncode_AddInt64(enc, obj->ari_type);

            switch (obj->ari_type)
            {
                case CACE_ARI_TYPE_LABEL:
                case CACE_ARI_TYPE_ARITYPE:
                    if (cace_ari_cbor_encode_primval(enc, obj))
                    {
                        return 2;
                    }
                    break;
                case CACE_ARI_TYPE_AC:
                    if (cace_ari_cbor_encode_ac(enc, obj->value.as_ac))
                    {
                        return 2;
                    }
                    break;
                case CACE_ARI_TYPE_AM:
                    if (cace_ari_cbor_encode_am(enc, obj->value.as_am))
                    {
                        return 2;
                    }
                    break;
                case CACE_ARI_TYPE_TBL:
                    if (cace_ari_cbor_encode_tbl(enc, obj->value.as_tbl))
                    {
                        return 2;
                    }
                    break;
                case CACE_ARI_TYPE_EXECSET:
                    if (cace_ari_cbor_encode_execset(enc, obj->value.as_execset))
                    {
                        return 2;
                    }
                    break;
                case CACE_ARI_TYPE_RPTSET:
                    if (cace_ari_cbor_encode_rptset(enc, obj->value.as_rptset))
                    {
                        return 2;
                    }
                    break;
                case CACE_ARI_TYPE_OBJPAT:
                    if (cace_ari_cbor_encode_objpat(enc, obj->value.as_objpat))
                    {
                        return 2;
                    }
                    break;
                default:
                    // simple typed literals
                    if (cace_ari_cbor_encode_primval(enc, obj))
                    {
                        return 2;
                    }
                    break;
            }

            QCBOREncode_CloseArray(enc);
        }
    }
    return 0;
}

static int cace_ari_cbor_decode_primval(QCBORDecodeContext *dec, cace_ari_lit_t *lit)
{
    QCBORItem decitem;
    QCBORDecode_VGetNext(dec, &decitem);

    switch (decitem.uDataType)
    {
        case QCBOR_TYPE_UNDEF:
            lit->prim_type = CACE_ARI_PRIM_UNDEFINED;
            break;
        case QCBOR_TYPE_NULL:
            lit->prim_type = CACE_ARI_PRIM_NULL;
            break;
        case QCBOR_TYPE_TRUE:
            lit->prim_type     = CACE_ARI_PRIM_BOOL;
            lit->value.as_bool = true;
            break;
        case QCBOR_TYPE_FALSE:
            lit->prim_type     = CACE_ARI_PRIM_BOOL;
            lit->value.as_bool = false;
            break;
        case QCBOR_TYPE_INT64:
            lit->prim_type      = CACE_ARI_PRIM_INT64;
            lit->value.as_int64 = decitem.val.int64;
            break;
        case QCBOR_TYPE_UINT64:
            lit->prim_type       = CACE_ARI_PRIM_UINT64;
            lit->value.as_uint64 = decitem.val.uint64;
            break;
        case QCBOR_TYPE_FLOAT:
            lit->prim_type        = CACE_ARI_PRIM_FLOAT64;
            lit->value.as_float64 = decitem.val.fnum;
            break;
        case QCBOR_TYPE_DOUBLE:
            lit->prim_type        = CACE_ARI_PRIM_FLOAT64;
            lit->value.as_float64 = decitem.val.dfnum;
            break;
        case QCBOR_TYPE_TEXT_STRING:
            lit->prim_type = CACE_ARI_PRIM_TSTR;
            cace_data_init(&(lit->value.as_data));
            cace_data_copy_from(&(lit->value.as_data), decitem.val.string.len, (cace_data_ptr_t)decitem.val.string.ptr);
            // add the null termination
            cace_data_append_byte(&(lit->value.as_data), 0);
            break;
        case QCBOR_TYPE_BYTE_STRING:
            lit->prim_type = CACE_ARI_PRIM_BSTR;
            cace_data_init(&(lit->value.as_data));
            cace_data_copy_from(&(lit->value.as_data), decitem.val.string.len, (cace_data_ptr_t)decitem.val.string.ptr);
            break;
    }
    return 0;
}

static int cace_ari_cbor_decode_label(QCBORDecodeContext *dec, cace_ari_lit_t *lit)
{
    QCBORItem decitem;
    QCBORDecode_VGetNext(dec, &decitem);

    switch (decitem.uDataType)
    {
        case QCBOR_TYPE_INT64:
            lit->prim_type      = CACE_ARI_PRIM_INT64;
            lit->value.as_int64 = decitem.val.int64;
            break;
        case QCBOR_TYPE_UINT64:
            lit->prim_type       = CACE_ARI_PRIM_UINT64;
            lit->value.as_uint64 = decitem.val.uint64;
            break;
        case QCBOR_TYPE_TEXT_STRING:
            lit->prim_type = CACE_ARI_PRIM_TSTR;
            cace_data_init(&(lit->value.as_data));
            cace_data_copy_from(&(lit->value.as_data), decitem.val.string.len, (cace_data_ptr_t)decitem.val.string.ptr);
            cace_data_append_byte(&(lit->value.as_data), 0);
            break;
        default:
            return 3;
    }
    return 0;
}

int cace_ari_cbor_decode(cace_ari_t *ari, const cace_data_t *buf, size_t *used, char **errm)
{
    CHKERR1(ari);
    CHKERR1(buf);

    cace_ari_reset(ari);

    if (!buf->ptr || !buf->len)
    {
        if (used)
        {
            *used = 0;
        }
        return 2;
    }

    QCBORDecodeContext dec;
    UsefulBufC         indata = { .ptr = buf->ptr, .len = buf->len };
    QCBORDecode_Init(&dec, indata, QCBOR_DECODE_MODE_MAP_AS_ARRAY);

    int parse_res = cace_ari_cbor_decode_stream(&dec, ari);
    if (used)
    {
        uint32_t pos = QCBORDecode_Tell(&dec);
        // work around a weird behavior of QCBORDecode_Tell()
        if (pos == UINT32_MAX)
        {
            pos = (uint32_t)indata.len;
        }
        *used = pos;
    }
    QCBORError dec_res = QCBORDecode_Finish(&dec);

    // Figure out the result state
    int retval = 0;
    if (parse_res)
    {
        if (errm)
        {
            m_string_t err;
            m_string_init_printf(err, "parser error %d", parse_res);
            *errm = m_string_clear_get_cstr(err);
        }
        retval = 3;
    }
    else if (dec_res != QCBOR_SUCCESS)
    {
        // undecoded input
        if (dec_res == QCBOR_ERR_EXTRA_BYTES)
        {
            // user expects all input is used
            if (!used)
            {
                retval = 4;
            }
        }
        else
        {
            // any other error
            retval = 3;
        }

        if (retval && errm)
        {
            m_string_t err;
            m_string_init_printf(err, "decoder error %d: %s", dec_res, qcbor_err_to_str(dec_res));
            *errm = m_string_clear_get_cstr(err);
        }
    }
    else
    {
        if (errm)
        {
            *errm = NULL;
        }
    }

    return retval;
}

int cace_ari_cbor_decode_stream(QCBORDecodeContext *dec, cace_ari_t *ari)
{
    QCBORItem decitem;
    QCBORDecode_VPeekNext(dec, &decitem);
    if (QCBORDecode_GetError(dec))
    {
        return 3;
    }

    int retval = 0;
    if (decitem.uDataType == QCBOR_TYPE_ARRAY)
    {
        // either a typed literal or an object reference
        QCBORDecode_EnterArray(dec, &decitem);
        if (QCBORDecode_GetError(dec))
        {
            return 3;
        }
        if (decitem.val.uCount == 2)
        {
            cace_ari_lit_t *obj = cace_ari_init_lit(ari);

            int64_t type_id;
            QCBORDecode_GetInt64(dec, &type_id);
            if ((type_id < INT32_MIN) || (type_id > INT32_MAX))
            {
                return 3;
            }
            obj->ari_type     = (int32_t)type_id;
            obj->has_ari_type = true;

            switch (obj->ari_type)
            {
                case CACE_ARI_TYPE_TP:
                case CACE_ARI_TYPE_TD:
                    obj->prim_type = CACE_ARI_PRIM_TIMESPEC;
                    if (cace_ari_cbor_decode_timespec(dec, &(obj->value.as_timespec)))
                    {
                        retval = 3;
                    }
                    break;
                case CACE_ARI_TYPE_LABEL:
                case CACE_ARI_TYPE_ARITYPE:
                    if (cace_ari_cbor_decode_label(dec, obj))
                    {
                        retval = 3;
                    }
                    break;

                case CACE_ARI_TYPE_AC:
                    cace_ari_lit_init_container(obj, CACE_ARI_TYPE_AC);
                    if (cace_ari_cbor_decode_ac(dec, obj->value.as_ac))
                    {
                        retval = 3;
                    }
                    break;
                case CACE_ARI_TYPE_AM:
                    cace_ari_lit_init_container(obj, CACE_ARI_TYPE_AM);
                    if (cace_ari_cbor_decode_am(dec, obj->value.as_am))
                    {
                        retval = 3;
                    }
                    break;
                case CACE_ARI_TYPE_TBL:
                    cace_ari_lit_init_container(obj, CACE_ARI_TYPE_TBL);
                    if (cace_ari_cbor_decode_tbl(dec, obj->value.as_tbl))
                    {
                        retval = 3;
                    }
                    break;
                case CACE_ARI_TYPE_EXECSET:
                    cace_ari_lit_init_container(obj, CACE_ARI_TYPE_EXECSET);
                    if (cace_ari_cbor_decode_execset(dec, obj->value.as_execset))
                    {
                        retval = 3;
                    }
                    break;
                case CACE_ARI_TYPE_RPTSET:
                    cace_ari_lit_init_container(obj, CACE_ARI_TYPE_RPTSET);
                    if (cace_ari_cbor_decode_rptset(dec, obj->value.as_rptset))
                    {
                        retval = 3;
                    }
                    break;
                case CACE_ARI_TYPE_OBJPAT:
                {
                    cace_ari_objpat_t *pat = cace_ari_lit_init_objpat(obj);
                    if (cace_ari_cbor_decode_objpat(dec, pat))
                    {
                        return 2;
                    }
                    break;
                }
                default:
                    // simple typed literals
                    if (cace_ari_cbor_decode_primval(dec, obj))
                    {
                        retval = 3;
                    }
                    break;
            }
        }
        else if (decitem.val.uCount > 2)
        {
            int             remain = decitem.val.uCount;
            cace_ari_ref_t *obj    = cace_ari_init_objref(ari);

            int fail = 0;
            fail += cace_ari_cbor_decode_idseg(dec, &(obj->objpath.org_id));
            --remain;
            fail += cace_ari_cbor_decode_idseg(dec, &(obj->objpath.model_id));
            --remain;
            fail += cace_ari_cbor_decode_optdate(dec, &(obj->objpath.model_rev));
            if (obj->objpath.model_rev.valid)
            {
                --remain;
            }
            fail += cace_ari_cbor_decode_idseg(dec, &(obj->objpath.type_id));
            --remain;
            fail += cace_ari_cbor_decode_idseg(dec, &(obj->objpath.obj_id));
            --remain;
            if (fail)
            {
                retval = 3;
            }

            if (!retval)
            {
                // Validate AMM object type
                int err = cace_ari_objpath_derive_type(&(obj->objpath));
                if (err)
                {
                    retval = 3;
                }
            }

            obj->params.state = CACE_ARI_PARAMS_NONE;
            if (!retval && (remain == 1))
            {
                --remain;
                // some parameters present
                QCBORDecode_VPeekNext(dec, &decitem);
                if (QCBORDecode_GetError(dec))
                {
                    return 3;
                }

                // temporary holding structure
                cace_ari_lit_t params;
                switch (decitem.uDataType)
                {
                    case QCBOR_TYPE_ARRAY:
                        cace_ari_lit_init_container(&params, CACE_ARI_TYPE_AC);
                        if (cace_ari_cbor_decode_ac(dec, params.value.as_ac))
                        {
                            retval = 3;
                        }
                        obj->params.state = CACE_ARI_PARAMS_AC;
                        // move the struct
                        obj->params.as_ac = params.value.as_ac;
                        break;
                    case QCBOR_TYPE_MAP_AS_ARRAY:
                        cace_ari_lit_init_container(&params, CACE_ARI_TYPE_AM);
                        if (cace_ari_cbor_decode_am(dec, params.value.as_am))
                        {
                            retval = 3;
                        }
                        obj->params.state = CACE_ARI_PARAMS_AM;
                        // move the struct
                        obj->params.as_am = params.value.as_am;
                        break;

                    default:
                        retval = 4;
                        break;
                }
            }

            if (remain > 0)
            {
                // extra items beyond optional parameters
                retval = 5;
            }
        }

        QCBORDecode_ExitArray(dec);
        if (QCBORDecode_GetError(dec))
        {
            retval = 3;
        }
    }
    else if (decitem.uDataType == QCBOR_TYPE_MAP_AS_ARRAY)
    {
        // skip the value anyway
        QCBORDecode_VGetNextConsume(dec, &decitem);
        retval = 4;
    }
    else
    {
        // otherwise this is a single primitive value
        cace_ari_lit_t *obj = cace_ari_init_lit(ari);

        if (cace_ari_cbor_decode_primval(dec, obj))
        {
            retval = 3;
        }
    }

    if (retval)
    {
        // don't leave a partial state
        cace_ari_reset(ari);
    }
    return retval;
}
