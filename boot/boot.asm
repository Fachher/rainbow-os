; =============================================================================
; Rainbow-OS Stage 1 Bootloader
; Loaded by BIOS at 0x7C00 (512 bytes, MBR)
;
; Tasks:
;   1. Set up stack and segments
;   2. Save boot drive number
;   3. Enable A20 line
;   4. Load Stage 2 from boot drive (sectors 1-4)
;   5. Pass boot drive in DL, jump to Stage 2
; =============================================================================

[BITS 16]
[ORG 0x7C00]

STAGE2_LOAD_SEG     equ 0x1000     ; Stage 2 loaded at 0x1000:0x0000 = 0x10000
STAGE2_SECTORS      equ 4          ; 4 sectors = 2 KB

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00                  ; Stack grows down from 0x7C00
    sti

    ; Save boot drive (BIOS passes it in DL)
    mov [boot_drive], dl

    ; Print boot message
    mov si, msg_booting
    call print_rm

    ; Enable A20 line (fast method via port 0x92)
    in al, 0x92
    test al, 2
    jnz .a20_done                   ; Already enabled
    or al, 2
    and al, 0xFE                    ; Don't trigger fast reset (bit 0)
    out 0x92, al
.a20_done:

    ; Load Stage 2 from floppy
    mov si, msg_loading
    call print_rm

    ; Reset floppy controller
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13

    ; Read sectors: INT 13h AH=02h
    mov ah, 0x02                    ; BIOS read sectors
    mov al, STAGE2_SECTORS          ; Number of sectors
    mov ch, 0                       ; Cylinder 0
    mov cl, 2                       ; Start at sector 2 (1-indexed, sector 1 = boot)
    mov dh, 0                       ; Head 0
    mov dl, [boot_drive]            ; Drive number
    mov bx, STAGE2_LOAD_SEG
    mov es, bx
    xor bx, bx                     ; ES:BX = 0x1000:0x0000 = 0x10000
    int 0x13
    jc disk_error

    ; Pass boot drive to Stage 2 in DL
    mov dl, [boot_drive]

    ; Jump to Stage 2
    jmp STAGE2_LOAD_SEG:0x0000

disk_error:
    mov si, msg_disk_err
    call print_rm
    cli
    hlt

; -----------------------------------------------------------------------------
; print_rm: Print null-terminated string in Real Mode (SI = string pointer)
; -----------------------------------------------------------------------------
print_rm:
    pusha
.loop:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E                    ; BIOS teletype
    mov bx, 0x0007                  ; Page 0, light gray
    int 0x10
    jmp .loop
.done:
    popa
    ret

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------
boot_drive:     db 0
msg_booting:    db "Rainbow-OS v0.1", 13, 10, 0
msg_loading:    db "Loading Stage 2...", 13, 10, 0
msg_disk_err:   db "DISK ERROR!", 13, 10, 0

; Pad to 510 bytes and add boot signature
times 510 - ($ - $$) db 0
dw 0xAA55
