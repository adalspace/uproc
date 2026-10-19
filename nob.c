#define NOB_IMPLEMENTATION
#define NOB_EXPERIMENTAL_DELETE_OLD
#include "./include/nob.h"

#define BUILD_FOLDER "./build"
#define SOURCE_FOLDER "./src"
#define LIB_FOLDER "./lib"

void cmd_custom_cflags(Cmd *cmd) {
    nob_cc_flags(cmd);
    cmd_append(cmd, "-I./include/");
}

void cmd_local_lib(Cmd *cmd) {
    cmd_append(cmd, "-L./lib/");
}

void cmd_link_uipc(Cmd *cmd) {
    cmd_append(cmd, "-l:libuipc.a");
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    shift_args(&argc, &argv); // program
    if (argc > 0) {
        const char *command = shift_args(&argc, &argv);
        nob_log(INFO, "desired command: %s", command);
        if (strcmp(command, "clean") == 0) {
            Cmd cmd = {0};
            cmd_append(&cmd, "rm", "-r", BUILD_FOLDER, LIB_FOLDER);
            return cmd_run(&cmd);
        }
    }

    if (!mkdir_if_not_exists(BUILD_FOLDER)) return 1;
    if (!mkdir_if_not_exists(LIB_FOLDER)) return 1;

    // build/uproc.o
    Cmd cmd = {0};
    nob_cc(&cmd);
    cmd_append(&cmd, "-c", "-fPIC");
    nob_cc_output(&cmd, BUILD_FOLDER"/uproc.o");
    cmd_custom_cflags(&cmd);
    nob_cc_inputs(&cmd, SOURCE_FOLDER"/uproc.c");
    cmd_local_lib(&cmd);
    cmd_link_uipc(&cmd);
    if (!cmd_run(&cmd)) return 1;

    // lib/libuproc.a
    cmd_append(&cmd, "ar", "rcs", LIB_FOLDER"/libuproc.a", BUILD_FOLDER"/uproc.o");
    if (!cmd_run(&cmd)) return 1;

    // lib/libuproc.so
    nob_cc(&cmd);
    cmd_append(&cmd, "-shared");
    nob_cc_output(&cmd, LIB_FOLDER"/libuproc.so");
    nob_cc_inputs(&cmd, BUILD_FOLDER"/uproc.o");
    if (!cmd_run(&cmd)) return 1;

    // build/server
    nob_cc(&cmd);
    nob_cc_output(&cmd, BUILD_FOLDER"/server");
    cmd_custom_cflags(&cmd);
    nob_cc_inputs(&cmd, SOURCE_FOLDER"/main.c");
    cmd_link_uipc(&cmd);
    cmd_local_lib(&cmd);
    cmd_append(&cmd, "-luproc");
    if (!cmd_run(&cmd)) return 1;

    return 0;
}
