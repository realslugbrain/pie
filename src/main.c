#include "pie/core/driver/driver.h"
#include "pie/core/registry/registry.h"
#include "pie/core/version.h"

#include <stdio.h>
#include <string.h>

static const char *command_name(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

static void print_usage(FILE *out, const char *cmd) {
  fprintf(out, "Pie compiler %s\n", PIE_VERSION);
  fprintf(out, "usage:\n");
  fprintf(out, "  %s --version\n", cmd);
  fprintf(out, "  %s features\n", cmd);
  fprintf(out, "  %s emit-ir <input.pie> -o <output.ir>\n", cmd);
  fprintf(out, "  %s emit-asm <input.pie> -o <output.asm>\n", cmd);
  fprintf(out, "  %s check [input.pie]\n", cmd);
  fprintf(out, "  %s build [input.pie] [-o output] [--keep-asm]\n", cmd);
  fprintf(out, "  %s run [input.pie]\n", cmd);
  fprintf(out, "  %s test [input.pie]\n", cmd);
  fprintf(out, "  %s new app <name>\n", cmd);
  fprintf(out, "  %s new lib <name>\n", cmd);
  fprintf(out, "  %s add <package>[@version]\n", cmd);
  fprintf(out, "  %s remove <package>\n", cmd);
}

static const char *arg_value_after_o(int argc, char **argv, int start) {
  for (int i = start; i + 1 < argc; i++) {
    if (strcmp(argv[i], "-o") == 0) {
      return argv[i + 1];
    }
  }
  return NULL;
}

static int parse_build_args(int argc, char **argv, const char **input,
                            const char **output, int *keep_asm) {
  *input = NULL;
  *output = NULL;
  *keep_asm = 0;

  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "--keep-asm") == 0) {
      *keep_asm = 1;
      continue;
    }

    if (strcmp(argv[i], "-o") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "pie: error: build requires an output path after -o\n");
        return 0;
      }
      if (*output) {
        fprintf(stderr, "pie: error: build received -o more than once\n");
        return 0;
      }
      *output = argv[++i];
      continue;
    }

    if (argv[i][0] == '-') {
      fprintf(stderr, "pie: error: unknown build option '%s'\n", argv[i]);
      return 0;
    }

    if (*input) {
      fprintf(stderr, "pie: error: build accepts at most one input file\n");
      return 0;
    }
    *input = argv[i];
  }

  return 1;
}

int main(int argc, char **argv) {
  const char *cmd = command_name(argv[0]);
  if (argc < 2) {
    print_usage(stderr, cmd);
    return 1;
  }

  if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "version") == 0) {
    printf("%s %s\n", cmd, PIE_VERSION);
    return 0;
  }

  if (strcmp(argv[1], "features") == 0) {
    pie_feature_registry_print(stdout, pie_feature_registry());
    return 0;
  }

  if (strcmp(argv[1], "new") == 0) {
    if (argc == 4 &&
        (strcmp(argv[2], "app") == 0 || strcmp(argv[2], "lib") == 0)) {
      return pie_driver_new_package(argv[2], argv[3]);
    }
    print_usage(stderr, cmd);
    return 1;
  }

  if (strcmp(argv[1], "add") == 0) {
    if (argc == 3) {
      return pie_driver_add_dependency(argv[2]);
    }
    print_usage(stderr, cmd);
    return 1;
  }

  if (strcmp(argv[1], "remove") == 0) {
    if (argc == 3) {
      return pie_driver_remove_dependency(argv[2]);
    }
    print_usage(stderr, cmd);
    return 1;
  }

  if (strcmp(argv[1], "emit-asm") == 0) {
    if (argc < 5) {
      print_usage(stderr, cmd);
      return 1;
    }
    const char *out = arg_value_after_o(argc, argv, 3);
    if (!out) {
      fprintf(stderr, "pie: error: emit-asm requires -o <output.asm>\n");
      return 1;
    }
    return pie_driver_emit_asm(argv[2], out);
  }

  if (strcmp(argv[1], "emit-ir") == 0) {
    if (argc < 5) {
      print_usage(stderr, cmd);
      return 1;
    }
    const char *out = arg_value_after_o(argc, argv, 3);
    if (!out) {
      fprintf(stderr, "pie: error: emit-ir requires -o <output.ir>\n");
      return 1;
    }
    return pie_driver_emit_ir(argv[2], out);
  }

  if (strcmp(argv[1], "check") == 0) {
    if (argc == 2) {
      return pie_driver_check_package();
    }
    if (argc == 3) {
      return pie_driver_check(argv[2]);
    }
    print_usage(stderr, cmd);
    return 1;
  }

  if (strcmp(argv[1], "build") == 0) {
    const char *input = NULL;
    const char *out = NULL;
    int keep_asm = 0;
    if (!parse_build_args(argc, argv, &input, &out, &keep_asm)) {
      return 1;
    }

    if (input) {
      if (!out) {
        fprintf(stderr, "pie: error: build with an explicit input file "
                        "requires -o <output>\n");
        return 1;
      }
      return pie_driver_build(input, out, keep_asm);
    }

    return pie_driver_build_package(out, keep_asm);
  }

  if (strcmp(argv[1], "run") == 0) {
    if (argc == 2) {
      return pie_driver_run_package();
    }
    if (argc == 3) {
      return pie_driver_run(argv[2]);
    }
    print_usage(stderr, cmd);
    return 1;
  }

  if (strcmp(argv[1], "test") == 0) {
    if (argc == 2) {
      return pie_driver_test(NULL);
    }
    if (argc == 3) {
      return pie_driver_test(argv[2]);
    }
    print_usage(stderr, cmd);
    return 1;
  }

  print_usage(stderr, cmd);
  return 1;
}
