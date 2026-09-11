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
 * @ingroup group_amp
 * Provide a POSIX datagram socket (AF_UNIX) adapter for AMP messaging.
 */
#include "socket.h"

#include "msg.h"

#include "cace/ari/text.h"
#include "cace/ari/time_util.h"
#include "cace/util/defs.h"
#include "cace/util/logging.h"

#include <m-bstring.h>

#include <errno.h>
#include <signal.h>
#include <strings.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static const char *URI_PREFIX = "file:";

const char *cace_amp_socket_strip_scheme(const char *uri)
{
    const size_t prefix_len = strlen(URI_PREFIX);

    if (!uri)
    {
        CACE_LOG_ERR("Given null URI");
        return NULL;
    }
    if (strncasecmp(uri, URI_PREFIX, prefix_len) == 0)
    {
        return uri + prefix_len;
    }
    CACE_LOG_ERR("given dest that is not a \"%s\" scheme: %s", URI_PREFIX, uri);

    if (strncmp(uri, "/", 1) == 0)
    {
        // looks like a path
        return uri;
    }
    return NULL;
}

void cace_amp_socket_state_init(cace_amp_socket_state_t *state)
{
    CHKVOID(state);
    m_string_init(state->path);
    state->sock_fd = -1;
}

void cace_amp_socket_state_deinit(cace_amp_socket_state_t *state)
{
    CHKVOID(state);
    cace_amp_socket_state_unbind(state);
    m_string_clear(state->path);
}

int cace_amp_socket_state_bind(cace_amp_socket_state_t *state, const char *sock_path)
{
    CHKERR1(state);
    CHKERR1(sock_path);

    struct sockaddr_un laddr;
    laddr.sun_family = AF_UNIX;
    char *sun_end    = stpncpy(laddr.sun_path, sock_path, sizeof(laddr.sun_path));
    if (sun_end - laddr.sun_path >= (ssize_t)sizeof(laddr.sun_path))
    {
        CACE_LOG_ERR("given path that is too long to fit in sockaddr_un");
        return 1;
    }
    // now copy the path
    m_string_set_cstr(state->path, sock_path);

    // Set up listen socket
    state->sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (state->sock_fd < 0)
    {
        CACE_LOG_ERR("Failed to construct socket on family AF_UNIX with errno %d", errno);
        return 2;
    }

    // preemptive unlink
    unlink(m_string_get_cstr(state->path));

    CACE_LOG_DEBUG("Binding to socket %s", laddr.sun_path);
    int res = bind(state->sock_fd, (struct sockaddr *)&laddr, sizeof(laddr));
    if (res)
    {
        CACE_LOG_ERR("Failed to bind to socket %s with errno %d", laddr.sun_path, errno);
        cace_amp_socket_state_unbind(state);
        return 3;
    }

    return 0;
}

void cace_amp_socket_state_unbind(cace_amp_socket_state_t *state)
{
    const char *path = m_string_get_cstr(state->path);

    if (state->sock_fd >= 0)
    {
        CACE_LOG_DEBUG("Unbinding from socket %s", path);
        close(state->sock_fd);
        state->sock_fd = -1;
    }

    if (path && unlink(path) && (errno != ENOENT))
    {
        CACE_LOG_WARNING("Failed to remove socket %s with errno %d", path, errno);
    }

    m_string_reset(state->path);
}

int cace_amp_socket_send(const cace_ari_list_t data, const cace_amm_msg_if_metadata_t *meta,
                         const struct timespec *timeout, void *ctx)
{
    CHKERR1(data);
    CHKERR1(meta);
    cace_amp_socket_state_t *state = ctx;
    CHKERR1(state);
    CHKERR1(state->sock_fd >= 0);

    int retval = 0;

    const char *dest_eid = cace_ari_cget_tstr_cstr(&meta->dest);
    if (!dest_eid)
    {
        m_string_t buf;
        m_string_init(buf);
        cace_ari_text_encode(buf, &meta->dest, CACE_ARI_TEXT_ENC_OPTS_DEFAULT);
        CACE_LOG_ERR("This transport can only send to text URI destinations, not %s", m_string_get_cstr(buf));
        m_string_clear(buf);

        return 6;
    }

    const char *dest_path = cace_amp_socket_strip_scheme(dest_eid);
    if (!dest_path)
    {
        CACE_LOG_ERR("given dest which is not a socket path %s", dest_eid);
        return 6;
    }

    struct sockaddr_un daddr;
    daddr.sun_family = AF_UNIX;
    char *sun_end    = stpncpy(daddr.sun_path, dest_path, sizeof(daddr.sun_path));
    if (sun_end - daddr.sun_path >= (ssize_t)sizeof(daddr.sun_path))
    {
        CACE_LOG_ERR("given dest that is too long to fit in sockaddr_un");
        return 1;
    }

    CACE_LOG_DEBUG("Sending message with %d ARIs", cace_ari_list_size(data));
    m_bstring_t msgbuf;
    m_bstring_init(msgbuf);
    if (cace_amp_msg_encode(msgbuf, data))
    {
        retval = 2;
    }

    // respect the timeout
    if (!retval && timeout)
    {
        struct pollfd fds[1] = { { .fd = state->sock_fd, .events = POLLOUT } };

        int res = poll(fds, 1, timeout->tv_sec);
        if (res == 0)
        {
            CACE_LOG_WARNING("timed out waiting to send");
            retval = 2;
        }
        else if (res < 0)
        {
            CACE_LOG_WARNING("failed poll() with errno %d", errno);
            retval = 2;
        }

        if (!(fds[0].revents & POLLOUT))
        {
            CACE_LOG_WARNING("poll waited but still not ready");
            retval = 2;
        }
    }

    if (!retval)
    {
        const size_t   msg_size  = m_bstring_size(msgbuf);
        const uint8_t *msg_begin = m_bstring_view(msgbuf, 0, msg_size);

        int flags = 0;
        CACE_LOG_DEBUG("sending datagram with %zd octets to %s", msg_size, daddr.sun_path);
        ssize_t got = sendto(state->sock_fd, msg_begin, msg_size, flags, (struct sockaddr *)&daddr, sizeof(daddr));
        if (got < 0)
        {
            CACE_LOG_WARNING("failed sendto() with errno %d", errno);
            retval = 3;
        }
        else if (got < (ssize_t)msg_size)
        {
            CACE_LOG_ERR("failed sendto(), sent only %z of %z", got, msg_size);
            retval = 4;
        }
    }

    m_bstring_clear(msgbuf);

    return retval;
}

