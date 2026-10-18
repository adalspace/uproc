#ifndef HEADER_UPROC_
#define HEADER_UPROC_

#include <stdbool.h>
#include <sched.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int fd;
    pid_t tid;
    char *server_socket;
} Proc;

typedef struct {
    bool status;
    int exit_code;
} WaitResult;

typedef int(*process_fn)();

Proc *spawn_process(process_fn fn, const char *server_socket);
WaitResult proc_wait(const Proc *proc);

void proc_free(Proc *proc);

#ifdef __cplusplus
}
#endif

#endif // HEADER_UPROC_
