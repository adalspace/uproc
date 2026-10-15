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

#define SERVER_SOCKET "./server.socket"

const Message *server_handler(Message *request, void *arg) {
    (void)arg;
    size_t payload_size = 0;
    const uint8_t *payload = message_payload(request, &payload_size);
    PacketBuffer req_buffer = packet_buffer_from_data(payload, payload_size);
    Packet *pkt = packet_deserialize(req_buffer);
    const PacketObject *root_obj = packet_root(pkt);
    printf("Got request from some child:\n---------------\n%s\n--------------\n", packet_object_stringify(root_obj));

    PacketObject *res = packet_object_new_compound();
    char msg_buffer[32] = {0};
    if (packet_object_type(root_obj) == PACKET_OBJECT_INTEGER) {
        int tid = packet_object_integer(root_obj);
        printf("TID received from child process: %d\n", tid);
        strcpy(msg_buffer, "SERVER_SUCCESS");
        packet_compound_insert(res, "status", packet_object_new_integer(0));
    } else {
        strcpy(msg_buffer, "SERVER_BAD_REQUEST");
        packet_compound_insert(res, "status", packet_object_new_integer(69));
    }
    packet_compound_insert(res, "message", packet_object_new_string(msg_buffer));
    Packet *res_pkt = packet_new(res);

    PacketBuffer res_buffer = packet_serialize(res_pkt);
    return message_new(request->version, MSG_RESPONSE, request->request_id, res_buffer, packet_buffer_size(res_buffer));
}

int child_process(pid_t tid) {
    printf("[INFO] New child process has been started on TID = %d\n", tid);
    Connection *conn = connection_allocate(SERVER_SOCKET);
    if (connection_connect(conn)) {
        PacketObject *root = packet_object_new_integer(tid);
        Packet *pkt = packet_new(root);
        PacketBuffer buffer = packet_serialize(pkt);
        Message *req = message_new(UIPC_VERSION,
                MSG_REQUEST, 1, buffer, packet_buffer_size(buffer));
        connection_send_message(conn, req);
        Message *res = connection_recv_message(conn);
        if (res) {
            printf("Got response from server process with message:\n");
            size_t res_payload_size = 0;
            const uint8_t *res_payload = message_payload(res, &res_payload_size);
            printf("[DEBUG] res_payload_size = %zu\n", res_payload_size);
            printf("[DEBUG] res_payload = %.*s\n", (int)res_payload_size, res_payload);
            PacketBuffer buffer = packet_buffer_from_data(res_payload, res_payload_size);
            printf("[DEBUG] Packet buffer = %p\n", buffer);
            Packet *pkt = packet_deserialize(buffer);
            printf("[DEBUG] Deserialized packet = %p\n", pkt);
            printf("[DEBUG] root object type = %d\n", packet_object_type(packet_root(pkt)));
            const char *dumped = packet_object_stringify(packet_root(pkt));
            printf("[DEBUG] %zu characters\n", strlen(dumped));
            printf("[DEBUG]\n---------------\n%s\n--------------\n", dumped);
            // printf("%.*s\n", (int)res_payload_size, res_payload);
        }
        connection_close(conn);
    }
    return 0;
}

int spawn_process() {
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
        return 1;
    }
    if (tid == 0) {
        setvbuf(stdout, NULL, _IONBF, 0);
        _exit(child_process(child_tid));
    }
    if (tid > 0) {
        printf("Child TID is %ld\n", tid);
        printf("Returned TID (%ld) should be equal to child_tid (%d)\n", tid, child_tid);
        printf("Child PIDFD = %d\n", child_fd);
        return child_fd;
    }
    return -1;
}

int main() {
    if (fork() == 0) {
        Server *srv = server_create(SERVER_SOCKET);
        server_register_handler(srv, server_handler, NULL);
        server_listen(srv);
        server_close(srv);
        return 0;
    }

    int child_fd = spawn_process();
    if (child_fd != -1) {
        siginfo_t infop = {0};
        int wait_result = waitid(P_PIDFD, child_fd, &infop, WEXITED);
        if (wait_result == -1) {
            fprintf(stderr, "ERROR: Failed to wait on child file descriptor");
            return 1;
        }
        printf("Child process has been successfully terminated with exit code %d\n", infop.si_status);
    }
    return 0;
}