int cace_amp_socket_recv(cace_ari_list_t data, cace_amm_msg_if_metadata_t *meta, cace_daemon_run_t *running, void *ctx)
{
    CHKERR1(data);
    CHKERR1(meta);
    CHKERR1(running);
    cace_amp_socket_state_t *state = ctx;
    CHKERR1(state);
    CHKERR1(state->sock_fd >= 0);

    // Watch stdin (fd 0) for input, assuming whole-lines are given
    struct pollfd pfds[] = {
        { .fd = state->sock_fd, .events = POLLIN | POLLERR | POLLHUP },
    };
    struct pollfd *poll_sock = pfds + 0;

    int retval = 0;

    m_bstring_t msgbuf;
    m_bstring_init(msgbuf);
    while (!retval)
    {
        // Wait up to 1 second
        int res = poll(pfds, sizeof(pfds) / sizeof(struct pollfd), 1000);
        if (res < 0)
        {
            retval = CACE_AMM_MSG_IF_RECV_END;
            break;
        }
        else if (res == 0)
        {
            // nothing ready, but maybe daemon is shutting down
            if (!cace_daemon_run_get(running))
            {
                CACE_LOG_DEBUG("returning due to running state change");
                retval = CACE_AMM_MSG_IF_RECV_END;
                break;
            }
            continue;
        }

        if (poll_sock->revents & POLLIN)
        {
            int     flags = MSG_PEEK | MSG_TRUNC;
            ssize_t got   = recvfrom(poll_sock->fd, NULL, 0, flags, NULL, NULL);
            if (got <= 0)
            {
                CACE_LOG_WARNING("ignoring failed recvfrom() with errno %d", errno);
                continue;
            }
            CACE_LOG_DEBUG("peeked datagram with %zd octets", got);

            m_bstring_resize(msgbuf, got);

            struct sockaddr_un saddr;
            saddr.sun_family = AF_UNIX;

            const size_t msg_size  = m_bstring_size(msgbuf);
            uint8_t     *msg_begin = m_bstring_acquire_access(msgbuf, 0, msg_size);

            socklen_t saddr_len = sizeof(saddr);

            flags = 0;
            got   = recvfrom(poll_sock->fd, msg_begin, msg_size, flags, (struct sockaddr *)&saddr, &saddr_len);
            m_bstring_release_access(msgbuf);
            if (got < 0)
            {
                CACE_LOG_WARNING("ignoring failed recvfrom() with errno %d", errno);
                continue;
            }

            m_string_t srcbuf;
            m_string_init(srcbuf);
            m_string_printf(srcbuf, "%s%s", URI_PREFIX, saddr.sun_path);
            CACE_LOG_DEBUG("read datagram with %zd octets from %s", got, m_string_get_cstr(srcbuf));
            cace_ari_set_tstr(&meta->src, m_string_get_cstr(srcbuf), true);
            cace_get_system_time(&meta->timestamp);
            m_string_clear(srcbuf);

            // stop when something received
            break;
        }
        if (poll_sock->revents & (POLLERR | POLLHUP))
        {
            // input has closed
            CACE_LOG_DEBUG("returning due to hangup");
            retval = CACE_AMM_MSG_IF_RECV_END;
        }
    }

    if (!retval)
    {
        const size_t   msgbuf_len = m_bstring_size(msgbuf);
        const uint8_t *msgbuf_ptr = m_bstring_view(msgbuf, 0, msgbuf_len);
        if (cace_amp_msg_decode(data, msgbuf_ptr, msgbuf_len))
        {
            retval = 5;
        }
        CACE_LOG_DEBUG("decoded %d ARI items in the datagram", cace_ari_list_size(data));
    }
    m_bstring_clear(msgbuf);

    return retval;
}
