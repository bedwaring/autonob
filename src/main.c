#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *name;
  const char **sources;
  size_t source_count;
  const char **compile_flags;
  size_t compile_flag_count;
} Anob_Target;

static void *xmalloc(size_t size) {
  void *p = malloc(size);
  assert(p != NULL && "Buy more RAM lol");
  return p;
}

static void write_c_string(FILE *out, const char *s) {
  fputc('"', out);
  for (; *s != '\0'; s++) {
    if (*s == '"' || *s == '\\')
      fputc('\\', out);
    fputc(*s, out);
  }
  fputc('"', out);
}

static void write_appends(FILE *out, const char **items, size_t count) {
  for (size_t i = 0; i < count; i++) {
    fputs("    nob_cmd_append(&cmd, ", out);
    write_c_string(out, items[i]);
    fputs(");\n", out);
  }
}

static void generate_nob(FILE *out, const Anob_Target *target) {
  fputs("#define NOB_IMPLEMENTATION\n", out);
  fputs("#include \"nob.h\"\n", out);
  fputs("\n", out);
  fputs("#include <stdlib.h>\n", out);
  fputs("\n", out);
  fputs("#ifdef _WIN32\n", out);
  fputs("#define EXE_SUFFIX \".exe\"\n", out);
  fputs("#else\n", out);
  fputs("#define EXE_SUFFIX \"\"\n", out);
  fputs("#endif\n", out);
  fputs("\n", out);
  fputs("int main(int argc, char **argv) {\n", out);
  fputs("    NOB_GO_REBUILD_URSELF(argc, argv);\n", out);
  fputs("\n", out);
  fputs("    const char *cc = getenv(\"CC\");\n", out);
  fputs("    if (cc == NULL || *cc == '\\0')\n", out);
  fputs("        cc = \"cc\";\n", out);
  fputs("\n", out);
  fputs("    Nob_Cmd cmd = {0};\n", out);
  fputs("    nob_cmd_append(&cmd, cc);\n", out);
  write_appends(out, target->compile_flags, target->compile_flag_count);
  write_appends(out, target->sources, target->source_count);
  fputs("    nob_cmd_append(&cmd, \"-o\", ", out);
  write_c_string(out, target->name);
  fputs(" EXE_SUFFIX);\n", out);
  fputs("\n", out);
  fputs("    return nob_cmd_run(&cmd) ? 0 : 1;\n", out);
  fputs("}\n", out);
}

static Anob_Target target = {0};

static char *copy_string(const char *s) {
  size_t size = strlen(s) + 1;
  char *copy = xmalloc(size);
  memcpy(copy, s, size);
  return copy;
}

static int l_executable(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  if (target.name != NULL)
    return luaL_error(L, "only one executable is supported for now");

  if (lua_getfield(L, 1, "name") != LUA_TSTRING)
    return luaL_error(L, "executable: 'name' must be a string");
  target.name = copy_string(lua_tostring(L, -1));
  lua_pop(L, 1);

  if (lua_getfield(L, 1, "sources") != LUA_TTABLE)
    return luaL_error(L, "executable: 'sources' must be a table");
  size_t source_count = lua_rawlen(L, -1);
  if (source_count == 0)
    return luaL_error(L, "executable: 'sources' must not be empty");

  const char **sources = xmalloc(source_count * sizeof(sources[0]));
  for (size_t i = 0; i < source_count; i++) {
    if (lua_rawgeti(L, -1, i + 1) != LUA_TSTRING)
      return luaL_error(L, "executable: sources[%d] must be a string",
                        (int)(i + 1));
    sources[i] = copy_string(lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  target.sources = sources;
  target.source_count = source_count;

  int flags_type = lua_getfield(L, 1, "flags");
  if (flags_type == LUA_TTABLE) {
    size_t flags_count = lua_rawlen(L, -1);
    const char **flags;
    if (flags_count == 0)
      flags = NULL;
    else
      flags = xmalloc(flags_count * sizeof(flags[0]));
    for (size_t i = 0; i < flags_count; i++) {
      if (lua_rawgeti(L, -1, i + 1) != LUA_TSTRING)
        return luaL_error(L, "executable: flags[%d] must be a string",
                          (int)(i + 1));

      flags[i] = copy_string(lua_tostring(L, -1));
      lua_pop(L, 1);
    }
    target.compile_flag_count = flags_count;
    target.compile_flags = flags;
  } else if (flags_type != LUA_TNIL) {
    return luaL_error(L, "executable: 'flags' must be a table");
  }
  lua_pop(L, 1);
  return 0;
}

static int l_dir(lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  DIR *dir = opendir(path);
  if (dir == NULL)
    return luaL_error(L, "dir: cannot open '%s': %s", path, strerror(errno));

  lua_newtable(L);
  lua_Integer count = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    lua_pushstring(L, entry->d_name);
    lua_rawseti(L, -2, ++count);
  }
  closedir(dir);

  // table.sort(names)
  lua_getglobal(L, "table");
  lua_getfield(L, -1, "sort");
  lua_pushvalue(L, -3);
  lua_call(L, 1, 0);
  lua_pop(L, 1);
  return 1;
}

static int luaopen_autonob(lua_State *L) {
  static const luaL_Reg funcs[] = {
      {"executable", l_executable},
      {"dir", l_dir},
      {NULL, NULL},
  };
  luaL_newlib(L, funcs);
  return 1;
}

static void print_usage(FILE *out, const char *program) {
  fprintf(out, "Usage: %s [-f <script>] <output-file>\n", program);
}

int main(int argc, char **argv) {
  const char *script = "autonob.lua";
  const char *output_file = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "error: %s requires a filename\n", argv[i]);
        return 1;
      }
      script = argv[++i];
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      print_usage(stdout, argv[0]);
      return 0;
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "error: unknown option %s\n", argv[i]);
      return 1;
    } else if (output_file != NULL) {
      fprintf(stderr, "error: unexpected argument %s\n", argv[i]);
      return 1;
    } else {
      output_file = argv[i];
    }
  }

  if (output_file == NULL) {
    print_usage(stderr, argv[0]);
    return 1;
  }

  lua_State *L = luaL_newstate();
  assert(L != NULL && "Buy more RAM lol");
  luaL_openlibs(L);
  luaL_requiref(L, "autonob", luaopen_autonob, 0);
  lua_pop(L, 1);

  if (luaL_loadfile(L, script) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
    fprintf(stderr, "error: %s\n", lua_tostring(L, -1));
    lua_close(L);
    return 1;
  }
  lua_close(L);

  if (target.name == NULL) {
    fprintf(stderr, "error: %s did not call autonob.executable\n", script);
    return 1;
  }

  FILE *file = fopen(output_file, "w");
  if (file == NULL) {
    perror("fopen");
    return 1;
  }
  generate_nob(file, &target);

  int failed = ferror(file);
  if (fclose(file) != 0)
    failed = 1;
  if (failed) {
    fprintf(stderr, "error: could not write %s\n", output_file);
    return 1;
  }
  return 0;
}
