#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include "uipc/uipc.h"
#include "uipc/upack.h"
#include "uproc/uproc.h"

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

int foo() {
    return 69;
}

int main() {
    if (fork() == 0) {
        Server *srv = server_create(SERVER_SOCKET);
        server_register_handler(srv, server_handler, NULL);
        server_listen(srv);
        server_close(srv);
        return 0;
    }

    Proc *child_proc = spawn_process(foo, SERVER_SOCKET);
    if (child_proc) {
        WaitResult result = proc_wait(child_proc);
        if (!result.status) {
            fprintf(stderr, "ERROR: Failed to wait on child file descriptor");
            return 1;
        }
        printf("Child process has been successfully terminated with exit code %d\n", result.exit_code);
    }
    return 0;
}
