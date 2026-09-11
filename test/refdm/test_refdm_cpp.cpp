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
 * This just verifies the ability for a C++11 executable to link with the
 * refdm library.
 */
#include <refdm/mgr.h>
#include <cace/util/logging.h>
#include <cace/util/defs.h>
#include <iostream>

/// Per-process state
static refdm_mgr_t mgr;

static int dummy_send(const cace_ari_list_t data, const cace_amm_msg_if_metadata_t *meta, const struct timespec *timeout _U_, void *ctx _U_)
{
    CHKERR1(data);
    CHKERR1(meta);
    CACE_LOG_INFO("Sending message with %d ARIs", cace_ari_list_size(data));
    return 0;
}

static int dummy_recv(cace_ari_list_t data, cace_amm_msg_if_metadata_t *meta, cace_daemon_run_t *running, void *ctx _U_)
{
    CHKERR1(data);
    CHKERR1(meta);
    CHKERR1(running);
    CACE_LOG_INFO("returning due to hangup");
    return 2;
}

int main(int argc _U_, char *argv[] _U_)
{
    // keep track of failure state
    int retval = 0;

    cace_openlog();
    refdm_mgr_init(&mgr);

    int log_limit = LOG_INFO;
    cace_log_set_least_severity(log_limit);
    CACE_LOG_DEBUG("Manager starting up with log limit %d", log_limit);

    mgr.mif.send = dummy_send;
    mgr.mif.recv = dummy_recv;

    /* Start daemon threads. */
    if (!retval)
    {
        if (refdm_mgr_start(&mgr))
        {
            CACE_LOG_ERR("Manager startup failed");
            retval = 2;
        }
        else
        {
            CACE_LOG_INFO("Manager startup completed");
        }
    }

    CACE_LOG_INFO("READY");

    if (!retval && false)
    {
        // Block until stopped
        cace_daemon_run_wait(&mgr.running);
        CACE_LOG_INFO("Manager is shutting down");
    }

    /* Join threads and wait for them to complete. */
    if (!retval)
    {
        if (refdm_mgr_stop(&mgr))
        {
            CACE_LOG_ERR("Manager stop failed");
            retval = 4;
        }
        else
        {
            CACE_LOG_INFO("Manager stopped");
        }
    }

    /* Cleanup. */
    CACE_LOG_DEBUG("Cleaning Agent Resources");
    refdm_mgr_deinit(&mgr);

    CACE_LOG_DEBUG("Manager shutdown completed");
    cace_closelog();
    return retval;
}
