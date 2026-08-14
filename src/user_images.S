.intel_syntax noprefix

.section .rdata,"dr"
.p2align 4
.globl user_snake_start
user_snake_start:
    .incbin "build/user_snake.bin"
.globl user_snake_end
user_snake_end:

.p2align 4
.globl user_calculator_start
user_calculator_start:
    .incbin "build/user_calculator.bin"
.globl user_calculator_end
user_calculator_end:

.p2align 4
.globl user_notes_start
user_notes_start:
    .incbin "build/user_notes.bin"
.globl user_notes_end
user_notes_end:
