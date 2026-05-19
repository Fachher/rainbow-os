; =============================================================================
; Kernel entry point
; Called from Stage 2 bootloader after switch to Protected Mode
; Sets up stack and calls kernel_main()
; =============================================================================

[BITS 32]

section .text
global _start
extern kernel_main

_start:
    ; Stack is already set up by Stage 2 (ESP = 0x90000)
    ; But we set up our own stack in BSS for a cleaner setup
    mov esp, stack_top

    ; Call the C kernel
    call kernel_main

    ; If kernel_main returns, halt the CPU
    cli
.halt:
    hlt
    jmp .halt

; =============================================================================
; Stack (16 KB in BSS)
; =============================================================================
section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
