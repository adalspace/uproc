#define _GNU_SOURCE

#include <asm-generic/errno-base.h>
#include <asm-generic/socket.h>
#include <bits/types/idtype_t.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/sched.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mount.h>
#include <assert.h>
#include <unistd.h>
#include <uipc/uipc.h>
#include <uipc/upack.h>

#include "uproc/uproc.h"

static int write_proc_file(pid_t pid, const char *proc_file, const char *data) {
    char file[256];
    if (snprintf(file, sizeof(file), "/proc/%ld/%s", (long)pid, proc_file) <= 0)
    {
        return 1;
    }
    int fd = open(file, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        return 1;
    }
    int wrote = write(fd, data, strlen(data));
    close(fd);
    return wrote;
}

static int isolate_root(const char *root) {
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1) return -1;
    if (mount(root, root, NULL, MS_BIND | MS_REC, NULL) == -1) return -1;
    if (chdir(root)) return -1;
    if (mkdir(".old_root", 0700) && errno != EEXIST) return -1;
    if (syscall(SYS_pivot_root, ".", ".old_root")) return -1;
    if (chdir("/")) return -1;
    if (umount2("/.old_root", MNT_DETACH)) return -1;
    if (rmdir("/.old_root")) return -1;
    return 0;
}

static int uproc_execute(
    const UProcExecutable *executable,
    pid_t *out_pid
) {
    int exec_pipe[2];

    if (pipe2(exec_pipe, O_CLOEXEC) == -1)
        return -1;

    pid_t pid = fork();

    if (pid == -1) {
        int err = errno;

        close(exec_pipe[0]);
        close(exec_pipe[1]);

        errno = err;
        return -1;
    }

    if (pid == 0) {
        // PID 2 in the UProc PID namespace.

        close(exec_pipe[0]);

        // Close inherited private daemon control socket
        // and other unwanted file descriptors before exec.

        execve(
            executable->path,
            (char *const *)executable->argv,
            (char *const *)executable->envp
        );

        // Only reached when execve() fails.
        int err = errno;

        // Report the failure to PID 1.
        const unsigned char *p =
            (const unsigned char *)&err;

        size_t remaining = sizeof(err);

        while (remaining > 0) {
            ssize_t n = write(
                exec_pipe[1],
                p,
                remaining
            );

            if (n == -1 && errno == EINTR)
                continue;

            if (n <= 0)
                break;

            p += n;
            remaining -= (size_t)n;
        }

        _exit(127);
    }

    // PID 1
    close(exec_pipe[1]);

    int exec_errno = 0;
    unsigned char *p = (unsigned char *)&exec_errno;
    size_t received = 0;

    while (received < sizeof(exec_errno)) {
        ssize_t n = read(
            exec_pipe[0],
            p + received,
            sizeof(exec_errno) - received
        );

        if (n == -1 && errno == EINTR)
            continue;

        if (n == -1) {
            int err = errno;
            close(exec_pipe[0]);
            errno = err;
            return -1;
        }

        if (n == 0)
            break;

        received += (size_t)n;
    }

    close(exec_pipe[0]);

    if (received != 0) {
        // PID 2 reported execve() failure.
        int status;

        while (waitpid(pid, &status, 0) == -1 &&
               errno == EINTR) {}

        errno = received == sizeof(exec_errno)
              ? exec_errno
              : EIO;

        return -1;
    }

    *out_pid = pid;
    return 0;
}

static int proc_child_wrapper(UProc *proc, UProcConfig config) {
    Connection *conn = connection_from_socket(proc->server_fd, NULL);

    // Transaction
    Message *ack = connection_recv_message(conn);
    Packet *ack_pkt = packet_from_message(ack);
    printf("[CHILD] Received ACK message from UDaemon:\n%s\n", packet_object_stringify(packet_root(ack_pkt)));

    PacketObject *root = packet_object_new_compound();
    packet_compound_insert(root, "daemon_id", packet_object_new_integer(proc->daemon_id));
    packet_compound_insert(root, "pid", packet_object_new_integer(getpid()));

    Packet *pkt = packet_new(root);
    PacketBuffer buffer = packet_serialize(pkt);
    Message *req = message_new(UIPC_VERSION,
        MSG_REQUEST, 1, buffer, packet_buffer_size(buffer));
    connection_send_message(conn, req);

    connection_close(conn);
    uipc_free(conn);

    if (setresgid(config.child_gid, config.child_gid, config.child_gid) == -1) {
        perror("setresgid");
        return 1;
    }

    if (setresuid(config.child_uid, config.child_uid, config.child_uid) == -1) {
        perror("setresuid");
        return 1;
    }

    if (!config.root_dir || isolate_root(config.root_dir) == -1) {
        fprintf(stderr, "[PID1] failed to isolate root to ('%s')\n", config.root_dir);
        return 1;
    }

    const char *work_dir = config.work_dir ? config.work_dir : "/";

    if (chdir(work_dir) == -1) {
        fprintf(stderr, "[PID1] failed to chdir to workdir ('%s')\n", work_dir);
        return 1;
    }

    pid_t workload_pid = -1;
    if (uproc_execute(&config.executable, &workload_pid) == -1) {
        fprintf(stderr, "[PID1] failed to execute '%s': %s\n", config.executable.path, strerror(errno));
        return 1;
    }

    fprintf(stderr, "[PID1] workload_pid = %d\n", workload_pid);

    // TODO: notify UDaemon of successfull exec
    // report_workload_started(proc, workload_pid);

    int status;

    for (;;) {
        pid_t result = waitpid(
            workload_pid,
            &status,
            0
        );

        if (result == -1 && errno == EINTR)
            continue;

        if (result == -1)
            return 1;

        break;
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);

    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);

    return 1;
}

