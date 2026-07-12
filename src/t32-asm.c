/*
 * t32-asm.c
 *
 * T32 assembler.
 *
 * Supported command forms:
 *
 *   t32-asm input.asm output.t32
 *   t32-asm -f bin input.asm -o output.bin
 *   t32-asm --format bin --output output.bin input.asm
 *
 * The positional form is retained for compatibility with the earliest
 * assembler and existing scripts.
 *
 * Output formats:
 *
 *   bin     Flat, directly loadable T32 binary image.
 *
 * Future formats such as "obj" should be introduced through the output-format
 * dispatch layer rather than by changing the command-line parser again.
 */

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define T32_ASM_VERSION "0.0.2"

#define MAX_LINES   8192
#define MAX_SYMS    4096
#define MAX_TOKENS  16
#define MAX_LINE    512

typedef enum {
    FORMAT_BIN = 0
} output_format_t;

typedef struct {
    const char *input_path;
    const char *output_path;
    output_format_t format;
    int verbose;
} options_t;

enum {
    OP_HALT = 0x00,
    OP_MOV  = 0x01,
    OP_MOVI = 0x02,
    OP_ADD  = 0x03,
    OP_ADDI = 0x04,
    OP_SUB  = 0x05,
    OP_SUBI = 0x06,
    OP_LDB  = 0x07,
    OP_STB  = 0x08,
    OP_JMP  = 0x09,
    OP_JZ   = 0x0A,
    OP_JNZ  = 0x0B
};

typedef struct {
    char name[64];
    uint32_t value;
} symbol_t;

typedef struct {
    char text[MAX_LINE];
    int line_number;
    uint32_t address;
} source_line_t;

static symbol_t symbols[MAX_SYMS];
static int symbol_count = 0;

static source_line_t source_lines[MAX_LINES];
static int source_line_count = 0;

static void print_usage(FILE *stream, const char *program)
{
    fprintf(
        stream,
        "usage:\n"
        "  %s input.asm output.t32\n"
        "  %s [options] input.asm\n"
        "\n"
        "options:\n"
        "  -f, --format <format>   Output format (currently: bin)\n"
        "  -o, --output <file>     Output file\n"
        "  -v, --verbose           Show selected format and paths\n"
        "      --version           Show version\n"
        "  -h, --help              Show this help\n",
        program,
        program
    );
}

static void fail_line(int line_number, const char *message)
{
    fprintf(stderr, "error:%d: %s\n", line_number, message);
    exit(EXIT_FAILURE);
}

static void fail_message(const char *message)
{
    fprintf(stderr, "t32-asm: %s\n", message);
    exit(EXIT_FAILURE);
}

static int copy_text(char *destination, size_t size, const char *source)
{
    size_t length;

    if (!destination || size == 0 || !source)
        return 0;

    length = strlen(source);
    if (length >= size)
        return 0;

    memcpy(destination, source, length + 1);
    return 1;
}

static void trim(char *text)
{
    char *start = text;
    size_t length;

    while (*start && isspace((unsigned char)*start))
        ++start;

    if (start != text)
        memmove(text, start, strlen(start) + 1);

    length = strlen(text);

    while (length > 0 &&
           isspace((unsigned char)text[length - 1])) {
        text[--length] = '\0';
    }
}

static void strip_comment(char *text)
{
    char *comment = strchr(text, ';');

    if (comment)
        *comment = '\0';
}

static int is_blank(const char *text)
{
    while (*text) {
        if (!isspace((unsigned char)*text))
            return 0;

        ++text;
    }

    return 1;
}

static void reset_assembler_state(void)
{
    symbol_count = 0;
    source_line_count = 0;
    memset(symbols, 0, sizeof(symbols));
    memset(source_lines, 0, sizeof(source_lines));
}

static void add_symbol(
    const char *name,
    uint32_t value,
    int line_number
)
{
    int index;

    for (index = 0; index < symbol_count; ++index) {
        if (strcmp(symbols[index].name, name) == 0)
            fail_line(line_number, "duplicate symbol");
    }

    if (symbol_count >= MAX_SYMS)
        fail_line(line_number, "too many symbols");

    if (!copy_text(
            symbols[symbol_count].name,
            sizeof(symbols[symbol_count].name),
            name)) {
        fail_line(line_number, "symbol name is too long");
    }

    symbols[symbol_count].value = value;
    ++symbol_count;
}

