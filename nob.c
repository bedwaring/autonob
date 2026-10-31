#define NOB_IMPLEMENTATION
#include "nob.h"

#include <stdlib.h>

#ifdef _WIN32
#define EXE_SUFFIX ".exe"
#else
#define EXE_SUFFIX ""
#endif

int main(int argc, char **argv) {
  NOB_GO_REBUILD_URSELF(argc, argv);

  const char *cc = getenv("CC");
  if (cc == NULL || *cc == '\0')
    cc = "cc";

  if (!nob_mkdir_if_not_exists("build"))
    return 1;

  Nob_File_Paths sources = {0};
  nob_da_append(&sources, "src/main.c");

  Nob_File_Paths lua_files = {0};
  if (!nob_read_entire_dir("thirdparty/lua", &lua_files))
    return 1;
  nob_da_foreach(const char *, file, &lua_files) {
    if (nob_sv_ends_with_cstr(nob_sv_from_cstr(*file), ".c"))
      nob_da_append(&sources, nob_temp_sprintf("thirdparty/lua/%s", *file));
  }

  Nob_Cmd cmd = {0};
  Nob_File_Paths objects = {0};
  nob_da_foreach(const char *, src, &sources) {
    const char *obj = nob_temp_sprintf("build/%s.o", nob_path_name(*src));
    nob_da_append(&objects, obj);

    int rebuild = nob_needs_rebuild1(obj, *src);
    if (rebuild < 0)
      return 1;
    if (rebuild == 0)
      continue;

    nob_cmd_append(&cmd, cc, "-std=c17", "-O2", "-Ithirdparty/lua", "-c", *src,
                   "-o", obj);
    if (!nob_cmd_run(&cmd))
      return 1;
  }

  const char *exe = "build/autonob" EXE_SUFFIX;
  int rebuild = nob_needs_rebuild(exe, objects.items, objects.count);
  if (rebuild < 0)
    return 1;
  if (rebuild == 0) {
    nob_log(NOB_INFO, "%s is up to date", exe);
    return 0;
  }

  nob_cmd_append(&cmd, cc, "-o", exe);
  nob_da_append_many(&cmd, objects.items, objects.count);
#ifndef _WIN32
  nob_cmd_append(&cmd, "-lm");
#endif
  return nob_cmd_run(&cmd) ? 0 : 1;
}
