#define NOB_IMPLEMENTATION
#include "nob.h"

#include <stdlib.h>

int main(int argc, char **argv) {
  NOB_GO_REBUILD_URSELF(argc, argv);

  const char *cc = getenv("CC");
  if (cc == NULL || *cc == '\0')
    cc = "cc";

  Nob_Cmd cmd = {0};
  nob_cmd_append(&cmd, cc, "-std=c17", "-Ithirdparty/lua", "main.c");

  Nob_File_Paths lua_files = {0};
  if (!nob_read_entire_dir("thirdparty/lua", &lua_files))
    return 1;
  nob_da_foreach(const char *, file, &lua_files) {
    if (nob_sv_ends_with_cstr(nob_sv_from_cstr(*file), ".c"))
      nob_cmd_append(&cmd, nob_temp_sprintf("thirdparty/lua/%s", *file));
  }

#ifndef _WIN32
  nob_cmd_append(&cmd, "-lm");
#endif
  nob_cmd_append(&cmd, "-o", "autonob");

  return nob_cmd_run(&cmd) ? 0 : 1;
}