// t32-asm.c - tiny32 first-pass assembler
// build: gcc -Wall -Wextra -O2 -o t32-asm t32-asm.c

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINES   8192
#define MAX_SYMS    4096
#define MAX_TOKENS  16
#define MAX_LINE    512

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
} Symbol;

typedef struct {
    char text[MAX_LINE];
    int line_no;
    uint32_t addr;
} Line;

static Symbol syms[MAX_SYMS];
static int sym_count = 0;

static Line lines[MAX_LINES];
static int line_count = 0;

static void die_line(int line_no, const char *msg) {
    fprintf(stderr, "error:%d: %s\n", line_no, msg);
    exit(1);
}

static void trim(char *s) {
    char *p = s;
    while (isspace((unsigned char)*p)) p++;
    memmove(s, p, strlen(p) + 1);

    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) {
        s[--n] = 0;
    }
}

static void strip_comment(char *s) {
    char *p = strchr(s, ';');
    if (p) *p = 0;
}

static int is_blank(const char *s) {
    while (*s) {
        if (!isspace((unsigned char)*s)) return 0;
        s++;
    }
    return 1;
}

static void add_sym(const char *name, uint32_t value, int line_no) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(syms[i].name, name) == 0) {
            die_line(line_no, "duplicate symbol");
        }
    }

    if (sym_count >= MAX_SYMS) die_line(line_no, "too many symbols");

    strncpy(syms[sym_count].name, name, sizeof(syms[sym_count].name) - 1);
    syms[sym_count].value = value;
    sym_count++;
}

static int find_sym(const char *name, uint32_t *out) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(syms[i].name, name) == 0) {
            *out = syms[i].value;
            return 1;
        }
    }
    return 0;
}

static uint32_t parse_value(const char *s, int line_no) {
    uint32_t v;

    if (find_sym(s, &v)) return v;

    char *end = NULL;
    unsigned long x = strtoul(s, &end, 0);
    if (end && *end == 0) return (uint32_t)x;

    die_line(line_no, "unknown value/symbol");
    return 0;
}

static int parse_reg(const char *s, int line_no) {
    if ((s[0] != 'r' && s[0] != 'R') || !isdigit((unsigned char)s[1])) {
        die_line(line_no, "expected register r0-r15");
    }

    int r = atoi(s + 1);
    if (r < 0 || r > 15) die_line(line_no, "register out of range");
    return r;
}

static uint32_t enc(uint8_t op, uint8_t rd, uint8_t ra, uint8_t rb) {
    return ((uint32_t)op << 24) |
           ((uint32_t)rd << 20) |
           ((uint32_t)ra << 16) |
           ((uint32_t)rb << 12);
}

static void write_u32(FILE *f, uint32_t v) {
    fputc((v >> 0) & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f);
    fputc((v >> 24) & 0xFF, f);
}

static int tokenize(char *s, char *tok[]) {
    int n = 0;

    for (char *p = s; *p; p++) {
        if (*p == ',' || *p == '[' || *p == ']') *p = ' ';
    }

    char *p = strtok(s, " \t\r\n");
    while (p && n < MAX_TOKENS) {
        tok[n++] = p;
        p = strtok(NULL, " \t\r\n");
    }

    return n;
}

