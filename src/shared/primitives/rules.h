/*
 * Copyright (c) 2012-2023 The Johns Hopkins University Applied Physics
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
 ** \file rules.h
 **
 **
 ** Description: This module contains the functions, structures, and other
 **              information necessary to describe Time-Based Rules (TRLs)
 **              and State-Based Rules (SRLs).
 **
 ** Assumptions:
 **
 ** Modification History:
 **  MM/DD/YY  AUTHOR         DESCRIPTION
 **  --------  ------------   ---------------------------------------------
 **  11/08/12  E. Birrane     Redesign of messaging architecture. (JHU/APL)
 **  06/24/13  E. Birrane     Migrated from uint32_t to time_t. (JHU/APL)
 **  05/17/15  E. Birrane     Moved controls to ctrl.[h|c]. Updated TRL/SRL to DTNMP v0.1  (Secure DTN - NASA: NNX14CS58P)
 **  06/26/15  E. Birrane     Updated structures/functs to reflect TRL/SRL naming.  (Secure DTN - NASA: NNX14CS58P)
 **  09/29/18  E. Birrane     Updated to AMPv0.5 (JHU/APL)
 *****************************************************************************/

#ifndef _RULES_H_
#define _RULES_H_

#include "../utils/nm_types.h"
#include "../utils/db.h"
#include "../primitives/ctrl.h"
#include "../primitives/expr.h"


#ifdef __cplusplus
extern "C" {
#endif

/*
 * +--------------------------------------------------------------------------+
 * |							  CONSTANTS  								  +
 * +--------------------------------------------------------------------------+
 */

#define RULE_DEFAULT_ENC_SIZE 1024

#define RULE_ACTIVE    (0x1)

#define AMP_RULE_EXEC_ALWAYS (-1)

/*
 * +--------------------------------------------------------------------------+
 * |							  	MACROS  								  +
 * +--------------------------------------------------------------------------+
 */

#define RULE_IS_ACTIVE(flags)    (flags & RULE_ACTIVE)
#define RULE_SET_ACTIVE(flags)   (flags |= RULE_ACTIVE)
#define RULE_CLEAR_ACTIVE(flags) (flags &= (~RULE_ACTIVE))


/*
 * +--------------------------------------------------------------------------+
 * |							  DATA TYPES  								  +
 * +--------------------------------------------------------------------------+
 */


typedef struct
{
	expr_t expr;       /**> If this evals to true, run action.      */
	amp_uvast max_fire; /**> # times to run action.                  */
	amp_uvast max_eval; /**> # times to eval expression.             */
} sbr_def_t;


typedef struct
{
	OS_time_t period;   /**> # ticks between rule firings.           */
	amp_uvast max_fire; /**> # times action can be run.              */
} tbr_def_t;


/*
 *  We support 2 serializations/deserializations. One for just
 *  the rule definition when communicationg over the wire to the
 *  AMP spec. And another for when we persist the object to a DB.
 *
 * +-----------+--------+--------+
 * | sbr_def_t | ticks  | flags  |
 * | [BYTESTR] | [UINT] | [BYTE] |
 * +-----------+--------+--------+
 *
 */
typedef struct {
	ari_t id;          /**> The identifier for the SBR definition.  */
	OS_time_t start;       /**> When to start the evaluation.           */

	union {
		sbr_def_t as_sbr;
		tbr_def_t as_tbr;
	} def;

	ac_t action;      /**> Action to run if expr is true on eval.  */

	/** Everything below is not part of a rule definition. **/

	OS_time_t eval_at;   /**> Time of next eval.      */
	amp_uvast    num_eval;   /**> Number of times rule evaluated.       */
	amp_uvast    num_fire;   /**> Number of times a rule action was run. */
	uint8_t  flags;      /**> Status of rule: Active or not.        */

	db_desc_t desc;      /**> SDR info. for persistent storage.     */
} rule_t;





/*
 * +--------------------------------------------------------------------------+
 * |						  FUNCTION PROTOTYPES  							  +
 * +--------------------------------------------------------------------------+
 */

int       rule_cb_comp_fn(void *i1, void *i2);

void      rule_cb_del_fn(void *item);
void      rule_cb_ht_del_fn(rh_elt_t *elt);


rule_t*   rule_copy_ptr(rule_t *rule);

rule_t*   rule_create_sbr(ari_t id, OS_time_t start, sbr_def_t def, ac_t action);

rule_t*   rule_create_tbr(ari_t id, OS_time_t start, tbr_def_t def, ac_t action);

rule_t*   rule_deserialize_helper(QCBORDecodeContext *it, int *success);

rule_t*   rule_deserialize_ptr(QCBORDecodeContext *it, int *success);

rule_t*   rule_deserialize_raw(blob_t *data, int *success);

rule_t*   rule_db_deserialize_ptr(QCBORDecodeContext *it, int *success);
rule_t*   rule_db_deserialize_raw(blob_t *data, int *success);

int rule_db_serialize(QCBOREncodeContext *encoder, void *item);
blob_t*   rule_db_serialize_wrapper(rule_t *rule);


void      rule_release(rule_t *rule, int destroy);


int rule_serialize(QCBOREncodeContext *encoder, void *item);

int rule_serialize_helper(QCBOREncodeContext *encoder, rule_t *rule);

blob_t*   rule_serialize_wrapper(rule_t *rule);

int	      sbr_should_fire(rule_t *rule);

sbr_def_t sbrdef_deserialize(QCBORDecodeContext *array_it, int *success);

int sbrdef_serialize(QCBOREncodeContext *encoder, sbr_def_t *def);


tbr_def_t tbrdef_deserialize(QCBORDecodeContext *it, int *success);

int tbrdef_serialize(QCBOREncodeContext *encoder, tbr_def_t *def);


#ifdef __cplusplus
}
#endif

#endif /* _RULES_H_ */
