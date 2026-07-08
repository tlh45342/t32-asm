; hello.asm - tiny32 video memory hello

.equ VIDEO_BASE, 0x90003000
.equ VIDEO_SIZE, 4000
.equ ATTR,       0x07

start:
    movi r0, VIDEO_BASE
    movi r1, VIDEO_SIZE
    movi r2, 0

clear_loop:
    stb r2, [r0]
    addi r0, r0, 1
    subi r1, r1, 1
    jnz r1, clear_loop

    movi r0, VIDEO_BASE
    movi r1, message
    movi r2, ATTR

print_loop:
    ldb r3, [r1]
    jz r3, done

    stb r3, [r0]

    addi r0, r0, 2
    addi r1, r1, 1
    jmp print_loop

done:
    halt

message:
    .ascii "Hello World"
    .byte 0
