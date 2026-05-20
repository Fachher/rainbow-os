# Rainbow-OS Handoff

## Was ist das?

Ein eigenes 32-Bit Betriebssystem mit Custom Bootloader fuer eine Intel 486 Plattform, emuliert mit QEMU. Geschrieben in NASM (Bootloader) und C (Kernel).

## Aktueller Stand

Alle 9 Meilensteine sind abgeschlossen. Das System hat einen voll funktionsfaehigen Kernel mit Interrupts, Shell, Speicherverwaltung, Dateisystem, SVGA-Grafik und bootet von Floppy oder HDD.

| Meilenstein | Status |
|---|---|
| M1: Bootloader (2-Stufen, Protected Mode) | fertig |
| M2: VGA Textmodus (80x25) | fertig |
| M3: Serielle Konsole (COM1 Logging) | fertig |
| M4: Tastatureingabe (PS/2, IDT, PIC) | fertig |
| M5: Shell | fertig |
| M6: Speicherverwaltung (PMM, Paging) | fertig |
| M7: FAT12 Dateisystem (Ramdisk) | fertig |
| M8: SVGA Grafik (Cirrus GD5446) | fertig |
| M9: HDD Boot | fertig |

## Zielplattform (QEMU)

```
CPU:    Intel 486, 66 MHz (-cpu 486)
RAM:    32 MB (-m 32)
Grafik: Cirrus Logic GD5446 (-vga cirrus)
Boot:   Floppy 1.44 MB oder HDD (16 MB Image)
Serial: COM1 auf stdio (-serial stdio)
Debug:  GDB-Stub auf Port 1234 (-s -S)
```

Hinweis: Tseng ET4000AX wird von QEMU nicht unterstuetzt. Cirrus GD5446 ist der Ersatz.

## Entwicklungsumgebung

- **Host:** MacBook M1 (QEMU nutzt TCG, kein KVM)
- **IDE:** CLion
- **Cross-Compiler:** `i686-elf-gcc` 16.1.0 via Homebrew
- **Assembler:** NASM 3.01
- **Build:** CMake mit Custom Toolchain (`cmake/i686-elf-toolchain.cmake`)
- **Debugging:** GDB Remote in CLion (localhost:1234, Symbol: `build/kernel.elf`)

### Toolchain installieren

```bash
brew install i686-elf-gcc i686-elf-binutils nasm qemu
```

Optionaler GDB fuer Remote-Debugging:
```bash
brew install i686-elf-gdb
```

## Bauen und Starten

```bash
# Konfigurieren
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/i686-elf-toolchain.cmake

# Bauen (Bootloader + Kernel + Floppy- und HDD-Image)
cmake --build build

# Starten (Floppy)
./scripts/run.sh

# Starten (HDD)
./scripts/run-hdd.sh

# Starten mit GDB-Stub (wartet auf Debugger-Verbindung)
./scripts/debug.sh
```

### CLion Setup

1. **CMake-Profil:** Settings > Build > CMake > CMake options:
   `-DCMAKE_TOOLCHAIN_FILE=cmake/i686-elf-toolchain.cmake`
2. **GDB Remote Debug:** Run > Edit Configurations > GDB Remote Debug:
   - GDB Binary: `i686-elf-gdb`
   - Target Remote: `localhost:1234`
   - Symbol file: `build/kernel.elf`

## Architektur

### Boot-Ablauf

```
BIOS
  -> boot.asm (Stage 1, 0x7C00, 512 Bytes)
       A20 aktivieren, Boot-Drive merken, Stage 2 laden
  -> stage2.asm (Stage 2, 0x10000)
       Kernel laden, GDT aufsetzen, Protected Mode aktivieren
       Kernel nach 0x100000 (1 MB) kopieren
  -> entry.asm (_start, 0x100000)
       Stack einrichten, kernel_main() aufrufen
  -> kernel.c (kernel_main)
       Serial, VGA, PIC, IDT, PMM, Paging, Ramdisk, SVGA, Keyboard, Shell
```

### Disk-Layout (Floppy und HDD identisch)

| Sektor (1-indexed) | Inhalt |
|---|---|
| 1 | Stage 1 Bootloader (boot.bin, 512 B) |
| 2-5 | Stage 2 Bootloader (stage2.bin) |
| 6+ | Kernel (kernel.bin) |

### Speicher-Layout (Protected Mode)

| Adresse | Verwendung |
|---|---|
| 0x00000 - 0x07BFF | Frei (von BIOS genutzt) |
| 0x07C00 - 0x07DFF | Stage 1 Bootloader |
| 0x10000 - 0x1FFFF | Stage 2 Bootloader |
| 0x20000 - 0x2FFFF | Kernel Temp-Puffer (vor Kopie) |
| 0x90000 | Stack (waechst nach unten) |
| 0xB8000 | VGA Text-Buffer |
| 0x100000+ | Kernel (finale Position) |

