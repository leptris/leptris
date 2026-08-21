# Leptris CLI Architecture

**Version**: 0.5.0
**Last Updated**: 2024-12-01
**Status**: Design Phase Complete

---

## Table of Contents

1. [Overview](#overview)
2. [Design Principles](#design-principles)
3. [Component Structure](#component-structure)
4. [Data Flow](#data-flow)
5. [Command Pattern](#command-pattern)
6. [Option Handling (MECE)](#option-handling-mece)
7. [Output Formatting](#output-formatting)
8. [Error Handling](#error-handling)
9. [Extension Points](#extension-points)
10. [Design Decisions](#design-decisions)
11. [Implementation Guidelines](#implementation-guidelines)

---

## Overview

The Leptris CLI is a **thin layer** on top of the C library API (`libleptris`). It provides a command-line interface for XML parsing, XPath queries, and document formatting while maintaining strict separation of concerns.

### Purpose

- **Standalone XML tool**: xmllint drop-in replacement
- **Scriptable**: JSON output, proper exit codes
- **Extensible**: Easy to add new commands
- **Professional**: Man pages, shell completion, color output

### Architecture Layers

```
┌─────────────────────────────────────────────────┐
│              User Input (CLI args)               │
└─────────────────────────────────────────────────┘
                       ↓
┌─────────────────────────────────────────────────┐
│  CLI Layer (argument parsing, formatting)       │
│  - Command dispatch                              │
│  - Option resolution (CLI → ENV → Default)      │
│  - Output formatting (XML/JSON/text)            │
│  - Error reporting                               │
└─────────────────────────────────────────────────┘
                       ↓
┌─────────────────────────────────────────────────┐
│  API Layer (high-level operations)              │
│  - leptris_parse()                                │
│  - leptris_xpath_eval()                           │
│  - leptris_document_free()                        │
└─────────────────────────────────────────────────┘
                       ↓
┌─────────────────────────────────────────────────┐
│  C Library (core functionality)                  │
│  - XML parser                                    │
│  - XPath engine                                  │
│  - DOM manipulation                              │
└─────────────────────────────────────────────────┘
```

**Key Insight**: The CLI never directly manipulates XML structures. It always goes through the public API.

---

## Design Principles

### 1. MECE (Mutually Exclusive, Collectively Exhaustive)

Every aspect of the CLI follows MECE principles:

- **Commands**: Each command has distinct responsibility (parse, xpath, format, version)
- **Options**: Three exclusive sources (CLI → ENV → Default)
- **Output formats**: Three formats (XML, JSON, text), never mixed
- **Error levels**: Three levels (warning, error, fatal), no overlap

### 2. Separation of Concerns

Clear boundaries between layers:

```
Concern             | Component         | Responsibility
--------------------|-------------------|--------------------------------
User interface      | main.c            | Dispatch, global setup
Argument parsing    | options.c         | Parse argv, read ENV
Business logic      | commands/*.c      | Call API, handle results
Output formatting   | output.c          | Render results
Error reporting     | error.c           | Print errors consistently
```

### 3. Open/Closed Principle

- **Open for extension**: Add new commands without modifying core
- **Closed for modification**: Core infrastructure is stable

Example: Adding a new command requires:
1. Create `cli/commands/mycommand.c`
2. Register in `main.c`
3. No changes to core infrastructure

### 4. Single Responsibility

Each module does one thing:

- `cli.c`: Command registry
- `options.c`: Option parsing
- `output.c`: Output formatting
- `error.c`: Error handling
- `commands/parse.c`: Parse command only

### 5. No Code Guards

Instead of `#ifdef` for platform differences:

```c
// BAD: Code guards
#ifdef _WIN32
    // Windows implementation
#else
    // Unix implementation
#endif

// GOOD: Architectural solution
output_supports_color(FILE* out) {
    // Single implementation handles all platforms
    return isatty(fileno(out));
}
```

---

## Component Structure

### File Organization

```
cli/
├── main.c              - Entry point, dispatch
├── cli.h/.c            - Command registry
├── options.h/.c        - Option parsing (MECE)
├── output.h/.c         - Output formatting
├── error.h/.c          - Error handling
└── commands/
    ├── parse.c         - Parse command
    ├── xpath.c         - XPath command
    ├── format.c        - Format command
    └── version.c       - Version command
```

### Component Dependencies

```
main.c
  ├─→ cli.c (command registry)
  ├─→ options.c (global options)
  ├─→ error.c (error setup)
  └─→ commands/*.c (each command)
        ├─→ options.c (command options)
        ├─→ output.c (result formatting)
        ├─→ error.c (error reporting)
        └─→ libleptris (API calls)
```

### Module Sizes (Target)

| Module | Lines | Status |
|--------|-------|--------|
| cli.h | 167 | ✅ Complete |
| cli.c | ~200 | Pending |
| options.h | 315 | ✅ Complete |
| options.c | ~400 | Pending |
| output.h | 296 | ✅ Complete |
| output.c | ~500 | Pending |
| error.h | 276 | ✅ Complete |
| error.c | ~300 | Pending |
| main.c | ~150 | Pending |
| commands/parse.c | ~200 | Pending |
| commands/xpath.c | ~250 | Pending |
| commands/format.c | ~200 | Pending |
| commands/version.c | ~50 | Pending |
| **Total** | ~3,000+ | Design phase |

---

## Data Flow

### Typical Command Execution

```
1. User runs: leptris xpath --format json file.xml "//book"
                      ↓
2. main() parses global options (--format json)
                      ↓
3. main() finds "xpath" command in registry
                      ↓
4. xpath_execute() called with remaining args
                      ↓
5. xpath_execute() parses command options (file.xml, "//book")
                      ↓
6. xpath_execute() resolves options (CLI → ENV → Default)
                      ↓
7. xpath_execute() calls leptris_parse(file.xml)
                      ↓
8. xpath_execute() calls leptris_xpath_eval(doc, "//book")
                      ↓
9. xpath_execute() creates JSON formatter
                      ↓
10. formatter->print_nodeset(result, stdout)
                      ↓
11. xpath_execute() cleans up and returns CLI_SUCCESS
                      ↓
12. main() exits with code 0
```

### Error Flow

```
1. Error occurs in command
                      ↓
2. Command creates cli_error_t
                      ↓
3. Command calls cli_error_print(error, stderr)
                      ↓
4. Error printed in standard format
                      ↓
5. Command returns error code (CLI_ERROR_PARSE, etc.)
                      ↓
6. main() exits with that code
```

---

## Command Pattern

### Command Interface

Every command implements the `cli_command_t` interface:

```c
typedef struct cli_command {
    const char* name;
    const char* description;
    cli_result_t (*execute)(int argc, char** argv);
    void (*print_help)(void);
} cli_command_t;
```

### Command Registry

Commands are registered at startup:

```c
int main(int argc, char** argv) {
    cli_registry_t* registry = cli_registry_new();

    cli_registry_register(registry, cli_command_parse());
    cli_registry_register(registry, cli_command_xpath());
    cli_registry_register(registry, cli_command_format());
    cli_registry_register(registry, cli_command_version());

    // Dispatch based on argv[1]
    cli_command_t* cmd = cli_registry_find(registry, argv[1]);
    int result = cmd->execute(argc - 1, &argv[1]);

    cli_registry_free(registry);
    return result;
}
```

### Command Implementation Template

```c
// commands/mycommand.c

static cli_result_t mycommand_execute(int argc, char** argv) {
    // 1. Parse options
    cli_mycommand_options_t* opts = cli_mycommand_options_new();
    if (cli_mycommand_options_parse(opts, argc, argv) != CLI_SUCCESS) {
        cli_mycommand_options_free(opts);
        return CLI_ERROR_ARGS;
    }

    // 2. Call API
    struct leptris_document* doc = leptris_parse(opts->input_file, &len);
    if (!doc) {
        cli_error_io(opts->input_file, "read", strerror(errno));
        cli_mycommand_options_free(opts);
        return CLI_ERROR_IO;
    }

    // 3. Format output
    output_formatter_t* fmt = output_formatter_create(opts->format);
    fmt->print_document(doc, stdout, fmt->context);

    // 4. Cleanup
    output_formatter_free(fmt);
    leptris_document_free(doc);
    cli_mycommand_options_free(opts);

    return CLI_SUCCESS;
}

static void mycommand_print_help(void) {
    printf("Usage: leptris mycommand [OPTIONS] FILE\n");
    printf("\nOptions:\n");
    printf("  -h, --help          Show this help\n");
    // ... more options
}

static cli_command_t mycommand = {
    .name = "mycommand",
    .description = "Do something with XML",
    .execute = mycommand_execute,
    .print_help = mycommand_print_help
};

cli_command_t* cli_command_mycommand(void) {
    return &mycommand;
}
```

---

## Option Handling (MECE)

### The MECE Hierarchy

Options are resolved from **three mutually exclusive sources** in order of specificity:

```
1. CLI Arguments (highest priority)
   └─→ --option value

2. Environment Variables (medium priority)
   └─→ LEPTRIS_OPTION=value

3. API Defaults (lowest priority)
   └─→ Hardcoded in code
```

### Resolution Example

```c
// User wants to know indent size for formatting

// Option specified on CLI? (highest priority)
int indent;
if (cli_opts->indent_specified) {
    indent = cli_opts->indent;  // Use CLI value
    source = OPTION_SOURCE_CLI;
}
// Option in environment?
else if (getenv("LEPTRIS_INDENT")) {
    indent = atoi(getenv("LEPTRIS_INDENT"));  // Use ENV value
    source = OPTION_SOURCE_ENV;
}
// Use default
else {
    indent = 2;  // Default value
    source = OPTION_SOURCE_DEFAULT;
}

// Now we know: indent=2, source=DEFAULT
```

### Option Structures

Each command has its own options struct:

```c
typedef struct cli_parse_options {
    const char* input_file;     // Required
    bool validate;              // --validate
    bool recover;               // --recover
    option_source_t validate_source;  // Tracing
} cli_parse_options_t;
```

### Option Parsing Pattern

```c
int cli_parse_options_parse(cli_parse_options_t* opts, int argc, char** argv) {
    option_parser_t parser = option_parser_new(argc, argv);

    while (option_parser_has_more(&parser)) {
        if (option_parser_match(&parser, "-v", "--validate")) {
            opts->validate = true;
            opts->validate_specified = true;
            option_parser_advance(&parser);
        }
        else if (option_parser_match(&parser, "-r", "--recover")) {
            opts->recover = true;
            opts->recover_specified = true;
            option_parser_advance(&parser);
        }
        else {
            // Positional argument (filename)
            opts->input_file = option_parser_current(&parser);
            option_parser_advance(&parser);
        }
    }

    // Resolve with MECE hierarchy
    opts->validate = resolve_bool_option(
        opts->validate,
        opts->validate_specified,
        "VALIDATE",
        false,  // default
        &opts->validate_source
    );

    return CLI_SUCCESS;
}
```

### Why MECE Matters

Without MECE, option handling becomes a mess:

```c
// BAD: Non-MECE (what if CLI and ENV both set?)
int indent = cli_opts->indent;  // Which one?
if (getenv("LEPTRIS_INDENT")) {
    indent = atoi(getenv("LEPTRIS_INDENT"));  // Override? Merge?
}

// GOOD: MECE (clear precedence)
int indent = resolve_int_option(
    cli_opts->indent,
    cli_opts->indent_specified,
    "INDENT",
    2,  // default
    &source
);
```

---

## Output Formatting

### Strategy Pattern

Output formatting uses the Strategy pattern with interchangeable formatters:

```c
typedef struct output_formatter {
    output_format_t type;
    void* context;

    void (*print_document)(...);
    void (*print_element)(...);
    void (*print_nodeset)(...);
    void (*print_string)(...);
    void (*print_number)(...);
    void (*print_boolean)(...);
    void (*print_error)(...);
} output_formatter_t;
```

### Formatter Factory

```c
output_formatter_t* output_formatter_create(output_format_t type) {
    switch (type) {
        case OUTPUT_FORMAT_XML:
            return create_xml_formatter();
        case OUTPUT_FORMAT_JSON:
            return create_json_formatter();
        case OUTPUT_FORMAT_TEXT:
            return create_text_formatter();
        default:
            return NULL;
    }
}
```

### XML Formatter

```xml
<!-- Default format (xmllint-compatible) -->
<book id="1">
  <title>The Book</title>
  <author>John Doe</author>
</book>
```

### JSON Formatter

```json
{
  "type": "nodeset",
  "count": 1,
  "nodes": [
    {
      "name": "book",
      "attributes": {"id": "1"},
      "children": [
        {"name": "title", "text": "The Book"},
        {"name": "author", "text": "John Doe"}
      ]
    }
  ]
}
```

### Text Formatter

```
book #1
  title: The Book
  author: John Doe
```

### Usage in Commands

```c
// In xpath command
output_formatter_t* fmt = output_formatter_create(global_opts->format);

struct leptris_xpath_result* result = leptris_xpath_eval(doc, expr, len);

// Dispatcher based on result type
switch (result->type) {
    case XPATH_RESULT_NODESET:
        fmt->print_nodeset(result, stdout, fmt->context);
        break;
    case XPATH_RESULT_STRING:
        fmt->print_string(result->value.string_value, stdout, fmt->context);
        break;
    // ... other types
}

output_formatter_free(fmt);
```

---

## Error Handling

### Error Levels

```c
typedef enum {
    ERROR_LEVEL_WARNING,  // Proceed with caution
    ERROR_LEVEL_ERROR,    // Operation failed
    ERROR_LEVEL_FATAL     // Must exit
} error_level_t;
```

### Error Context

```c
typedef struct cli_error {
    error_level_t level;
    const char* message;
    const char* file;      // Optional
    int line;              // Optional
    int column;            // Optional
    const char* suggestion;  // Optional
    int exit_code;
} cli_error_t;
```

### Error Printing

```
error: file.xml:10:5: unexpected end of element
suggestion: check element nesting
```

### Convenience Functions

```c
// Fatal error (exits immediately)
cli_fatal("cannot allocate memory");

// Regular error (continues)
cli_error("failed to parse XML: %s", reason);

// Warning (continues)
cli_warning("missing namespace declaration");

// Info (if not quiet)
cli_info("processing %d elements", count);

// Debug (if verbose)
cli_debug("resolved option: indent=%d", indent);
```

### Specialized Errors

```c
// Parse error
cli_error_parse("file.xml", 10, 5, "unexpected end of element");

// XPath error
cli_error_xpath("//book[@id=1]", "invalid predicate syntax");

// I/O error
cli_error_io("file.xml", "open", strerror(errno));

// Usage error
cli_error_usage("xpath", "missing expression argument");
```

---

## Extension Points

### Adding a New Command

1. **Create command file**: `cli/commands/mycommand.c`

2. **Define options struct**:
```c
typedef struct cli_mycommand_options {
    const char* input_file;
    bool my_flag;
    // ...
} cli_mycommand_options_t;
```

3. **Implement command**:
```c
static cli_result_t mycommand_execute(int argc, char** argv) {
    // Parse, call API, format, cleanup
}

static void mycommand_print_help(void) {
    printf("Usage: leptris mycommand...\n");
}

cli_command_t* cli_command_mycommand(void) {
    static cli_command_t cmd = {
        .name = "mycommand",
        .description = "Do something",
        .execute = mycommand_execute,
        .print_help = mycommand_print_help
    };
    return &cmd;
}
```

4. **Register in main.c**:
```c
cli_registry_register(registry, cli_command_mycommand());
```

### Adding a New Output Format

1. **Add enum value**:
```c
typedef enum {
    OUTPUT_FORMAT_XML,
    OUTPUT_FORMAT_JSON,
    OUTPUT_FORMAT_TEXT,
    OUTPUT_FORMAT_YAML  // New format
} output_format_t;
```

2. **Implement formatter**:
```c
static void yaml_print_document(...) {
    // YAML implementation
}

output_formatter_t* create_yaml_formatter(void) {
    output_formatter_t* fmt = malloc(sizeof(output_formatter_t));
    fmt->type = OUTPUT_FORMAT_YAML;
    fmt->print_document = yaml_print_document;
    // ... other methods
    return fmt;
}
```

3. **Add to factory**:
```c
output_formatter_t* output_formatter_create(output_format_t type) {
    switch (type) {
        // ... existing cases
        case OUTPUT_FORMAT_YAML:
            return create_yaml_formatter();
    }
}
```

---

## Design Decisions

### Why Command Pattern?

**Pros**:
- Extensible: Add commands without modifying core
- Testable: Each command is independent
- Clear: Each command is self-contained

**Cons**:
- More files
- Slight overhead (function pointers)

**Decision**: Pros outweigh cons for maintainability.

### Why MECE Option Hierarchy?

**Alternatives Considered**:
1. Environment overrides CLI (inverted precedence)
2. Merge CLI + ENV (complex, error-prone)
3. Only CLI (inflexible)

**Decision**: CLI > ENV > Default matches user expectations (most specific wins).

### Why Strategy Pattern for Output?

**Alternatives Considered**:
1. Switch statement in each command (code duplication)
2. Template system (too complex)
3. External format files (overkill)

**Decision**: Strategy pattern balances simplicity and extensibility.

### Why Thin CLI Layer?

**Alternatives Considered**:
1. CLI parses XML directly (tight coupling)
2. CLI wraps C++ API (language mixing)
3. CLI is part of library (boundary blur)

**Decision**: Thin layer maintains separation, makes API reusable.

### Why No Getopt?

**Alternatives Considered**:
1. getopt/getopt_long (standard but inflexible)
2. argp (GNU-specific)
3. Custom parser (flexible)

**Decision**: Custom parser gives us full control over behavior and error messages.

---

## Implementation Guidelines

### Code Style

- **Line limit**: 80 characters
- **Function limit**: 50 lines
- **File limit**: 800 lines
- **Indentation**: 4 spaces
- **Comments**: Doxygen-style

### Memory Management

```c
// Always match alloc/free
options = cli_parse_options_new();
// ... use options
cli_parse_options_free(options);

// Use API memory functions
doc = leptris_parse(...);
// ... use doc
leptris_document_free(doc);
```

### Error Handling

```c
// Always check return values
struct leptris_document* doc = leptris_parse(file, &len);
if (!doc) {
    cli_error_io(file, "parse", "invalid XML");
    return CLI_ERROR_PARSE;
}

// Use goto for cleanup
cli_result_t cmd_execute(...) {
    cli_result_t result = CLI_SUCCESS;
    options = NULL;
    doc = NULL;
    fmt = NULL;

    options = cli_options_new();
    if (!options) {
        result = CLI_ERROR_MEMORY;
        goto cleanup;
    }

    // ... more code

cleanup:
    if (fmt) output_formatter_free(fmt);
    if (doc) leptris_document_free(doc);
    if (options) cli_options_free(options);
    return result;
}
```

### Testing

Each component should have:

1. **Unit tests** (test/cli/test_*.c):
```c
TEST(OptionsTest, ParseValidOptions) {
    char* argv[] = {"parse", "--validate", "file.xml"};
    cli_parse_options_t* opts = cli_parse_options_new();

    int result = cli_parse_options_parse(opts, 3, argv);

    EXPECT_EQ(result, CLI_SUCCESS);
    EXPECT_TRUE(opts->validate);
    EXPECT_STREQ(opts->input_file, "file.xml");

    cli_parse_options_free(opts);
}
```

2. **Integration tests** (test/cli/integration/*.sh):
```bash
#!/bin/bash
# Test parse command

echo "<root><item/></root>" > test.xml
./leptris parse test.xml
if [ $? -eq 0 ]; then
    echo "PASS"
else
    echo "FAIL"
    exit 1
fi
rm test.xml
```

---

## Summary

The Leptris CLI architecture is designed with:

1. **MECE principles** throughout (options, commands, formats, errors)
2. **Clear separation** of concerns (CLI → API → Library)
3. **Extensibility** through patterns (Command, Strategy, Factory)
4. **Consistency** in error handling and output
5. **Testability** through modular design

**Next Steps**: Implement each component (Session 93+)