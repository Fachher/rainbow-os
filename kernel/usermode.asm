; =============================================================================
; Ring 3 entry / exit (coroutine-style swap with the kernel)
;
; enter_user(eip, user_esp): save the kernel context, then iret down to ring 3.
; return_to_kernel(): restore that context and resume right after enter_user.
; Called from the syscall / fault handlers (which run on the TSS esp0 stack)
; when a user program exits or faults.
; =============================================================================

[BITS 32]
section .text

global enter_user
global return_to_kernel
extern saved_kernel_esp

; void enter_user(uint32_t entry_eip, uint32_t user_esp)
enter_user:
    pusha                       ; save kernel registers
    mov [saved_kernel_esp], esp ; remember where to come back to

    mov ecx, [esp + 36]         ; arg1: entry eip
    mov edx, [esp + 40]         ; arg2: user esp

    mov ax, 0x23                ; user data selector (RPL 3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push 0x23                   ; SS  (user data)
    push edx                    ; ESP (user stack)
    pushfd
    pop eax
    or eax, 0x200               ; set IF so interrupts stay enabled in ring 3
    push eax                    ; EFLAGS
    push 0x1B                   ; CS  (user code, RPL 3)
    push ecx                    ; EIP (program entry)
    iret

; void return_to_kernel(void)  — does not return to its caller
return_to_kernel:
    mov ax, 0x10                ; kernel data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov esp, [saved_kernel_esp]
    popa                        ; restore kernel registers from enter_user
    sti                         ; re-enable interrupts (gate had cleared IF)
    ret                         ; resume right after enter_user()
