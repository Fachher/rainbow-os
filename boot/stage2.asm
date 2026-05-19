; =============================================================================
; Rainbow-OS Stage 2 Bootloader
; Loaded at 0x1000:0x0000 (linear 0x10000)
;
; Tasks:
;   1. Load kernel sectors from floppy into temp buffer (Real Mode)
;   2. Set up GDT (flat model, 4 GB segments)
;   3. Switch to 32-bit Protected Mode
;   4. Copy kernel to 0x100000 (1 MB)
;   5. Jump to kernel entry point
; =============================================================================

[BITS 16]
[ORG 0x0000]

KERNEL_TMP_SEG      equ 0x2000         ; Temp buffer at 0x2000:0x0000 = 0x20000
KERNEL_LOAD_ADDR    equ 0x100000       ; Final kernel address: 1 MB
KERNEL_START_SECTOR equ 6              ; Kernel starts at sector 6 (1-indexed)
KERNEL_SECTORS      equ 64             ; 64 sectors = 32 KB max kernel size

stage2_start:
    ; Set up segments (we're at 0x1000:0x0000)
    mov ax, cs
    mov ds, ax
    mov es, ax

    ; Print message
    mov si, msg_stage2
    call print_rm16

    ; Load kernel into temp buffer at 0x20000
    mov ax, KERNEL_TMP_SEG
    mov es, ax
    xor bx, bx                         ; ES:BX = 0x2000:0x0000

    mov ah, 0x02                        ; BIOS read sectors
    mov al, KERNEL_SECTORS
    mov ch, 0                           ; Cylinder 0
    mov cl, KERNEL_START_SECTOR         ; Start sector
    mov dh, 0                           ; Head 0
    mov dl, 0x00                        ; Floppy A:
    int 0x13
    jc .disk_error

    mov si, msg_pm
    call print_rm16

    ; Disable interrupts before PM switch
    cli

    ; Load GDT
    lgdt [gdt_descriptor]

    ; Enable Protected Mode (CR0 bit 0)
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump to flush pipeline and enter 32-bit code
    ; Need 32-bit far jump (66h prefix) since we're still in [BITS 16]
    ; Target is linear address: 0x10000 + pm_entry offset
    jmp dword 0x08:(pm_entry + 0x10000)

.disk_error:
    mov si, msg_disk_err
    call print_rm16
    cli
    hlt

; -----------------------------------------------------------------------------
; print_rm16: Print string in Real Mode (SI = offset, DS = segment)
; -----------------------------------------------------------------------------
print_rm16:
    pusha
.loop:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .loop
.done:
    popa
    ret

; -----------------------------------------------------------------------------
; Data (16-bit)
; -----------------------------------------------------------------------------
msg_stage2:     db "Stage 2 loaded", 13, 10, 0
msg_pm:         db "Entering Protected Mode...", 13, 10, 0
msg_disk_err:   db "Kernel load error!", 13, 10, 0

; =============================================================================
; GDT - Global Descriptor Table (Flat Model)
; =============================================================================
align 8
gdt_start:
    ; Null descriptor
    dq 0

    ; Code segment: Base=0, Limit=4GB, 32-bit, Ring 0, Execute/Read
gdt_code:
    dw 0xFFFF           ; Limit low
    dw 0x0000           ; Base low
    db 0x00             ; Base middle
    db 10011010b        ; Access: Present, Ring 0, Code, Execute/Read
    db 11001111b        ; Flags: 4KB granularity, 32-bit + Limit high
    db 0x00             ; Base high

    ; Data segment: Base=0, Limit=4GB, 32-bit, Ring 0, Read/Write
gdt_data:
    dw 0xFFFF           ; Limit low
    dw 0x0000           ; Base low
    db 0x00             ; Base middle
    db 10010010b        ; Access: Present, Ring 0, Data, Read/Write
    db 11001111b        ; Flags: 4KB granularity, 32-bit + Limit high
    db 0x00             ; Base high
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1         ; GDT size
    dd gdt_start + 0x10000             ; Linear address (segment 0x1000 * 16 + offset)

; =============================================================================
; 32-bit Protected Mode Code
; =============================================================================
[BITS 32]

pm_entry:
    ; Set up data segments
    mov ax, 0x10            ; Data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000        ; Stack at 576 KB

    ; Copy kernel from temp buffer (0x20000) to final address (0x100000)
    mov esi, 0x20000
    mov edi, KERNEL_LOAD_ADDR
    mov ecx, (KERNEL_SECTORS * 512) / 4     ; Copy dwords
    cld
    rep movsd

    ; Jump to kernel (indirect to avoid relative offset miscalculation with ORG 0)
    mov eax, KERNEL_LOAD_ADDR
    jmp eax
