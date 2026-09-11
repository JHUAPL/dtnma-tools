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
/*****************************************************************************
 ** \file nm_mgr.h
 **
 ** File Name: nm_mgr.h
 **
 ** Subsystem:
 **          Network Manager Application
 **
 ** Description: This file implements the DTNMP Manager user interface
 **
 ** Notes:
 **
 ** Assumptions:
 **
 ** Modification History:
 **  MM/DD/YY  AUTHOR          DESCRIPTION
 **  --------  ------------    ---------------------------------------------
 **  09/01/11  V. Ramachandran Initial Implementation (JHU/APL)
 **  08/19/13  E. Birrane      Documentation clean up and code review comments. (JHU/APL)
 **  08/21/16  E. Birrane      Update to AMP v02 (Secure DTN - NASA: NNX14CS58P)
 **  10/06/18   E. Birrane     Update to AMP v0.5 (JHU/APL)
 *****************************************************************************/

#ifndef REFDM_MGR_H_
#define REFDM_MGR_H_

#include "agents.h"
#include "instr.h"

#include "refdm/config.h"

#include "cace/amm/msg_if.h"
#include "cace/util/daemon_run.h"
#include "cace/util/threadset.h"

#include <m-dict.h>
#include <m-string.h>

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct mg_context;

#if POSTGRESQL_FOUND
#define UI_SQL_SERVERLEN (80)
#define UI_SQL_ACCTLEN   (20)
#define UI_SQL_DBLEN     (20)
#define UI_SQL_TOTLEN    (UI_SQL_SERVERLEN + UI_SQL_ACCTLEN + UI_SQL_ACCTLEN + UI_SQL_DBLEN)
/*
 * This structure holds the constants needed to store SQL database
 * information.
 */
typedef struct
{

    char *server;
    char *username;
    char *password;
    char *database;

    //    db_desc_t desc;
} refdm_db_t;
#endif

typedef enum mgr_ui_mode_enum
{
    MGR_UI_STANDARD,  // Standard Shell-Based UI
    MGR_UI_NCURSES,   // NCURSES-Based UI (currently a compile-time flag mutually exclusive with MGR_UI_STANDARD)
    MGR_UI_AUTOMATOR, // Special Altenrative UI Optimized for Automation
} mgr_ui_mode_enum;

#ifdef USE_NCURSES
#define MGR_UI_DEFAULT MGR_UI_NCURSES
#else
#define MGR_UI_DEFAULT MGR_UI_STANDARD
#endif

/// @cond Doxygen_Suppress
M_DEQUE_DEF(refdm_agent_list, refdm_agent_t *, M_PTR_OPLIST)
M_DICT_DEF2(refdm_agent_dict, const char *, M_CSTR_OPLIST, refdm_agent_t *, M_PTR_OPLIST)
/// @endcond

typedef struct refdm_mgr_s
{
    // FIXME: this is not thread safe storage
    mgr_ui_mode_enum mgr_ui_mode;
    /// Message logging options
    refdm_agent_autologging_cfg_t agent_log_cfg;

    /// Running state
    cace_daemon_run_t running;
    /// Messaging configuration
    cace_amm_msg_if_t mif;
    /// Instrumentation counters
    refdm_instr_t instr;
    /// Threads associated with the mgr
    cace_threadset_t threads;

    /// Agent state storage
    refdm_agent_list_t agent_list;
    /// Lookup agents by EID
    refdm_agent_dict_t agent_dict;
    /// Access control for #agent_list and #agent_dict
    pthread_mutex_t agent_mutex;

#if CIVETWEB_FOUND
    /// The port number to listen on
    uint16_t rest_listen_port;
    /** Static file source path.
     * @sa refdm_nm_rest_get_docroot()
     */
    m_string_t docroot_path;
    /// HTTP server state, managed by a background thread
    struct mg_context *rest;
#endif
#if POSTGRESQL_FOUND
    /// SQL client state, managed by a background thread
    refdm_db_t      sql_info;
    pthread_mutex_t sql_lock;
#endif

} refdm_mgr_t;

/** Initialize a new manager state with default config.
 *
 * @param[in,out] mgr The manager state.
 */
void refdm_mgr_init(refdm_mgr_t *mgr);

void refdm_mgr_deinit(refdm_mgr_t *mgr);

/** Begin the REFDM daemon operation.
 *
 * @pre The manager must have set values for:
 *  * refdm_mgr_t::agent_log_cfg
 *  * refdm_mgr_t::rest_listen_port
 *
 * @param[in] mgr The manager to start.
 * @sa refdm_mgr_stop()
 */
int refdm_mgr_start(refdm_mgr_t *mgr);

/** End daemon operation.
 *
 * @pre The manager must have been successfully started with
 * refdm_mgr_start().
 * @param[in] mgr The manager to stop.
 */
int refdm_mgr_stop(refdm_mgr_t *mgr);

/** Add a new known agent by its EID URI.
 *
 * @param[in] mgr The manager to update.
 * @param[in] agent_eid The unique agent EID.
 * Adding short-circuit if the EID already exists in the agent list.
 * @param[out] is_new If provided, will be set to false if the agent was
 * already present and true if it was newly added.
 * @return The new manager, or NULL if adding failed.
 */
refdm_agent_t *refdm_mgr_agent_add(refdm_mgr_t *mgr, const char *agent_eid, bool *is_new);

refdm_agent_t *refdm_mgr_agent_get_eid(refdm_mgr_t *mgr, const char *agent_eid);

refdm_agent_t *refdm_mgr_agent_get_index(refdm_mgr_t *mgr, size_t index);

/** Clear reports for a specific agent.
 */
void refdm_mgr_clear_reports(refdm_mgr_t *mgr, refdm_agent_t *agent);

#ifdef __cplusplus
}
#endif

#endif /* REFDM_MGR_H_ */