static int find_symbol(const char *name, uint32_t *value)
{
    int index;

    for (index = 0; index < symbol_count; ++index) {
        if (strcmp(symbols[index].name, name) == 0) {
            *value = symbols[index].value;
            return 1;
        }
    }

    return 0;
}

static uint32_t parse_value(const char *text, int line_number)
{
    uint32_t symbol_value;
    char *end = NULL;
    unsigned long value;

    if (find_symbol(text, &symbol_value))
        return symbol_value;

    errno = 0;
    value = strtoul(text, &end, 0);

    if (errno == 0 &&
        end != text &&
        end &&
        *end == '\0' &&
        value <= 0xfffffffful) {
        return (uint32_t)value;
    }

    fail_line(line_number, "unknown value or symbol");
    return 0;
}

static int parse_register(const char *text, int line_number)
{
    char *end = NULL;
    long register_number;

    if ((text[0] != 'r' && text[0] != 'R') ||
        !isdigit((unsigned char)text[1])) {
        fail_line(line_number, "expected register r0-r15");
    }

    register_number = strtol(text + 1, &end, 10);

    if (!end ||
        *end != '\0' ||
        register_number < 0 ||
        register_number > 15) {
        fail_line(line_number, "register out of range");
    }

    return (int)register_number;
}

static uint32_t encode_instruction(
    uint8_t opcode,
    uint8_t destination,
    uint8_t source_a,
    uint8_t source_b
)
{
    return
        ((uint32_t)opcode << 24) |
        ((uint32_t)destination << 20) |
        ((uint32_t)source_a << 16) |
        ((uint32_t)source_b << 12);
}

static void write_u32(FILE *output, uint32_t value)
{
    fputc((int)((value >> 0) & 0xffu), output);
    fputc((int)((value >> 8) & 0xffu), output);
    fputc((int)((value >> 16) & 0xffu), output);
    fputc((int)((value >> 24) & 0xffu), output);
}

static int tokenize(char *text, char *tokens[])
{
    int count = 0;
    char *cursor;
    char *token;

    for (cursor = text; *cursor; ++cursor) {
        if (*cursor == ',' ||
            *cursor == '[' ||
            *cursor == ']') {
            *cursor = ' ';
        }
    }

    token = strtok(text, " \t\r\n");

    while (token && count < MAX_TOKENS) {
        tokens[count++] = token;
        token = strtok(NULL, " \t\r\n");
    }

    return count;
}

static uint32_t instruction_size(char *line, int line_number)
{
    char temporary[MAX_LINE];
    char *tokens[MAX_TOKENS];
    int count;

    if (!copy_text(temporary, sizeof(temporary), line))
        fail_line(line_number, "source line is too long");

    count = tokenize(temporary, tokens);

    if (count == 0)
        return 0;

    if (tokens[0][0] == '.') {
        if (strcmp(tokens[0], ".byte") == 0)
            return 1;

        if (strcmp(tokens[0], ".word") == 0)
            return 4;

        if (strcmp(tokens[0], ".ascii") == 0) {
            char *first_quote = strchr(line, '"');
            char *last_quote =
                first_quote ? strrchr(first_quote + 1, '"') : NULL;

            if (!first_quote ||
                !last_quote ||
                last_quote <= first_quote) {
                fail_line(line_number, "bad .ascii string");
            }

            return (uint32_t)(last_quote - first_quote - 1);
        }

        if (strcmp(tokens[0], ".equ") == 0)
            return 0;

        fail_line(line_number, "unknown directive");
    }

    if (strcmp(tokens[0], "movi") == 0 ||
        strcmp(tokens[0], "addi") == 0 ||
        strcmp(tokens[0], "subi") == 0 ||
        strcmp(tokens[0], "jmp") == 0 ||
        strcmp(tokens[0], "jz") == 0 ||
        strcmp(tokens[0], "jnz") == 0) {
        return 8;
    }

    return 4;
}

