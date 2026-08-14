.intel_syntax noprefix

.section .rdata,"dr"
.p2align 4
.globl user_snake_start
user_snake_start:
    .incbin "build/user_snake.bin"
.globl user_snake_end
user_snake_end:
