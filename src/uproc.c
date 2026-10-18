#include <bits/types/idtype_t.h>
#include <errno.h>
#include <stdio.h>
#include <linux/sched.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <uipc/uipc.h>
#include <uipc/upack.h>

#include "uproc/uproc.h"

int child_process(pid_t tid, const char *server_socket, process_fn fn) {
    Connection *conn = connection_allocate(server_socket);
    if (connection_connect(conn)) {
        PacketObject *root = packet_object_new_integer(tid);
        Packet *pkt = packet_new(root);
        PacketBuffer buffer = packet_serialize(pkt);
        Message *req = message_new(UIPC_VERSION,
                MSG_REQUEST, 1, buffer, packet_buffer_size(buffer));
        connection_send_message(conn, req);
        char buf[1024];
        int n = 0;
        do { n = connection_recv(conn, buf, sizeof(buf)); } while (n > 0);
        connection_close(conn);
    }

    return fn();
}

Proc *spawn_process(process_fn fn, const char *server_socket) {
    Proc *proc = (Proc *)malloc(sizeof(Proc));
    proc->server_socket = strdup(server_socket);

    pid_t child_tid = -1;
    int child_fd = -1;
    pid_t settids[1] = {1};
    struct clone_args args = {0};
    // Create new PID namespace for child environment
    args.child_tid = (uintptr_t)&child_tid;
    args.pidfd = (uintptr_t)&child_fd;
    args.flags = CLONE_NEWPID |
                 CLONE_CHILD_SETTID |
                 CLONE_CLEAR_SIGHAND |
                 // Not setting this so a copy 
                 // file descriptors can be created
                 // for the child process
                 // CLONE_FILES |

                 // Not setting this so a copy
                 // of filesystem information can
                 // be created for the child process
                 // CLONE_FS |
                 
                 // TODO: work on cgroup (CLONE_INTO_CGROUP | CLONE_NEWCGROUP)
                 CLONE_NEWIPC |
                 CLONE_NEWNET |
                 // TODO: isolate the mount point using CLONE_NEWNS
                 CLONE_NEWUSER |
                 CLONE_NEWUTS |
                 CLONE_PIDFD;
    // Can be null unless CLONE_VM flag has been specified
    args.stack = (uintptr_t)NULL;
    args.stack_size = 0;

    // Make child send termination signal to the parent
    // When child process ends it's execution
    args.exit_signal = SIGCHLD;

    // PID must be 1 as child process will be
    // first process in it's PID namespace
    args.set_tid = (uintptr_t)settids;
    args.set_tid_size = 1;

    long tid = syscall(SYS_clone3, &args, sizeof(args));
    if (tid == -1) {
        fprintf(stderr, "ERROR: Failed to spawn a child process with error %d: %s\n", errno, strerror(errno));
        free(proc);
        return NULL;
    }
    proc->tid = tid;
    proc->fd = child_fd;
    if (tid == 0) {
        setvbuf(stdout, NULL, _IONBF, 0);
        _exit(child_process(child_tid, strdup(server_socket), fn));
    }
    if (tid > 0) {
        return proc;
    }

    free(proc);
    return NULL;
}

WaitResult proc_wait(const Proc *proc) {
    WaitResult result = {0};
    siginfo_t infop = {0};
    int wait_result = waitid(P_PIDFD, proc->fd, &infop, WEXITED);
    result.status = wait_result != -1;
    result.exit_code = infop.si_status;
    return result;
}

void proc_free(Proc *proc) {
    free(proc->server_socket);
    free(proc);
}