## Projektstruktur

```
rainbow-os/
├── boot/
│   ├── boot.asm            # Stage 1: MBR, A20, Stage 2 laden
│   └── stage2.asm          # Stage 2: GDT, PM-Switch, Kernel laden
├── kernel/
│   ├── entry.asm           # _start -> kernel_main()
│   ├── isr_stubs.asm       # ISR/IRQ Assembly-Stubs (32 Exceptions + 16 IRQs)
│   ├── kernel.c            # Kernel-Hauptfunktion
│   ├── idt.c               # IDT Setup + Interrupt-Dispatcher
│   ├── pic.c               # 8259A PIC Remap + EOI
│   ├── pmm.c               # Physischer Speichermanager (Bitmap, 4K Frames)
│   ├── paging.c            # Paging (Identity Map 32 MB)
│   ├── linker.ld           # Linker-Skript (Base 0x100000, 4K-aligned)
│   ├── drivers/
│   │   ├── vga.c/h         # VGA Textmodus: putchar, write, scroll, cursor
│   │   ├── serial.c/h      # COM1: init, putchar, write, hex/dec output
│   │   ├── keyboard.c/h    # PS/2 Tastatur: IRQ1, Scancode Set 1, Shift
│   │   └── svga.c/h        # Cirrus GD5446: 640x480x8bpp, Bank-Switching, Zeichenprimitiven
│   ├── fs/
│   │   ├── fat12.c/h       # FAT12 Read-Only Treiber (BPB, FAT-Chain, Verzeichnis)
│   │   └── ramdisk.c/h     # 64 KB FAT12 Ramdisk (beim Boot formatiert)
│   ├── shell/
│   │   └── shell.c/h       # Kommandozeile (help, clear, version, meminfo, ls, cat, gfx, reboot)
│   ├── lib/
│   │   └── string.c/h      # memset, memcpy, strlen, strcmp, strncmp
│   └── include/
│       ├── types.h          # uint8/16/32_t, size_t
│       ├── io.h             # inb(), outb(), io_wait()
│       ├── idt.h            # IDT Structs, ISR-Frame, Handler-Typedef
│       ├── pic.h            # PIC Konstanten und API
│       ├── pmm.h            # PMM API (alloc/free frame)
│       └── paging.h         # Paging API
├── cmake/
│   └── i686-elf-toolchain.cmake
├── scripts/
│   ├── run.sh              # QEMU Floppy-Boot
│   ├── run-hdd.sh          # QEMU HDD-Boot
│   ├── debug.sh            # QEMU mit GDB-Stub (-s -S)
│   ├── mkimage.sh          # Floppy-Image (1.44 MB)
│   └── mkimage-hdd.sh      # HDD-Image (16 MB)
└── CMakeLists.txt
```

## Bekannte Eigenheiten

1. **ORG 0 in stage2.asm:** Stage 2 wird an Segment 0x1000 (linear 0x10000) geladen, nutzt aber `[ORG 0]` fuer 16-Bit Code. Der 32-Bit Far Jump nach Protected Mode muss daher die lineare Adresse explizit angeben (`jmp dword 0x08:(pm_entry + 0x10000)`), und der Kernel-Jump nutzt einen indirekten Jump (`mov eax, addr; jmp eax`), da NASM relative Offsets falsch berechnet.

2. **GCC 16 bool:** GCC 16.1 hat `bool` als eingebautes Keyword. `types.h` definiert daher kein eigenes `bool` -- `true`/`false` sind ebenfalls Built-ins.

3. **Clang-Warnungen in der IDE:** CLion zeigt Fehler fuer `inb`/`outb` (x86 inline asm) und include-Pfade, weil der lokale Clang (ARM) nicht der Cross-Compiler ist. Die Dateien kompilieren korrekt mit `i686-elf-gcc`.

4. **Kernel-Groesse:** Aktuell werden 64 Sektoren (32 KB) fuer den Kernel geladen. Bei groesserem Kernel muss `KERNEL_SECTORS` in `stage2.asm` erhoeht und ggf. Multi-Track-Loading implementiert werden (INT 13h kann nicht ueber Track-Grenzen lesen).

5. **Stage 2 DS-Reihenfolge:** `stage2.asm` muss `DS = CS` setzen *bevor* lokale Variablen (wie `boot_drive`) geschrieben werden, da nach dem Far Jump aus Stage 1 DS noch den alten Wert (0x0000) hat.

6. **SVGA Bank-Switching:** Der Cirrus GD5446 nutzt GR9 fuer Bank-Auswahl in 4 KB Einheiten. Fuer 64 KB Banken: `GR9 = bank_nummer * 16`. Der Framebuffer liegt bei 0xA0000 (64 KB Fenster).
