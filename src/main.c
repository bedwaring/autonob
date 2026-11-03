#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  ANOB_ALL,
  ANOB_POSIX,
  ANOB_WINDOWS,
} Anob_Platform;

typedef struct {
  const char *value;
  Anob_Platform platform;
} Anob_Item;

typedef struct {
  Anob_Item *items;
  size_t count;
} Anob_List;

typedef struct {
  const char *name;
  Anob_List sources;
  Anob_List compile_flags;
  Anob_List libs;
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

static void write_appends(FILE *out, const Anob_List *list) {
  for (size_t i = 0; i < list->count; i++) {
    const Anob_Item *item = &list->items[i];
    if (item->platform == ANOB_POSIX)
      fputs("#ifndef _WIN32\n", out);
    else if (item->platform == ANOB_WINDOWS)
      fputs("#ifdef _WIN32\n", out);
    fputs("    nob_cmd_append(&cmd, ", out);
    write_c_string(out, item->value);
    fputs(");\n", out);
    if (item->platform != ANOB_ALL)
      fputs("#endif\n", out);
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
  write_appends(out, &target->compile_flags);
  write_appends(out, &target->sources);
  write_appends(out, &target->libs);
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

static Anob_Item read_item(lua_State *L, const char *field, size_t index) {
  Anob_Item item = {0};
  int top = lua_gettop(L);
  if (lua_type(L, -1) == LUA_TTABLE) {
    lua_getfield(L, -1, "platform");
    lua_getfield(L, -2, "value");
    const char *platform = lua_tostring(L, -2);
    if (platform != NULL && strcmp(platform, "posix") == 0)
      item.platform = ANOB_POSIX;
    else if (platform != NULL && strcmp(platform, "windows") == 0)
      item.platform = ANOB_WINDOWS;
    else
      luaL_error(L, "executable: %s[%d] must be a string", field, (int)index);
  }
  if (lua_type(L, -1) != LUA_TSTRING)
    luaL_error(L, "executable: %s[%d] must be a string", field, (int)index);
  item.value = copy_string(lua_tostring(L, -1));
  lua_settop(L, top);
  return item;
}

static Anob_List read_list(lua_State *L, const char *field, bool required) {
  Anob_List list = {0};
  int type = lua_getfield(L, 1, field);
  if (type == LUA_TNIL && !required) {
    lua_pop(L, 1);
    return list;
  }
  if (type != LUA_TTABLE)
    luaL_error(L, "executable: '%s' must be a table", field);

  list.count = lua_rawlen(L, -1);
  if (list.count == 0 && required)
    luaL_error(L, "executable: '%s' must not be empty", field);
  if (list.count > 0)
    list.items = xmalloc(list.count * sizeof(list.items[0]));

  for (size_t i = 0; i < list.count; i++) {
    lua_rawgeti(L, -1, i + 1);
    list.items[i] = read_item(L, field, i + 1);
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return list;
}

static int l_executable(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  if (target.name != NULL)
    return luaL_error(L, "only one executable is supported for now");

  if (lua_getfield(L, 1, "name") != LUA_TSTRING)
    return luaL_error(L, "executable: 'name' must be a string");
  target.name = copy_string(lua_tostring(L, -1));
  lua_pop(L, 1);

  target.sources = read_list(L, "sources", true);
  target.compile_flags = read_list(L, "flags", false);
  target.libs = read_list(L, "libs", false);
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

static int make_platform_value(lua_State *L, const char *platform) {
  luaL_checktype(L, 1, LUA_TSTRING);
  lua_createtable(L, 0, 2);
  lua_pushvalue(L, 1);
  lua_setfield(L, -2, "value");
  lua_pushstring(L, platform);
  lua_setfield(L, -2, "platform");
  return 1;
}

static int l_posix(lua_State *L) { return make_platform_value(L, "posix"); }

static int l_windows(lua_State *L) { return make_platform_value(L, "windows"); }

static int luaopen_autonob(lua_State *L) {
  static const luaL_Reg funcs[] = {
      {"executable", l_executable}, {"dir", l_dir}, {"posix", l_posix},
      {"windows", l_windows},       {NULL, NULL},
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