static void first_pass(void)
{
    uint32_t program_counter = 0;
    int index;

    for (index = 0; index < source_line_count; ++index) {
        source_line_t *line = &source_lines[index];
        char buffer[MAX_LINE];
        char *colon;

        if (!copy_text(buffer, sizeof(buffer), line->text))
            fail_line(line->line_number, "source line is too long");

        trim(buffer);

        if (is_blank(buffer))
            continue;

        colon = strchr(buffer, ':');

        if (colon) {
            char *remainder;

            *colon = '\0';
            trim(buffer);
            add_symbol(buffer, program_counter, line->line_number);

            remainder = colon + 1;
            trim(remainder);

            if (is_blank(remainder)) {
                line->address = program_counter;
                continue;
            }

            if (!copy_text(line->text, sizeof(line->text), remainder))
                fail_line(line->line_number, "source line is too long");
        }

        line->address = program_counter;

        {
            char temporary[MAX_LINE];

            if (!copy_text(temporary, sizeof(temporary), line->text))
                fail_line(line->line_number, "source line is too long");

            trim(temporary);

            if (strncmp(temporary, ".equ", 4) == 0) {
                char *tokens[MAX_TOKENS];
                int count = tokenize(temporary, tokens);

                if (count != 3)
                    fail_line(
                        line->line_number,
                        "usage: .equ NAME VALUE"
                    );

                add_symbol(
                    tokens[1],
                    parse_value(tokens[2], line->line_number),
                    line->line_number
                );
                continue;
            }
        }

        program_counter +=
            instruction_size(line->text, line->line_number);
    }
}

static void emit_ascii(
    FILE *output,
    char *line,
    int line_number
)
{
    char *first_quote = strchr(line, '"');
    char *last_quote =
        first_quote ? strrchr(first_quote + 1, '"') : NULL;
    char *cursor;

    if (!first_quote ||
        !last_quote ||
        last_quote <= first_quote) {
        fail_line(line_number, "bad .ascii string");
    }

    for (cursor = first_quote + 1;
         cursor < last_quote;
         ++cursor) {
        if (*cursor == '\\' && cursor + 1 < last_quote) {
            ++cursor;

            if (*cursor == 'n')
                fputc('\n', output);
            else if (*cursor == 'r')
                fputc('\r', output);
            else if (*cursor == 't')
                fputc('\t', output);
            else
                fputc((unsigned char)*cursor, output);
        } else {
            fputc((unsigned char)*cursor, output);
        }
    }
}

static void emit_instruction(
    FILE *output,
    char *tokens[],
    int count,
    int line_number
)
{
    const char *operation = tokens[0];

    if (strcmp(operation, "halt") == 0) {
        if (count != 1)
            fail_line(line_number, "usage: halt");

        write_u32(output, encode_instruction(OP_HALT, 0, 0, 0));
    } else if (strcmp(operation, "mov") == 0) {
        if (count != 3)
            fail_line(line_number, "usage: mov rd, ra");

        write_u32(
            output,
            encode_instruction(
                OP_MOV,
                (uint8_t)parse_register(tokens[1], line_number),
                (uint8_t)parse_register(tokens[2], line_number),
                0
            )
        );
    } else if (strcmp(operation, "movi") == 0) {
        if (count != 3)
            fail_line(line_number, "usage: movi rd, imm32");

        write_u32(
            output,
            encode_instruction(
                OP_MOVI,
                (uint8_t)parse_register(tokens[1], line_number),
                0,
                0
            )
        );
        write_u32(output, parse_value(tokens[2], line_number));
    } else if (strcmp(operation, "add") == 0) {
        if (count != 4)
            fail_line(line_number, "usage: add rd, ra, rb");

        write_u32(
            output,
            encode_instruction(
                OP_ADD,
                (uint8_t)parse_register(tokens[1], line_number),
                (uint8_t)parse_register(tokens[2], line_number),
                (uint8_t)parse_register(tokens[3], line_number)
            )
        );
    } else if (strcmp(operation, "addi") == 0) {
        if (count != 4)
            fail_line(line_number, "usage: addi rd, ra, imm32");

        write_u32(
            output,
            encode_instruction(
                OP_ADDI,
                (uint8_t)parse_register(tokens[1], line_number),
                (uint8_t)parse_register(tokens[2], line_number),
                0
            )
        );
        write_u32(output, parse_value(tokens[3], line_number));
    } else if (strcmp(operation, "sub") == 0) {
        if (count != 4)
            fail_line(line_number, "usage: sub rd, ra, rb");

        write_u32(
            output,
            encode_instruction(
                OP_SUB,
                (uint8_t)parse_register(tokens[1], line_number),
                (uint8_t)parse_register(tokens[2], line_number),
                (uint8_t)parse_register(tokens[3], line_number)
            )
        );
    } else if (strcmp(operation, "subi") == 0) {
        if (count != 4)
            fail_line(line_number, "usage: subi rd, ra, imm32");

        write_u32(
            output,
            encode_instruction(
                OP_SUBI,
                (uint8_t)parse_register(tokens[1], line_number),
                (uint8_t)parse_register(tokens[2], line_number),
                0
            )
        );
        write_u32(output, parse_value(tokens[3], line_number));
    } else if (strcmp(operation, "ldb") == 0) {
        if (count != 3)
            fail_line(line_number, "usage: ldb rd, [ra]");

        write_u32(
            output,
            encode_instruction(
                OP_LDB,
                (uint8_t)parse_register(tokens[1], line_number),
                (uint8_t)parse_register(tokens[2], line_number),
                0
            )
        );
    } else if (strcmp(operation, "stb") == 0) {
        if (count != 3)
            fail_line(line_number, "usage: stb rb, [ra]");

        write_u32(
            output,
            encode_instruction(
                OP_STB,
                0,
                (uint8_t)parse_register(tokens[2], line_number),
                (uint8_t)parse_register(tokens[1], line_number)
            )
        );
    } else if (strcmp(operation, "jmp") == 0) {
        if (count != 2)
            fail_line(line_number, "usage: jmp label");

        write_u32(output, encode_instruction(OP_JMP, 0, 0, 0));
        write_u32(output, parse_value(tokens[1], line_number));
    } else if (strcmp(operation, "jz") == 0) {
        if (count != 3)
            fail_line(line_number, "usage: jz ra, label");

        write_u32(
            output,
            encode_instruction(
                OP_JZ,
                0,
                (uint8_t)parse_register(tokens[1], line_number),
                0
            )
        );
        write_u32(output, parse_value(tokens[2], line_number));
    } else if (strcmp(operation, "jnz") == 0) {
        if (count != 3)
            fail_line(line_number, "usage: jnz ra, label");

        write_u32(
            output,
            encode_instruction(
                OP_JNZ,
                0,
                (uint8_t)parse_register(tokens[1], line_number),
                0
            )
        );
        write_u32(output, parse_value(tokens[2], line_number));
    } else {
        fail_line(line_number, "unknown instruction");
    }
}

