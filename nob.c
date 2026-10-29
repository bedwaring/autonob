#define NOB_IMPLEMENTATION
#include "nob.h"

#include <stdlib.h>

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    const char *cc = getenv("CC");
    if (cc == NULL || *cc == '\0')
        cc = "cc";

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, cc, "-std=c17", "main.c", "-o", "autonob");

    return nob_cmd_run(&cmd) ? 0 : 1;
}