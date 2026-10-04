/**
 * @file cli.h
 * @brief Command interface for Leptris CLI tool
 *
 * This file defines the core command pattern infrastructure for the Leptris
 * CLI. It provides a registry-based, extensible architecture where each
 * command implements a common interface.
 *
 * Design Principles:
 * - MECE: Each command has distinct responsibility
 * - Open/Closed: Add new commands without modifying core
 * - Single Responsibility: Commands do one thing well
 * - Separation of Concerns: CLI → API → Library (clear layers)
 */

#ifndef LEPTRIS_CLI_H
#define LEPTRIS_CLI_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Result Codes (compatible with xmllint)                                   */
/* ------------------------------------------------------------------------- */

/**
 * CLI result codes
 *
 * These codes are compatible with xmllint for drop-in replacement:
 * - 0: Success
 * - 1: Parse error
 * - 2: XPath error
 * - 3: I/O error
 * - 4: Bad arguments
 */
typedef enum {
    CLI_SUCCESS = 0,        /**< Command succeeded */
    CLI_ERROR_PARSE = 1,    /**< XML parsing failed */
    CLI_ERROR_XPATH = 2,    /**< XPath evaluation failed */
    CLI_ERROR_IO = 3,       /**< I/O error (file not found, etc.) */
    CLI_ERROR_ARGS = 4,     /**< Invalid arguments */
    CLI_ERROR_MEMORY = 5,   /**< Memory allocation failed */
    CLI_ERROR_INTERNAL = 6, /**< Internal error (bug) */
    CLI_ERROR_XSLT = 7      /**< XSLT transformation failed */
} cli_result_t;

/* ------------------------------------------------------------------------- */
/* Command Interface (Virtual Table Pattern)                                */
/* ------------------------------------------------------------------------- */

/**
 * Command interface
 *
 * Each command implements this interface. Commands are registered in a
 * global registry and dispatched based on the first CLI argument.
 *
 * Example command structure:
 *
 * ```c
 * cli_result_t parse_execute(int argc, char** argv) {
 *     // Parse options
 *     // Call API
 *     // Format output
 *     return CLI_SUCCESS;
 * }
 *
 * void parse_print_help(void) {
 *     printf("Usage: leptris parse [OPTIONS] FILE\n");
 *     // ... more help text
 * }
 *
 * cli_command_t parse_command = {
 *     .name = "parse",
 *     .description = "Parse and validate XML",
 *     .execute = parse_execute,
 *     .print_help = parse_print_help
 * };
 * ```
 */
typedef struct cli_command {
    const char* name;           /**< Command name (e.g., "parse") */
    const char* description;    /**< One-line description for --help */

    /**
     * Execute the command
     *
     * @param argc Number of arguments (including command name)
     * @param argv Arguments array (argv[0] is command name)
     * @return CLI result code
     */
    cli_result_t (*execute)(int argc, char** argv);

    /**
     * Print command-specific help
     *
     * Called when user runs: leptris help <command>
     * or: leptris <command> --help
     */
    void (*print_help)(void);
} cli_command_t;

/* ------------------------------------------------------------------------- */
/* Command Registry                                                          */
/* ------------------------------------------------------------------------- */

/**
 * Command registry
 *
 * Central registry holding all available commands. Commands are registered
 * at startup, then dispatched based on user input.
 *
 * Implementation uses a dynamic array that grows as needed.
 */
typedef struct cli_registry {
    cli_command_t** commands;   /**< Dynamic array of command pointers */
    size_t count;               /**< Number of registered commands */
    size_t capacity;            /**< Allocated capacity */
} cli_registry_t;

/**
 * Create a new command registry
 *
 * @return Pointer to new registry, or NULL on allocation failure
 */
cli_registry_t* cli_registry_new(void);

/**
 * Free a command registry
 *
 * Frees the registry structure but NOT the individual commands (they are
 * typically static or separately managed).
 *
 * @param registry Registry to free
 */
void cli_registry_free(cli_registry_t* registry);

/**
 * Register a command
 *
 * Adds a command to the registry. The command pointer must remain valid
 * for the lifetime of the registry.
 *
 * @param registry Registry to add to
 * @param command Command to register
 * @return CLI_SUCCESS or error code
 */
cli_result_t cli_registry_register(
    cli_registry_t* registry,
    cli_command_t* command
);

/**
 * Find a command by name
 *
 * @param registry Registry to search
 * @param name Command name to find
 * @return Command pointer or NULL if not found
 */
cli_command_t* cli_registry_find(
    const cli_registry_t* registry,
    const char* name
);

/**
 * Print all registered commands
 *
 * Prints one aligned "name — description" line per registered
 * command, in registration order, without a section header — the
 * caller frames the listing. This is the single source of truth
 * for the global --help command list: command descriptors carry
 * their own description, and no hand-maintained copy of the list
 * exists anywhere else.
 *
 * @param registry Registry to print
 */
void cli_registry_print_all(const cli_registry_t* registry);

/* ------------------------------------------------------------------------- */
/* Shared Command Utilities                                                  */
/* ------------------------------------------------------------------------- */

/**
 * Read a whole stream into a NUL-terminated heap buffer
 *
 * @param fp Stream to read (not closed)
 * @param out_len Length in bytes excluding the NUL, or NULL
 * @return Buffer (caller frees), or NULL on allocation failure
 */
char* cli_read_stream(FILE* fp, size_t* out_len);

/**
 * Read a whole file into a NUL-terminated heap buffer
 *
 * @param path File path, or "-" for stdin
 * @param out_len Length in bytes excluding the NUL, or NULL
 * @return Buffer (caller frees), NULL on open or allocation failure
 */
char* cli_read_file(const char* path, size_t* out_len);

/* ------------------------------------------------------------------------- */
/* Standard Commands (Forward Declarations)                                 */
/* ------------------------------------------------------------------------- */

/**
 * Get the 'parse' command
 *
 * Command: leptris parse [OPTIONS] FILE
 * Purpose: Parse and validate XML files
 */
extern cli_command_t* cli_command_parse(void);

/**
 * Get the 'xpath' command
 *
 * Command: leptris xpath [OPTIONS] FILE EXPR
 * Purpose: Execute XPath queries
 */
extern cli_command_t* cli_command_xpath(void);

/**
 * Get the 'format' command
 *
 * Command: leptris format [OPTIONS] FILE
 * Purpose: Pretty-print XML
 */
extern cli_command_t* cli_command_format(void);

/**
 * Get the 'version' command
 *
 * Command: leptris version
 * Purpose: Show version information
 */
extern cli_command_t* cli_command_version(void);

/**
 * Get the 'diff' command
 *
 * Command: leptris diff [OPTIONS] A B
 * Purpose: Structural XML diff between two documents (lane 17)
 */
extern cli_command_t* cli_command_diff(void);
extern cli_command_t* cli_command_validate(void);

/**
 * Get the 'xquery' command
 *
 * Command: leptris xquery [OPTIONS] (-q FILE | -e EXPR)
 * Purpose: Execute an XQuery 1.0 query
 */
extern cli_command_t* cli_command_xquery(void);

/**
 * Get the 'xslt' command
 *
 * Command: leptris xslt -s STYLESHEET [INPUT]
 * Purpose: Apply an XSLT stylesheet to an XML document
 */
extern cli_command_t* cli_command_xslt(void);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_CLI_H */