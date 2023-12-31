/*
 * Copyright (c) 2018-2023 The Johns Hopkins University Applied Physics
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

/*****************************************************************************
 **
 ** File Name: blob.h
 **
 ** Subsystem:
 **          Primitive Types
 **
 ** Description: This file contains the definitions, prototypes, constants, and
 **              other information necessary for the identification and
 **              processing of Binary Large Objects (BLOBs).
 **
 ** Notes:
 **
 ** Assumptions:
 **
 **
 ** Modification History:
 **  MM/DD/YY  AUTHOR         DESCRIPTION
 **  --------  ------------   ---------------------------------------------
 **  04/14/16  E. Birrane     Initial Implementation (Secure DTN - NASA: NNX14CS58P)
 **  11/18/18  E. Birrane     Updates for new AMP. (JHU/APL).
 *****************************************************************************/

#ifndef BLOB_H_
#define BLOB_H_

#include <stdint.h>
#include <qcbor/qcbor.h>


#ifdef __cplusplus
extern "C" {
#endif

/*
 * +--------------------------------------------------------------------------+
 * |							  CONSTANTS  								  +
 * +--------------------------------------------------------------------------+
 */


/*
 * +--------------------------------------------------------------------------+
 * |							  	MACROS  								  +
 * +--------------------------------------------------------------------------+
 */

#ifndef SMALL_SIZES
#define SMALL_SIZES 100
#endif
#ifndef WORD_SIZE
#define WORD_SIZE 8
#endif
#define BLOB_DEFAULT_ENC_SIZE (SMALL_SIZES * WORD_SIZE)

/*
 * +--------------------------------------------------------------------------+
 * |							  DATA TYPES  								  +
 * +--------------------------------------------------------------------------+
 */


/**
 * The BLOB is a self-delineating structure that captured arbitrary user data.
 */
typedef struct {
	uint8_t *value; /**> The data associated with the entry. */
	size_t length;  /**> The length of the data in bytes. */
	size_t alloc;   /**> The allocated size of the entry. */
} blob_t;


/*
 * +--------------------------------------------------------------------------+
 * |						  FUNCTION PROTOTYPES  							  +
 * +--------------------------------------------------------------------------+
 */

int       blob_append(blob_t *blob, uint8_t *buffer, uint32_t length);
blob_t*   blob_create(uint8_t *value, size_t length, size_t alloc);
int       blob_compare(blob_t* v1, blob_t *v2);
int       blob_copy(blob_t src, blob_t *dest);
blob_t*   blob_copy_ptr(blob_t *src);

blob_t    blob_deserialize(QCBORDecodeContext *it, int *success);
blob_t    blob_deserialize_as_bytes(QCBORDecodeContext *it, int *success, size_t len);
blob_t*   blob_deserialize_as_bytes_ptr(QCBORDecodeContext *it, int *success, size_t len);
blob_t*   blob_deserialize_ptr(QCBORDecodeContext *it, int *success);
int       blob_serialize(QCBOREncodeContext *encoder, blob_t *item);
int       blob_serialize_as_bytes(QCBOREncodeContext *it, blob_t *blob);

int       blob_grow(blob_t *blob, uint32_t length);
int       blob_init(blob_t *blob, uint8_t *value, size_t length, size_t alloc);
void      blob_release(blob_t *blob, int destroy);

blob_t*   blob_serialize_wrapper(blob_t *blob);
int8_t    blob_trim(blob_t *blob, uint32_t length);


#ifdef __cplusplus
}
#endif

#endif  /* _BLOB_H_ */
