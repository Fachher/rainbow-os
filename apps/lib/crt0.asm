; Userland C runtime startup. Placed first in the binary (section .start) so it
; sits at the load address (0x200000) where the kernel begins execution.
; Zeroes .bss (the loader copies only the .bin image), calls main, then exits.

[BITS 32]
section .start
global _start
extern main
extern __bss_start
extern __bss_end

_start:
    ; zero .bss
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    add ecx, 3
    shr ecx, 2
    xor eax, eax
    cld
    rep stosd

    call main                ; int main(void)

    ; sys_exit(eax)
    push eax
    mov ebx, esp             ; ebx -> exit code
    mov eax, 3               ; SYS_EXIT
    int 0x80
.hang:
    jmp .hang
