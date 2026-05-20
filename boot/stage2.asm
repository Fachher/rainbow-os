; =============================================================================
; Rainbow-OS Stage 2 Bootloader
; Loaded at 0x1000:0x0000 (linear 0x10000)
;
; Tasks:
;   1. Load kernel sectors from boot drive into temp buffer (Real Mode)
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
    ; Set up segments first (we're at 0x1000:0x0000)
    mov ax, cs
    mov ds, ax
    mov es, ax

    ; Save boot drive (passed from Stage 1 in DL)
    mov [boot_drive], dl

    ; Print message
    mov si, msg_stage2
    call print_rm16

    ; Query drive geometry via INT 13h AH=08h
    mov ah, 0x08
    mov dl, [boot_drive]
    xor di, di
    push es
    int 0x13
    pop es
    jc .use_default_geo               ; If query fails, assume floppy defaults
    and cl, 0x3F                       ; CL bits 0-5 = max sector number
    mov [spt], cl
    jmp .geo_done
.use_default_geo:
    mov byte [spt], 18                 ; Default: 1.44 MB floppy
.geo_done:

    ; Load kernel into temp buffer at 0x20000 (multi-track safe)
    mov ax, KERNEL_TMP_SEG
    mov es, ax
    xor bx, bx                         ; ES:BX = 0x2000:0x0000

    mov word [sectors_left], KERNEL_SECTORS
    mov byte [cur_sector], KERNEL_START_SECTOR
    mov byte [cur_head], 0
    mov word [cur_cylinder], 0

.read_loop:
    ; Calculate how many sectors remain on this track
    movzx ax, byte [cur_sector]
    movzx cx, byte [spt]
    inc cx                              ; CX = max_sector + 1
    sub cx, ax                          ; CX = sectors left on this track
    cmp cx, [sectors_left]
    jbe .clamp_ok
    mov cx, [sectors_left]              ; Don't read more than needed
.clamp_ok:
    ; CX = number of sectors to read this iteration
    mov al, cl                          ; AL = sector count
    mov ah, 0x02                        ; BIOS read sectors
    mov ch, [cur_cylinder]              ; Cylinder
    mov cl, [cur_sector]                ; Start sector (1-indexed)
    mov dh, [cur_head]                  ; Head
    mov dl, [boot_drive]                ; Boot drive
    int 0x13
    jc .disk_error

    ; Advance buffer pointer: AL sectors * 512 bytes
    movzx cx, al
    shl cx, 9                           ; CX = bytes read
    add bx, cx                          ; Advance BX

    ; Subtract sectors read from total
    movzx cx, al
    sub [sectors_left], cx
    cmp word [sectors_left], 0
    je .read_done

    ; Advance to next track: sector resets to 1, head toggles, cylinder increments
    mov byte [cur_sector], 1
    mov al, [cur_head]
    xor al, 1                           ; Toggle head (0->1, 1->0)
    mov [cur_head], al
    test al, al
    jnz .read_loop                      ; If head is now 1, same cylinder
    inc word [cur_cylinder]             ; Head wrapped to 0: next cylinder
    jmp .read_loop

.read_done:

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
boot_drive:     db 0
spt:            db 18                   ; Sectors per track (queried from BIOS)
sectors_left:   dw 0
cur_sector:     db 0
cur_head:       db 0
cur_cylinder:   dw 0

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