static void second_pass_binary(FILE *output)
{
    int index;

    for (index = 0; index < source_line_count; ++index) {
        source_line_t *line = &source_lines[index];
        char buffer[MAX_LINE];
        char temporary[MAX_LINE];
        char *tokens[MAX_TOKENS];
        char *colon;
        int count;

        if (!copy_text(buffer, sizeof(buffer), line->text))
            fail_line(line->line_number, "source line is too long");

        trim(buffer);

        if (is_blank(buffer))
            continue;

        colon = strchr(buffer, ':');

        if (colon) {
            char *remainder = colon + 1;

            trim(remainder);

            if (is_blank(remainder))
                continue;

            memmove(buffer, remainder, strlen(remainder) + 1);
        }

        if (!copy_text(temporary, sizeof(temporary), buffer))
            fail_line(line->line_number, "source line is too long");

        count = tokenize(temporary, tokens);

        if (count == 0)
            continue;

        if (tokens[0][0] == '.') {
            if (strcmp(tokens[0], ".equ") == 0) {
                continue;
            } else if (strcmp(tokens[0], ".byte") == 0) {
                if (count != 2)
                    fail_line(line->line_number, "usage: .byte VALUE");

                fputc(
                    (int)(parse_value(
                        tokens[1],
                        line->line_number
                    ) & 0xffu),
                    output
                );
                continue;
            } else if (strcmp(tokens[0], ".word") == 0) {
                if (count != 2)
                    fail_line(line->line_number, "usage: .word VALUE");

                write_u32(
                    output,
                    parse_value(tokens[1], line->line_number)
                );
                continue;
            } else if (strcmp(tokens[0], ".ascii") == 0) {
                emit_ascii(output, buffer, line->line_number);
                continue;
            }

            fail_line(line->line_number, "unknown directive");
        }

        emit_instruction(
            output,
            tokens,
            count,
            line->line_number
        );
    }
}

static output_format_t parse_format(const char *text)
{
    if (strcmp(text, "bin") == 0)
        return FORMAT_BIN;

    fprintf(stderr, "t32-asm: unsupported output format: %s\n", text);
    fprintf(stderr, "t32-asm: supported formats: bin\n");
    exit(EXIT_FAILURE);
}

static const char *format_name(output_format_t format)
{
    switch (format) {
    case FORMAT_BIN:
        return "bin";
    default:
        return "unknown";
    }
}

