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

#define T32_ASM_VERSION "0.0.4"

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

enum
{
    /*----------------------------------------------------------
     * System
     *----------------------------------------------------------*/

    OP_HALT    = 0x00,
    OP_NOP     = 0x01,
    OP_TRAP    = 0x02,
    OP_IRET    = 0x03,
    OP_CPUID   = 0x04,

    /* 0x05 - 0x07 Reserved */

    /*----------------------------------------------------------
     * Register Movement
     *----------------------------------------------------------*/

    OP_MOV     = 0x08,
    OP_MOVI    = 0x09,

    /* 0x0A - 0x0F Reserved */

    /*----------------------------------------------------------
     * Memory
     *----------------------------------------------------------*/

    OP_LDB     = 0x10,
    OP_LDH     = 0x11,
    OP_LDW     = 0x12,

    OP_STB     = 0x13,
    OP_STH     = 0x14,
    OP_STW     = 0x15,

    /* 0x16 - 0x17 Reserved */

    /*----------------------------------------------------------
     * Arithmetic
     *----------------------------------------------------------*/

    OP_ADD     = 0x18,
    OP_ADDI    = 0x19,

    OP_SUB     = 0x1A,
    OP_SUBI    = 0x1B,

    OP_MUL     = 0x1C,
    OP_MULU    = 0x1D,

    OP_DIV     = 0x1E,
    OP_DIVU    = 0x1F,

    /*----------------------------------------------------------
     * Logic
     *----------------------------------------------------------*/

    OP_AND     = 0x20,
    OP_OR      = 0x21,
    OP_XOR     = 0x22,
    OP_NOT     = 0x23,

    OP_SHL     = 0x24,
    OP_SHR     = 0x25,
    OP_SAR     = 0x26,

    /* 0x27 Reserved */

    /*----------------------------------------------------------
     * Compare / Branch
     *----------------------------------------------------------*/

    OP_CMP     = 0x28,
    OP_CMPI    = 0x29,

    OP_JMP     = 0x2A,
    OP_JZ      = 0x2B,
    OP_JNZ     = 0x2C,

    /* 0x2D - 0x2F Reserved */

    /*----------------------------------------------------------
     * Stack / Call
     *----------------------------------------------------------*/

    OP_PUSH    = 0x30,
    OP_POP     = 0x31,

    OP_CALL    = 0x32,
    OP_RET     = 0x33
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