static uint32_t instr_size(char *line, int line_no) {
    char temp[MAX_LINE];
    strncpy(temp, line, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = 0;

    char *tok[MAX_TOKENS];
    int n = tokenize(temp, tok);
    if (n == 0) return 0;

    if (tok[0][0] == '.') {
        if (strcmp(tok[0], ".byte") == 0) return 1;
        if (strcmp(tok[0], ".word") == 0) return 4;

        if (strcmp(tok[0], ".ascii") == 0) {
            char *q1 = strchr(line, '"');
            char *q2 = q1 ? strrchr(q1 + 1, '"') : NULL;
            if (!q1 || !q2 || q2 <= q1) die_line(line_no, "bad .ascii string");
            return (uint32_t)(q2 - q1 - 1);
        }

        if (strcmp(tok[0], ".equ") == 0) return 0;

        die_line(line_no, "unknown directive");
    }

    if (!strcmp(tok[0], "movi") ||
        !strcmp(tok[0], "addi") ||
        !strcmp(tok[0], "subi") ||
        !strcmp(tok[0], "jmp")  ||
        !strcmp(tok[0], "jz")   ||
        !strcmp(tok[0], "jnz")) {
        return 8;
    }

    return 4;
}

static void pass1(void) {
    uint32_t pc = 0;

    for (int i = 0; i < line_count; i++) {
        char buf[MAX_LINE];
        strncpy(buf, lines[i].text, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;

        trim(buf);
        if (is_blank(buf)) continue;

        char *colon = strchr(buf, ':');
        if (colon) {
            *colon = 0;
            trim(buf);
            add_sym(buf, pc, lines[i].line_no);

            char *rest = colon + 1;
            trim(rest);
            if (is_blank(rest)) {
                lines[i].addr = pc;
                continue;
            }

            strncpy(lines[i].text, rest, sizeof(lines[i].text) - 1);
        }

        lines[i].addr = pc;

        char temp[MAX_LINE];
        strncpy(temp, lines[i].text, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = 0;
        trim(temp);

        if (strncmp(temp, ".equ", 4) == 0) {
            char *tok[MAX_TOKENS];
            int n = tokenize(temp, tok);
            if (n != 3) die_line(lines[i].line_no, "usage: .equ NAME VALUE");
            add_sym(tok[1], parse_value(tok[2], lines[i].line_no), lines[i].line_no);
            continue;
        }

        pc += instr_size(lines[i].text, lines[i].line_no);
    }
}

static void emit_ascii(FILE *out, char *line, int line_no) {
    char *q1 = strchr(line, '"');
    char *q2 = q1 ? strrchr(q1 + 1, '"') : NULL;
    if (!q1 || !q2 || q2 <= q1) die_line(line_no, "bad .ascii string");

    for (char *p = q1 + 1; p < q2; p++) {
        if (*p == '\\' && p + 1 < q2) {
            p++;
            if (*p == 'n') fputc('\n', out);
            else if (*p == 'r') fputc('\r', out);
            else if (*p == 't') fputc('\t', out);
            else fputc(*p, out);
        } else {
            fputc(*p, out);
        }
    }
}

static void pass2(FILE *out) {
    for (int i = 0; i < line_count; i++) {
        char buf[MAX_LINE];
        strncpy(buf, lines[i].text, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        trim(buf);

        if (is_blank(buf)) continue;

        char *colon = strchr(buf, ':');
        if (colon) {
            char *rest = colon + 1;
            trim(rest);

            if (is_blank(rest))
                continue;

            memmove(buf, rest, strlen(rest) + 1);
        }

        char tmp[MAX_LINE];
        strncpy(tmp, buf, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = 0;

        char *tok[MAX_TOKENS];
        int n = tokenize(tmp, tok);
        if (n == 0) continue;

        int ln = lines[i].line_no;

        if (tok[0][0] == '.') {
            if (strcmp(tok[0], ".equ") == 0) continue;

            if (strcmp(tok[0], ".byte") == 0) {
                if (n != 2) die_line(ln, "usage: .byte VALUE");
                fputc(parse_value(tok[1], ln) & 0xFF, out);
                continue;
            }

            if (strcmp(tok[0], ".word") == 0) {
                if (n != 2) die_line(ln, "usage: .word VALUE");
                write_u32(out, parse_value(tok[1], ln));
                continue;
            }

            if (strcmp(tok[0], ".ascii") == 0) {
                emit_ascii(out, buf, ln);
                continue;
            }

            die_line(ln, "unknown directive");
        }

        if (strcmp(tok[0], "halt") == 0) {
            write_u32(out, enc(OP_HALT, 0, 0, 0));
        }
        else if (strcmp(tok[0], "mov") == 0) {
            if (n != 3) die_line(ln, "usage: mov rd, ra");
            write_u32(out, enc(OP_MOV, parse_reg(tok[1], ln), parse_reg(tok[2], ln), 0));
        }
        else if (strcmp(tok[0], "movi") == 0) {
            if (n != 3) die_line(ln, "usage: movi rd, imm32");
            write_u32(out, enc(OP_MOVI, parse_reg(tok[1], ln), 0, 0));
            write_u32(out, parse_value(tok[2], ln));
        }
        else if (strcmp(tok[0], "add") == 0) {
            if (n != 4) die_line(ln, "usage: add rd, ra, rb");
            write_u32(out, enc(OP_ADD, parse_reg(tok[1], ln), parse_reg(tok[2], ln), parse_reg(tok[3], ln)));
        }
        else if (strcmp(tok[0], "addi") == 0) {
            if (n != 4) die_line(ln, "usage: addi rd, ra, imm32");
            write_u32(out, enc(OP_ADDI, parse_reg(tok[1], ln), parse_reg(tok[2], ln), 0));
            write_u32(out, parse_value(tok[3], ln));
        }
        else if (strcmp(tok[0], "sub") == 0) {
            if (n != 4) die_line(ln, "usage: sub rd, ra, rb");
            write_u32(out, enc(OP_SUB, parse_reg(tok[1], ln), parse_reg(tok[2], ln), parse_reg(tok[3], ln)));
        }
        else if (strcmp(tok[0], "subi") == 0) {
            if (n != 4) die_line(ln, "usage: subi rd, ra, imm32");
            write_u32(out, enc(OP_SUBI, parse_reg(tok[1], ln), parse_reg(tok[2], ln), 0));
            write_u32(out, parse_value(tok[3], ln));
        }
        else if (strcmp(tok[0], "ldb") == 0) {
            if (n != 3) die_line(ln, "usage: ldb rd, [ra]");
            write_u32(out, enc(OP_LDB, parse_reg(tok[1], ln), parse_reg(tok[2], ln), 0));
        }
        else if (strcmp(tok[0], "stb") == 0) {
            if (n != 3) die_line(ln, "usage: stb rb, [ra]");
            write_u32(out, enc(OP_STB, 0, parse_reg(tok[2], ln), parse_reg(tok[1], ln)));
        }
        else if (strcmp(tok[0], "jmp") == 0) {
            if (n != 2) die_line(ln, "usage: jmp label");
            write_u32(out, enc(OP_JMP, 0, 0, 0));
            write_u32(out, parse_value(tok[1], ln));
        }
        else if (strcmp(tok[0], "jz") == 0) {
            if (n != 3) die_line(ln, "usage: jz ra, label");
            write_u32(out, enc(OP_JZ, 0, parse_reg(tok[1], ln), 0));
            write_u32(out, parse_value(tok[2], ln));
        }
        else if (strcmp(tok[0], "jnz") == 0) {
            if (n != 3) die_line(ln, "usage: jnz ra, label");
            write_u32(out, enc(OP_JNZ, 0, parse_reg(tok[1], ln), 0));
            write_u32(out, parse_value(tok[2], ln));
        }
        else {
            die_line(ln, "unknown instruction");
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s input.t32asm output.t32\n", argv[0]);
        return 1;
    }

    FILE *in = fopen(argv[1], "r");
    if (!in) {
        perror(argv[1]);
        return 1;
    }

    char buf[MAX_LINE];
    int line_no = 0;

    while (fgets(buf, sizeof(buf), in)) {
        line_no++;

        strip_comment(buf);
        trim(buf);

        if (is_blank(buf)) continue;

        if (line_count >= MAX_LINES) {
            fprintf(stderr, "too many lines\n");
            fclose(in);
            return 1;
        }

        snprintf(lines[line_count].text,
         sizeof(lines[line_count].text),
         "%s",
         buf);
        lines[line_count].line_no = line_no;
        line_count++;
    }

    fclose(in);

    pass1();

    FILE *out = fopen(argv[2], "wb");
    if (!out) {
        perror(argv[2]);
        return 1;
    }

    pass2(out);
    fclose(out);

    printf("assembled %s -> %s\n", argv[1], argv[2]);
    return 0;
}
