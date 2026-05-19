; =============================================================================
; ISR and IRQ stub entry points
; Save registers, call C handler, restore registers
; =============================================================================

[BITS 32]

section .text

; External C handler
extern isr_handler

; =============================================================================
; IDT load helper
; =============================================================================
global idt_flush
idt_flush:
    mov eax, [esp+4]       ; Pointer to idt_ptr
    lidt [eax]
    sti                     ; Enable interrupts
    ret

; =============================================================================
; ISR common stub - called after int_no and err_code are on stack
; =============================================================================
isr_common_stub:
    pusha                   ; Push edi,esi,ebp,esp,ebx,edx,ecx,eax

    mov ax, ds
    push eax               ; Save data segment

    mov ax, 0x10            ; Kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                ; Pass pointer to isr_frame as argument
    call isr_handler
    add esp, 4              ; Clean up argument

    pop eax                 ; Restore data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    add esp, 8              ; Clean up int_no and err_code
    iret

; =============================================================================
; CPU exception stubs (ISR 0-31)
; Some push error codes, some don't - we push a dummy 0 for those that don't
; =============================================================================

; Macro: ISR with no error code (push dummy 0)
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push dword 0            ; Dummy error code
    push dword %1           ; Interrupt number
    jmp isr_common_stub
%endmacro

; Macro: ISR with error code (CPU already pushed it)
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push dword %1           ; Interrupt number
    jmp isr_common_stub
%endmacro

; Exceptions 0-31
ISR_NOERRCODE 0             ; Division by zero
ISR_NOERRCODE 1             ; Debug
ISR_NOERRCODE 2             ; NMI
ISR_NOERRCODE 3             ; Breakpoint
ISR_NOERRCODE 4             ; Overflow
ISR_NOERRCODE 5             ; Bound range exceeded
ISR_NOERRCODE 6             ; Invalid opcode
ISR_NOERRCODE 7             ; Device not available
ISR_ERRCODE   8             ; Double fault
ISR_NOERRCODE 9             ; Coprocessor segment overrun
ISR_ERRCODE   10            ; Invalid TSS
ISR_ERRCODE   11            ; Segment not present
ISR_ERRCODE   12            ; Stack-segment fault
ISR_ERRCODE   13            ; General protection fault
ISR_ERRCODE   14            ; Page fault
ISR_NOERRCODE 15            ; Reserved
ISR_NOERRCODE 16            ; x87 FP exception
ISR_ERRCODE   17            ; Alignment check
ISR_NOERRCODE 18            ; Machine check
ISR_NOERRCODE 19            ; SIMD FP exception
ISR_NOERRCODE 20            ; Virtualization exception
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30            ; Security exception
ISR_NOERRCODE 31

; =============================================================================
; IRQ stubs (ISR 32-47)
; All push dummy error code 0 and interrupt number = IRQ + 32
; =============================================================================

%macro IRQ 2
global irq%1
irq%1:
    push dword 0            ; Dummy error code
    push dword %2           ; Interrupt number (IRQ + 32)
    jmp isr_common_stub
%endmacro

IRQ 0,  32                  ; PIT timer
IRQ 1,  33                  ; Keyboard
IRQ 2,  34                  ; Cascade
IRQ 3,  35                  ; COM2
IRQ 4,  36                  ; COM1
IRQ 5,  37                  ; LPT2
IRQ 6,  38                  ; Floppy
IRQ 7,  39                  ; LPT1 / spurious
IRQ 8,  40                  ; CMOS RTC
IRQ 9,  41                  ; Free
IRQ 10, 42                  ; Free
IRQ 11, 43                  ; Free
IRQ 12, 44                  ; PS/2 mouse
IRQ 13, 45                  ; FPU
IRQ 14, 46                  ; Primary ATA
IRQ 15, 47                  ; Secondary ATA
