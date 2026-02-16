/**
 * @file main.c
 * @brief Main entry point for Taurus CLI tool
 */

#include "cli.h"
#include "options.h"
#include "error.h"
#include "output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Helper Functions                                                          */
/* ------------------------------------------------------------------------- */

static void print_usage(void) {
    printf("Usage: taurus [OPTIONS] COMMAND [ARGS...]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  parse      Parse and validate XML\n");
    printf("  xpath      Execute XPath queries\n");
    printf("  format     Pretty-print XML\n");
    printf("  c14n       Canonicalize XML (for digital signatures)\n");
    printf("  version    Show version information\n");
    printf("\n");
    printf("Global Options:\n");
    printf("  -v, --verbose       Verbose output\n");
    printf("  -q, --quiet         Suppress warnings\n");
    printf("  --format FORMAT     Output format (xml|json|text)\n");
    printf("  --color             Enable color output\n");
    printf("  --no-color          Disable color output\n");
    printf("  -h, --help          Show this help\n");
    printf("  --version           Show version\n");
    printf("\n");
    printf("Use 'taurus COMMAND --help' for command-specific help.\n");
}

static void print_version(void) {
    printf("taurus version 0.1.0\n");
    printf("C library (libtaurus) with public API\n");
}

/* ------------------------------------------------------------------------- */
/* Main Entry Point                                                          */
/* ------------------------------------------------------------------------- */

int main(int argc, char** argv) {
    cli_result_t result = CLI_SUCCESS;
    cli_global_options_t* global_opts = NULL;
    cli_registry_t* registry = NULL;

    /* Parse global options */
    global_opts = cli_global_options_new();
    if (!global_opts) {
        cli_fatal("cannot allocate memory");
        return CLI_ERROR_MEMORY;
    }

    if (cli_global_options_parse(global_opts, &argc, &argv) != CLI_SUCCESS) {
        cli_global_options_free(global_opts);
        return CLI_ERROR_ARGS;
    }

    /* Setup error configuration */
    cli_error_set_verbose(global_opts->verbose > 0);
    cli_error_set_quiet(global_opts->quiet > 0);
    cli_error_set_color(global_opts->color);

    /* Handle global --help */
    if (global_opts->help) {
        print_usage();
        cli_global_options_free(global_opts);
        return CLI_SUCCESS;
    }

    /* Handle global --version */
    if (global_opts->version) {
        print_version();
        cli_global_options_free(global_opts);
        return CLI_SUCCESS;
    }

    /* Create command registry */
    registry = cli_registry_new();
    if (!registry) {
        cli_fatal("cannot allocate memory");
        cli_global_options_free(global_opts);
        return CLI_ERROR_MEMORY;
    }

    /* Register commands */
    cli_registry_register(registry, cli_command_version());
    cli_registry_register(registry, cli_command_parse());
    cli_registry_register(registry, cli_command_xpath());
    cli_registry_register(registry, cli_command_format());
    cli_registry_register(registry, cli_command_c14n());

    /* Check if command specified */
    if (argc < 2) {
        print_usage();
        result = CLI_ERROR_ARGS;
        goto cleanup;
    }

    /* Find command */
    cli_command_t* cmd = cli_registry_find(registry, argv[1]);
    if (!cmd) {
        cli_error("unknown command: %s", argv[1]);
        print_usage();
        result = CLI_ERROR_ARGS;
        goto cleanup;
    }

    /* Execute command */
    result = cmd->execute(argc - 1, &argv[1]);

cleanup:
    cli_registry_free(registry);
    cli_global_options_free(global_opts);

    return result;
}