UDaemon *udaemon_init(const char *udaemon_address) {
    UDaemon *udaemon = calloc(1, sizeof(*udaemon));
    if (!udaemon) return NULL;

    udaemon->server_address = strdup(udaemon_address);
    if (!udaemon->server_address) {
        free(udaemon);
        return NULL;
    }

    atomic_init(&udaemon->next_id, 1);
    return udaemon;
}

UProcID udaemon_next_id(UDaemon *daemon) {
    return atomic_fetch_add_explicit(
        &daemon->next_id,
        1,
        memory_order_relaxed
    );
}

UProc *udaemon_create_proc(UDaemon *udaemon, UProcConfig config) {
    UProc *proc = calloc(1, sizeof(*proc));
    if (!proc)
        return NULL;

    int fds[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, (int*)&fds) == -1) {
        free(proc);
        return NULL;
    }

    proc->daemon_id = udaemon_next_id(udaemon);
    proc->pidfd = -1;

    struct clone_args args = {0};

    args.pidfd = (uintptr_t)&proc->pidfd;
    args.flags = CLONE_NEWPID |
                 // CLONE_CHILD_SETTID |
                 CLONE_CLEAR_SIGHAND |
                 // TODO: work on cgroup (CLONE_INTO_CGROUP | CLONE_NEWCGROUP)
                 CLONE_NEWIPC |
                 CLONE_NEWNET |
                 // TODO: isolate the mount point using CLONE_NEWNS
                 CLONE_NEWNS |
                 CLONE_NEWUSER |
                 CLONE_NEWUTS |
                 CLONE_PIDFD;
    args.exit_signal = SIGCHLD;

    long pid = syscall(SYS_clone3, &args, sizeof(args));
    if (pid == -1) {
        fprintf(stderr, "ERROR: Failed to spawn a child process with error %d: %s\n", errno, strerror(errno));
        free(proc);
        close(fds[0]);
        close(fds[1]);
        return NULL;
    }

    if (pid == 0) {
        close(fds[0]);
        proc->server_fd = fds[1];
        int status = proc_child_wrapper(proc, config);
        close(fds[1]);
        _exit(status);
    }

    proc->pid = pid;

    close(fds[1]);
    Connection *child_connection = connection_from_socket(fds[0], NULL);

    char mapping[128];
    assert(snprintf(mapping, sizeof(mapping), "%d %d 1", config.child_uid, config.host_uid) > 0);
    if (write_proc_file(proc->pid, "uid_map", mapping) < 0) {
        close(fds[0]);
        close(fds[1]);
        udaemon_kill_proc(proc);
        free(proc);
        return NULL;
    }

    if (write_proc_file(proc->pid, "setgroups", "deny") < 0) {
        close(fds[0]);
        close(fds[1]);
        udaemon_kill_proc(proc);
        free(proc);
        return NULL;
    }

    assert(snprintf(mapping, sizeof(mapping), "%d %d 1", config.child_gid, config.host_gid) > 0);
    if (write_proc_file(proc->pid, "gid_map", mapping) < 0) {
        close(fds[0]);
        close(fds[1]);
        udaemon_kill_proc(proc);
        free(proc);
        return NULL;
    }

    Packet *pkt = packet_new(packet_object_new_string("MAIN PROCESS IS READY"));
    PacketBuffer buffer = packet_serialize(pkt);
    connection_send_message(child_connection, message_new(UIPC_VERSION, MSG_REQUEST, 1, buffer, packet_buffer_size(buffer)));

    Message *res = connection_recv_message(child_connection);
    Packet *res_pkt = packet_from_message(res);
    pid_t peerid = uipc_peer_pid(child_connection);
    printf("[UDAEMON] Received response from child process with PID (%d == %d):\n%s\n", proc->pid, peerid, packet_object_stringify(packet_root(res_pkt)));

    connection_close(child_connection);
    uipc_free(child_connection);

    return proc;
}

int udaemon_kill_proc(const UProc *proc) {
    return syscall(SYS_pidfd_send_signal, proc->pidfd, SIGKILL, NULL, 0);
}

UProcWaitResult udaemon_wait_proc(const UProc *proc) {
    UProcWaitResult result = {0};
    siginfo_t infop = {0};
    int wait_result = waitid(P_PIDFD, proc->pidfd, &infop, WEXITED);
    result.status = wait_result != -1;
    result.exit_code = infop.si_status;
    return result;
}

void udaemon_free(UDaemon *udaemon) {
    if (!udaemon) return;
    if (udaemon->server_address)
        free(udaemon->server_address);
    free(udaemon);
}