    if (strcmp(tokens[0], "trap") == 0 ||
        strcmp(tokens[0], "movi") == 0 ||
        strcmp(tokens[0], "addi") == 0 ||
        strcmp(tokens[0], "subi") == 0 ||
        strcmp(tokens[0], "cmpi") == 0 ||
        strcmp(tokens[0], "jmp") == 0 ||
        strcmp(tokens[0], "jz") == 0 ||
        strcmp(tokens[0], "jnz") == 0 ||
        strcmp(tokens[0], "call") == 0) {
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

#define REQUIRE_COUNT(expected, usage_text) \
    do { if (count != (expected)) fail_line(line_number, (usage_text)); } while (0)

#define EMIT0(opcode_value) \
    write_u32(output, encode_instruction((opcode_value), 0, 0, 0))

#define EMIT_R(opcode_value, reg_a) \
    write_u32(output, encode_instruction((opcode_value), \
        (uint8_t)parse_register((reg_a), line_number), 0, 0))

#define EMIT_RA(opcode_value, reg_a) \
    write_u32(output, encode_instruction((opcode_value), 0, \
        (uint8_t)parse_register((reg_a), line_number), 0))

#define EMIT_RR(opcode_value, rd_text, ra_text) \
    write_u32(output, encode_instruction((opcode_value), \
        (uint8_t)parse_register((rd_text), line_number), \
        (uint8_t)parse_register((ra_text), line_number), 0))

#define EMIT_RRR(opcode_value, rd_text, ra_text, rb_text) \
    write_u32(output, encode_instruction((opcode_value), \
        (uint8_t)parse_register((rd_text), line_number), \
        (uint8_t)parse_register((ra_text), line_number), \
        (uint8_t)parse_register((rb_text), line_number)))

    if (strcmp(operation, "halt") == 0) {
        REQUIRE_COUNT(1, "usage: halt");
        EMIT0(OP_HALT);
    } else if (strcmp(operation, "nop") == 0) {
        REQUIRE_COUNT(1, "usage: nop");
        EMIT0(OP_NOP);
    } else if (strcmp(operation, "trap") == 0) {
        REQUIRE_COUNT(2, "usage: trap vector");
        EMIT0(OP_TRAP);
        write_u32(output, parse_value(tokens[1], line_number));
    } else if (strcmp(operation, "iret") == 0) {
        REQUIRE_COUNT(1, "usage: iret");
        EMIT0(OP_IRET);
    } else if (strcmp(operation, "cpuid") == 0) {
        REQUIRE_COUNT(2, "usage: cpuid rd");
        EMIT_R(OP_CPUID, tokens[1]);

    } else if (strcmp(operation, "mov") == 0) {
        REQUIRE_COUNT(3, "usage: mov rd, ra");
        EMIT_RR(OP_MOV, tokens[1], tokens[2]);
    } else if (strcmp(operation, "movi") == 0) {
        REQUIRE_COUNT(3, "usage: movi rd, imm32");
        EMIT_R(OP_MOVI, tokens[1]);
        write_u32(output, parse_value(tokens[2], line_number));

    } else if (strcmp(operation, "ldb") == 0) {
        REQUIRE_COUNT(3, "usage: ldb rd, [ra]");
        EMIT_RR(OP_LDB, tokens[1], tokens[2]);
    } else if (strcmp(operation, "ldh") == 0) {
        REQUIRE_COUNT(3, "usage: ldh rd, [ra]");
        EMIT_RR(OP_LDH, tokens[1], tokens[2]);
    } else if (strcmp(operation, "ldw") == 0) {
        REQUIRE_COUNT(3, "usage: ldw rd, [ra]");
        EMIT_RR(OP_LDW, tokens[1], tokens[2]);
    } else if (strcmp(operation, "stb") == 0) {
        REQUIRE_COUNT(3, "usage: stb rb, [ra]");
        write_u32(output, encode_instruction(
            OP_STB, 0,
            (uint8_t)parse_register(tokens[2], line_number),
            (uint8_t)parse_register(tokens[1], line_number)));
    } else if (strcmp(operation, "sth") == 0) {
        REQUIRE_COUNT(3, "usage: sth rb, [ra]");
        write_u32(output, encode_instruction(
            OP_STH, 0,
            (uint8_t)parse_register(tokens[2], line_number),
            (uint8_t)parse_register(tokens[1], line_number)));
    } else if (strcmp(operation, "stw") == 0) {
        REQUIRE_COUNT(3, "usage: stw rb, [ra]");
        write_u32(output, encode_instruction(
            OP_STW, 0,
            (uint8_t)parse_register(tokens[2], line_number),
            (uint8_t)parse_register(tokens[1], line_number)));

    } else if (strcmp(operation, "add") == 0) {
        REQUIRE_COUNT(4, "usage: add rd, ra, rb");
        EMIT_RRR(OP_ADD, tokens[1], tokens[2], tokens[3]);
    } else if (strcmp(operation, "addi") == 0) {
        REQUIRE_COUNT(4, "usage: addi rd, ra, imm32");
        EMIT_RR(OP_ADDI, tokens[1], tokens[2]);
        write_u32(output, parse_value(tokens[3], line_number));
    } else if (strcmp(operation, "sub") == 0) {
        REQUIRE_COUNT(4, "usage: sub rd, ra, rb");
        EMIT_RRR(OP_SUB, tokens[1], tokens[2], tokens[3]);
    } else if (strcmp(operation, "subi") == 0) {
        REQUIRE_COUNT(4, "usage: subi rd, ra, imm32");
        EMIT_RR(OP_SUBI, tokens[1], tokens[2]);
        write_u32(output, parse_value(tokens[3], line_number));
    } else if (strcmp(operation, "mul") == 0) {
        REQUIRE_COUNT(4, "usage: mul rd, ra, rb");
        EMIT_RRR(OP_MUL, tokens[1], tokens[2], tokens[3]);
    } else if (strcmp(operation, "mulu") == 0) {
        REQUIRE_COUNT(4, "usage: mulu rd, ra, rb");
        EMIT_RRR(OP_MULU, tokens[1], tokens[2], tokens[3]);
    } else if (strcmp(operation, "div") == 0) {
        REQUIRE_COUNT(4, "usage: div rd, ra, rb");
        EMIT_RRR(OP_DIV, tokens[1], tokens[2], tokens[3]);
    } else if (strcmp(operation, "divu") == 0) {
        REQUIRE_COUNT(4, "usage: divu rd, ra, rb");
        EMIT_RRR(OP_DIVU, tokens[1], tokens[2], tokens[3]);

    } else if (strcmp(operation, "and") == 0) {
        REQUIRE_COUNT(4, "usage: and rd, ra, rb");
        EMIT_RRR(OP_AND, tokens[1], tokens[2], tokens[3]);
    } else if (strcmp(operation, "or") == 0) {
        REQUIRE_COUNT(4, "usage: or rd, ra, rb");
        EMIT_RRR(OP_OR, tokens[1], tokens[2], tokens[3]);
    } else if (strcmp(operation, "xor") == 0) {
        REQUIRE_COUNT(4, "usage: xor rd, ra, rb");
        EMIT_RRR(OP_XOR, tokens[1], tokens[2], tokens[3]);
    } else if (strcmp(operation, "not") == 0) {
        REQUIRE_COUNT(3, "usage: not rd, ra");
        EMIT_RR(OP_NOT, tokens[1], tokens[2]);
    } else if (strcmp(operation, "shl") == 0) {
        REQUIRE_COUNT(4, "usage: shl rd, ra, rb");
        EMIT_RRR(OP_SHL, tokens[1], tokens[2], tokens[3]);
    } else if (strcmp(operation, "shr") == 0) {
        REQUIRE_COUNT(4, "usage: shr rd, ra, rb");
        EMIT_RRR(OP_SHR, tokens[1], tokens[2], tokens[3]);
    } else if (strcmp(operation, "sar") == 0) {
        REQUIRE_COUNT(4, "usage: sar rd, ra, rb");
        EMIT_RRR(OP_SAR, tokens[1], tokens[2], tokens[3]);

    } else if (strcmp(operation, "cmp") == 0) {
        REQUIRE_COUNT(3, "usage: cmp ra, rb");
        write_u32(output, encode_instruction(
            OP_CMP, 0,
            (uint8_t)parse_register(tokens[1], line_number),
            (uint8_t)parse_register(tokens[2], line_number)));
    } else if (strcmp(operation, "cmpi") == 0) {
        REQUIRE_COUNT(3, "usage: cmpi ra, imm32");
        EMIT_RA(OP_CMPI, tokens[1]);
        write_u32(output, parse_value(tokens[2], line_number));
    } else if (strcmp(operation, "jmp") == 0) {
        REQUIRE_COUNT(2, "usage: jmp target");
        EMIT0(OP_JMP);
        write_u32(output, parse_value(tokens[1], line_number));
    } else if (strcmp(operation, "jz") == 0) {
        REQUIRE_COUNT(3, "usage: jz ra, target");
        EMIT_RA(OP_JZ, tokens[1]);
        write_u32(output, parse_value(tokens[2], line_number));
    } else if (strcmp(operation, "jnz") == 0) {
        REQUIRE_COUNT(3, "usage: jnz ra, target");
        EMIT_RA(OP_JNZ, tokens[1]);
        write_u32(output, parse_value(tokens[2], line_number));

    } else if (strcmp(operation, "push") == 0) {
        REQUIRE_COUNT(2, "usage: push ra");
        EMIT_RA(OP_PUSH, tokens[1]);
    } else if (strcmp(operation, "pop") == 0) {
        REQUIRE_COUNT(2, "usage: pop rd");
        EMIT_R(OP_POP, tokens[1]);
    } else if (strcmp(operation, "call") == 0) {
        REQUIRE_COUNT(2, "usage: call target");
        EMIT0(OP_CALL);
        write_u32(output, parse_value(tokens[1], line_number));
    } else if (strcmp(operation, "ret") == 0) {
        REQUIRE_COUNT(1, "usage: ret");
        EMIT0(OP_RET);
    } else {
        fail_line(line_number, "unknown instruction");
    }

#undef EMIT_RRR
#undef EMIT_RR
#undef EMIT_RA
#undef EMIT_R
#undef EMIT0
#undef REQUIRE_COUNT
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