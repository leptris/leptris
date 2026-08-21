/**
 * @file version.c
 * @brief Version command implementation for Leptris CLI
 */

#include "leptris.h"
#include "../cli.h"
#include "../error.h"
#include <stdio.h>

/* ------------------------------------------------------------------------- */
/* Version Command Implementation                                            */
/* ------------------------------------------------------------------------- */

static cli_result_t version_execute(int argc, char** argv) {
    (void)argc;
    (void)argv;

    /* Print version information */
    printf("leptris version %s\n", leptris_version());
    printf("C library (libleptris) with public API\n");
    printf("Built with CMake\n");

    return CLI_SUCCESS;
}

static void version_print_help(void) {
    printf("Usage: leptris version [OPTIONS]\n");
    printf("\n");
    printf("Show version information.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help          Show this help\n");
}

/* ------------------------------------------------------------------------- */
/* Command Registration                                                      */
/* ------------------------------------------------------------------------- */

static cli_command_t version_command = {
    .name = "version",
    .description = "Show version information",
    .execute = version_execute,
    .print_help = version_print_help
};

cli_command_t* cli_command_version(void) {
    return &version_command;
}