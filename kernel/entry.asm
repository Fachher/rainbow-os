; =============================================================================
; Kernel entry point
; Called from Stage 2 bootloader after switch to Protected Mode
; Sets up stack and calls kernel_main()
; =============================================================================

[BITS 32]

section .text
global _start
extern kernel_main

extern _bss_start
extern _bss_end

_start:
    ; Use Stage 2's stack temporarily (ESP = 0x90000) to zero BSS
    ; BSS must be zeroed before using our own stack (which lives in BSS)
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, edi
    shr ecx, 2              ; Convert bytes to dwords
    xor eax, eax
    cld
    rep stosd

    ; Now BSS is zeroed, safe to use our stack
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