static int parse_options(int argc, char **argv, options_t *options)
{
    const char *positionals[2] = {NULL, NULL};
    int positional_count = 0;
    int index;

    memset(options, 0, sizeof(*options));
    options->format = FORMAT_BIN;

    for (index = 1; index < argc; ++index) {
        const char *argument = argv[index];

        if (strcmp(argument, "-h") == 0 ||
            strcmp(argument, "--help") == 0) {
            print_usage(stdout, argv[0]);
            exit(EXIT_SUCCESS);
        } else if (strcmp(argument, "--version") == 0) {
            printf("t32-asm %s\n", T32_ASM_VERSION);
            exit(EXIT_SUCCESS);
        } else if (strcmp(argument, "-v") == 0 ||
                   strcmp(argument, "--verbose") == 0) {
            options->verbose = 1;
        } else if (strcmp(argument, "-f") == 0 ||
                   strcmp(argument, "--format") == 0) {
            if (++index >= argc) {
                fprintf(stderr, "t32-asm: missing argument for %s\n", argument);
                return 0;
            }

            options->format = parse_format(argv[index]);
        } else if (strcmp(argument, "-o") == 0 ||
                   strcmp(argument, "--output") == 0) {
            if (++index >= argc) {
                fprintf(stderr, "t32-asm: missing argument for %s\n", argument);
                return 0;
            }

            options->output_path = argv[index];
        } else if (argument[0] == '-') {
            fprintf(stderr, "t32-asm: unknown option: %s\n", argument);
            return 0;
        } else {
            if (positional_count >= 2) {
                fprintf(stderr, "t32-asm: too many positional arguments\n");
                return 0;
            }

            positionals[positional_count++] = argument;
        }
    }

    if (positional_count == 2 && !options->output_path) {
        options->input_path = positionals[0];
        options->output_path = positionals[1];
    } else if (positional_count == 1 && options->output_path) {
        options->input_path = positionals[0];
    } else {
        fprintf(stderr, "t32-asm: input and output files are required\n");
        return 0;
    }

    return 1;
}

static int load_source(const char *path)
{
    FILE *input;
    char buffer[MAX_LINE];
    int line_number = 0;

    input = fopen(path, "r");
    if (!input) {
        perror(path);
        return 0;
    }

    while (fgets(buffer, sizeof(buffer), input)) {
        ++line_number;

        if (!strchr(buffer, '\n') && !feof(input)) {
            fclose(input);
            fprintf(
                stderr,
                "error:%d: source line exceeds %d bytes\n",
                line_number,
                MAX_LINE - 1
            );
            return 0;
        }

        strip_comment(buffer);
        trim(buffer);

        if (is_blank(buffer))
            continue;

        if (source_line_count >= MAX_LINES) {
            fclose(input);
            fprintf(stderr, "t32-asm: too many source lines\n");
            return 0;
        }

        if (!copy_text(
                source_lines[source_line_count].text,
                sizeof(source_lines[source_line_count].text),
                buffer)) {
            fclose(input);
            fprintf(
                stderr,
                "error:%d: source line is too long\n",
                line_number
            );
            return 0;
        }

        source_lines[source_line_count].line_number = line_number;
        ++source_line_count;
    }

    if (ferror(input)) {
        perror(path);
        fclose(input);
        return 0;
    }

    fclose(input);
    return 1;
}

static int write_output(const options_t *options)
{
    FILE *output;

    output = fopen(options->output_path, "wb");
    if (!output) {
        perror(options->output_path);
        return 0;
    }

    switch (options->format) {
    case FORMAT_BIN:
        second_pass_binary(output);
        break;

    default:
        fclose(output);
        fail_message("internal error: unknown output format");
    }

    if (fclose(output) != 0) {
        perror(options->output_path);
        return 0;
    }

    return 1;
}

int main(int argc, char **argv)
{
    options_t options;

    if (!parse_options(argc, argv, &options)) {
        print_usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    reset_assembler_state();

    if (!load_source(options.input_path))
        return EXIT_FAILURE;

    first_pass();

    if (options.verbose) {
        fprintf(
            stderr,
            "input:  %s\n"
            "output: %s\n"
            "format: %s\n",
            options.input_path,
            options.output_path,
            format_name(options.format)
        );
    }

    if (!write_output(&options))
        return EXIT_FAILURE;

    printf(
        "assembled %s -> %s\n",
        options.input_path,
        options.output_path
    );

    return EXIT_SUCCESS;
}
