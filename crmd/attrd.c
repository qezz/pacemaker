/*
 * Copyright (C) 2006-2017 Andrew Beekhof <andrew@beekhof.net>
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#include <crm_internal.h>

#include <crm/crm.h>
#include <crm/attrd.h>
#include <crm/msg_xml.h>

#include <crmd_fsa.h>
#include <crmd_utils.h>
#include <crmd_messages.h>

crm_ipc_t *attrd_ipc = NULL;

#if !HAVE_ATOMIC_ATTRD
static int
update_without_attrd(const char *host_uuid, const char *name, const char *value,
                     const char *user_name, gboolean is_remote_node,
                     char command)
{
	crm_info("my trace");
    int call_opt = cib_none;

    if (fsa_cib_conn == NULL) {
        return -1;
    }

    call_opt = crmd_cib_smart_opt();

    crm_trace("updating status for host_uuid %s, %s=%s",
              host_uuid, (name? name : "<null>"), (value? value : "<null>"));
    if (value) {
        return update_attr_delegate(fsa_cib_conn, call_opt, XML_CIB_TAG_STATUS,
                                    host_uuid, NULL, NULL, NULL, name, value,
                                    FALSE, user_name,
                                    (is_remote_node? "remote" : NULL));
    } else {
        return delete_attr_delegate(fsa_cib_conn, call_opt, XML_CIB_TAG_STATUS,
                                    host_uuid, NULL, NULL, NULL, name, NULL,
                                    FALSE, user_name);
    }
}
#endif

static void
log_attrd_error(const char *host, const char *name, const char *value,
                gboolean is_remote, char command, int rc)
{
	crm_info("my trace");
    const char *display_command; /* for commands without name/value */
    const char *node_type = (is_remote? "Pacemaker Remote" : "cluster");
    gboolean shutting_down = is_set(fsa_input_register, R_SHUTDOWN);
    const char *when = (shutting_down? " at shutdown" : "");

    switch (command) {
        case 'R':
            display_command = "refresh";
            break;
        case 'C':
            display_command = "purge";
            break;
        default:
            display_command = NULL;
    }

    if (display_command) {
        crm_err("Could not request %s of %s node %s%s: %s (%d)",
                display_command, node_type, host, when, pcmk_strerror(rc), rc);
    } else {
        crm_err("Could not request update of %s=%s for %s node %s%s: %s (%d)",
                name, value, node_type, host, when, pcmk_strerror(rc), rc);
    }

    /* If we can't request shutdown via attribute, fast-track it */
    if ((command == 'U') && shutting_down) {
        register_fsa_input(C_FSA_INTERNAL, I_FAIL, NULL);
    }
}

static void
update_attrd_helper(const char *host, const char *name, const char *value,
                    const char *user_name, gboolean is_remote_node,
                    char command)
{
	crm_info("my trace");
    int rc;
    int max = 5;
    int attrd_opts = attrd_opt_none;

    if (is_remote_node) {
        attrd_opts |= attrd_opt_remote;

#if !HAVE_ATOMIC_ATTRD
        /* Legacy attrd can handle remote peer remove ('C') requests,
         * otherwise talk directly to cib for remote nodes.
         */

        /* host is required for updating a remote node */
        CRM_CHECK(host != NULL, return;);

        if (command != 'C') {
            /* remote node uname and uuid are equal */
            rc = update_without_attrd(host, name, value, user_name,
                                      is_remote_node, command);
            if (rc < pcmk_ok) {
                log_attrd_error(host, name, value, is_remote_node, command, rc);
            }
            return;
        }
#endif
    }

    if (attrd_ipc == NULL) {
        attrd_ipc = crm_ipc_new(T_ATTRD, 0);
    }

    do {
        if (crm_ipc_connected(attrd_ipc) == FALSE) {
            crm_ipc_close(attrd_ipc);
            crm_info("Connecting to attribute manager ... %d retries remaining",
                     max);
            if (crm_ipc_connect(attrd_ipc) == FALSE) {
                crm_perror(LOG_INFO, "Connection to attribute manager failed");
            }
        }

        rc = attrd_update_delegate(attrd_ipc, command, host, name, value,
                                   XML_CIB_TAG_STATUS, NULL, NULL, user_name,
                                   attrd_opts);
        if (rc == pcmk_ok) {
            break;

        } else if (rc != -EAGAIN && rc != -EALREADY) {
            crm_info("Disconnecting from attribute manager: %s (%d)",
                     pcmk_strerror(rc), rc);
            crm_ipc_close(attrd_ipc);
        }

        sleep(5 - max);

    } while (max--);

    if (rc != pcmk_ok) {
        log_attrd_error(host, name, value, is_remote_node, command, rc);
    }
}

void
update_attrd(const char *host, const char *name, const char *value,
             const char *user_name, gboolean is_remote_node)
{
	crm_info("my trace");
    update_attrd_helper(host, name, value, user_name, is_remote_node, 'U');
}

void
update_attrd_remote_node_removed(const char *host, const char *user_name)
{
	crm_info("my trace");
    crm_trace("Asking attrd to purge Pacemaker Remote node %s", host);
    update_attrd_helper(host, NULL, NULL, user_name, TRUE, 'C');
}
