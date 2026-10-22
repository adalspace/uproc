#ifndef HEADER_UPROC_
#define HEADER_UPROC_

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <sched.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t UProcID;

typedef struct {
    char *server_address;
    atomic_uint_fast64_t next_id;
} UDaemon;

UDaemon *udaemon_init(const char *udaemon_address);

UProcID udaemon_next_id(UDaemon *daemon);

typedef struct {
    uint64_t daemon_id;
    int server_fd;

    int pidfd;
    pid_t pid;
} UProc;

typedef struct {
    const char *path;
    const char *const *argv;
    const char *const *envp;
} UProcExecutable;

typedef struct {
    // Entrypoint
    UProcExecutable executable;

    // User Namespace Configuration
    uid_t host_uid;
    uid_t child_uid;
    gid_t host_gid;
    gid_t child_gid;

    // Filesystem Configuration
    const char *root_dir;
    const char *work_dir;
} UProcConfig;

typedef struct {
    bool status;
    int exit_code;
} UProcWaitResult;

UProc *udaemon_create_proc(UDaemon *udaemon, UProcConfig config);
UProcWaitResult udaemon_wait_proc(const UProc *proc);
int udaemon_kill_proc(const UProc *proc);
void udaemon_free(UDaemon *udaemon);

#ifdef __cplusplus
}
#endif

#endif // HEADER_UPROC_
