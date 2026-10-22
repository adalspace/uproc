#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <dirent.h>
#include "uproc/uproc.h"

#define SERVER_SOCKET "./server.socket"

int main() {
    UDaemon *daemon = udaemon_init(SERVER_SOCKET);

    const char *argv[] = {
        "/usr/bin/python3",
        "/app/main.py",
        NULL
    };

    const char *envp[] = {
        "PATH=/usr/bin:/bin",
        "HOME=/app",
        "LANG=C.UTF-8",
        NULL
    };

    for (size_t i = 0; i < 2; i++) {
        UProcConfig config = {
            .executable = {
                .path = "/usr/bin/python3",
                .argv = argv,
                .envp = envp,
            },
            .child_uid = 0,
            .host_uid = geteuid(),
            .child_gid = 0,
            .host_gid = getegid(),
            .root_dir = "./rootfs",
            .work_dir = "/app"
        };
        UProc *child_proc = udaemon_create_proc(daemon, config);
        if (child_proc) {
            UProcWaitResult result = udaemon_wait_proc(child_proc);
            if (!result.status) {
                fprintf(stderr, "ERROR: Failed to wait on child file descriptor");
                return 1;
            }
            printf("Child process has been successfully terminated with exit code %d\n", result.exit_code);
        } else {
            fprintf(stderr, "[DAEMON] ERROR: Failed to create child process\n");
        }
    }

    udaemon_free(daemon);

    return 0;
}
