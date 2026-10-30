#include <assert.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  const char *name;
  const char **sources;
  size_t source_count;
  const char **compile_flags;
  size_t compile_flag_count;
} Anob_Target;

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
  if (luaL_loadfile(L, script) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
    fprintf(stderr, "error: %s\n", lua_tostring(L, -1));
    lua_close(L);
    return 1;
  }

  // temporary
  const char *sources[] = {"hello.c"};
  const char *compile_flags[] = {"-Wall", "-Wextra"};
  Anob_Target target = {
      .name = "hello",
      .sources = sources,
      .source_count = sizeof(sources) / sizeof(sources[0]),
      .compile_flags = compile_flags,
      .compile_flag_count = sizeof(compile_flags) / sizeof(compile_flags[0]),
  };

  FILE *file = fopen(output_file, "w");
  if (file == NULL) {
    perror("fopen");
    lua_close(L);
    return 1;
  }
  generate_nob(file, &target);

  int failed = ferror(file);
  if (fclose(file) != 0)
    failed = 1;
  lua_close(L);
  if (failed) {
    fprintf(stderr, "error: could not write %s\n", output_file);
    return 1;
  }
  return 0;
}
