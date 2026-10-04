/**
 * @file cli.c
 * @brief Command registry implementation for Leptris CLI
 */

#include "cli.h"
#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Command Registry Implementation                                           */
/* ------------------------------------------------------------------------- */

cli_registry_t* cli_registry_new(void) {
    cli_registry_t* registry = malloc(sizeof(cli_registry_t));
    if (!registry) return NULL;

    registry->commands = malloc(sizeof(cli_command_t*) * 8);
    if (!registry->commands) {
        free(registry);
        return NULL;
    }

    registry->count = 0;
    registry->capacity = 8;
    return registry;
}

void cli_registry_free(cli_registry_t* registry) {
    if (!registry) return;

    /* Note: We don't free individual commands - they are typically
     * static or separately managed */
    free(registry->commands);
    free(registry);
}

cli_result_t cli_registry_register(
    cli_registry_t* registry,
    cli_command_t* command
) {
    if (!registry || !command) {
        return CLI_ERROR_ARGS;
    }

    /* Check for duplicate names */
    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->commands[i]->name, command->name) == 0) {
            cli_error("command already registered: %s", command->name);
            return CLI_ERROR_ARGS;
        }
    }

    /* Grow if needed */
    if (registry->count >= registry->capacity) {
        size_t new_capacity = registry->capacity * 2;
        cli_command_t** new_commands = realloc(
            registry->commands,
            sizeof(cli_command_t*) * new_capacity
        );
        if (!new_commands) {
            return CLI_ERROR_MEMORY;
        }

        registry->commands = new_commands;
        registry->capacity = new_capacity;
    }

    registry->commands[registry->count++] = command;
    return CLI_SUCCESS;
}

cli_command_t* cli_registry_find(
    const cli_registry_t* registry,
    const char* name
) {
    if (!registry || !name) return NULL;

    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->commands[i]->name, name) == 0) {
            return registry->commands[i];
        }
    }

    return NULL;
}

void cli_registry_print_all(const cli_registry_t* registry) {
    if (!registry) return;

    for (size_t i = 0; i < registry->count; i++) {
        printf("  %-12s  %s\n",
            registry->commands[i]->name,
            registry->commands[i]->description);
    }
}

/* ------------------------------------------------------------------------- */
/* Shared Command Utilities                                                  */
/* ------------------------------------------------------------------------- */

char* cli_read_stream(FILE* fp, size_t* out_len) {
    size_t cap = 4096, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        size_t n = fread(buf + len, 1, cap - len - 1, fp);
        len += n;
        if (len + 1 >= cap) {
            cap *= 2;
            char* grown = (char*)realloc(buf, cap);
            if (!grown) { free(buf); return NULL; }
            buf = grown;
            continue;
        }
        break;
    }
    buf[len] = 0;
    if (out_len) *out_len = len;
    return buf;
}

char* cli_read_file(const char* path, size_t* out_len) {
    if (strcmp(path, "-") == 0) return cli_read_stream(stdin, out_len);
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;
    char* s = cli_read_stream(fp, out_len);
    fclose(fp);
    return s;
}

/* ------------------------------------------------------------------------- */
/* Helper Functions                                                          */
/* ------------------------------------------------------------------------- */

const char* cli_result_to_string(cli_result_t result) {
    switch (result) {
        case CLI_SUCCESS:       return "success";
        case CLI_ERROR_PARSE:   return "parse error";
        case CLI_ERROR_XPATH:   return "xpath error";
        case CLI_ERROR_IO:      return "I/O error";
        case CLI_ERROR_ARGS:    return "invalid arguments";
        case CLI_ERROR_MEMORY:  return "memory allocation failed";
        case CLI_ERROR_INTERNAL: return "internal error";
        case CLI_ERROR_XSLT:    return "xslt error";
        default:                return "unknown error";
    }
}