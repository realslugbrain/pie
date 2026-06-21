#ifndef PIE_CORE_DRIVER_DRIVER_H
#define PIE_CORE_DRIVER_DRIVER_H

int pie_driver_emit_asm(const char *input_path, const char *output_path);
int pie_driver_emit_ir(const char *input_path, const char *output_path);
int pie_driver_check(const char *input_path);
int pie_driver_check_package(void);
int pie_driver_build(const char *input_path, const char *output_path,
                     int keep_asm);
int pie_driver_build_package(const char *output_path, int keep_asm);
int pie_driver_run(const char *input_path);
int pie_driver_run_package(void);
int pie_driver_test(const char *input_path);
int pie_driver_test_package(void);
int pie_driver_new_package(const char *kind, const char *path);
int pie_driver_add_dependency(const char *spec);
int pie_driver_remove_dependency(const char *name);

#endif
