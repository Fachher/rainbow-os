# Rainbow-OS Handoff

## Was ist das?

Ein eigenes 32-Bit Betriebssystem mit Custom Bootloader fuer eine Intel 486 Plattform, emuliert mit QEMU. Geschrieben in NASM (Bootloader) und C (Kernel).

## Aktueller Stand

Meilensteine 1-3 sind abgeschlossen. Das System bootet von Floppy, wechselt in den Protected Mode und gibt Text auf dem Bildschirm und der seriellen Konsole aus.

| Meilenstein | Status |
|---|---|
| M1: Bootloader (2-Stufen, Protected Mode) | fertig |
| M2: VGA Textmodus (80x25) | fertig |
| M3: Serielle Konsole (COM1 Logging) | fertig |
| M4: Tastatureingabe (PS/2, IDT, PIC) | offen |
| M5: Shell | offen |
| M6: Speicherverwaltung (PMM, Paging) | offen |
| M7: FAT12/FAT16 Dateisystem | offen |
| M8: SVGA Grafik (Cirrus GD5446) | offen |
| M9: HDD Boot | offen |

## Zielplattform (QEMU)

```
CPU:    Intel 486, 66 MHz (-cpu 486)
RAM:    32 MB (-m 32)
Grafik: Cirrus Logic GD5446 (-vga cirrus)
Boot:   Floppy 1.44 MB
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

# Bauen (Bootloader + Kernel + Floppy-Image)
cmake --build build

# Starten
./scripts/run.sh

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
       A20 aktivieren, Stage 2 von Floppy laden
  -> stage2.asm (Stage 2, 0x10000)
       Kernel laden, GDT aufsetzen, Protected Mode aktivieren
       Kernel nach 0x100000 (1 MB) kopieren
  -> entry.asm (_start, 0x100000)
       Stack einrichten, kernel_main() aufrufen
  -> kernel.c (kernel_main)
       Serial init, VGA init, Willkommensmeldung
```

### Floppy-Layout

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
│   ├── kernel.c            # Kernel-Hauptfunktion
│   ├── linker.ld           # Linker-Skript (Base 0x100000, 4K-aligned)
│   ├── drivers/
│   │   ├── vga.c/h         # VGA Textmodus: putchar, write, scroll, cursor
│   │   └── serial.c/h      # COM1: init, putchar, write, hex/dec output
│   ├── lib/
│   │   └── string.c/h      # memset, memcpy, strlen
│   └── include/
│       ├── types.h          # uint8/16/32_t, size_t
│       └── io.h             # inb(), outb(), io_wait()
├── cmake/
│   └── i686-elf-toolchain.cmake
├── scripts/
│   ├── run.sh              # QEMU normal starten
│   ├── debug.sh            # QEMU mit GDB-Stub (-s -S)
│   └── mkimage.sh          # Floppy-Image aus boot+stage2+kernel bauen
└── CMakeLists.txt
```

## Bekannte Eigenheiten

1. **ORG 0 in stage2.asm:** Stage 2 wird an Segment 0x1000 (linear 0x10000) geladen, nutzt aber `[ORG 0]` fuer 16-Bit Code. Der 32-Bit Far Jump nach Protected Mode muss daher die lineare Adresse explizit angeben (`jmp dword 0x08:(pm_entry + 0x10000)`), und der Kernel-Jump nutzt einen indirekten Jump (`mov eax, addr; jmp eax`), da NASM relative Offsets falsch berechnet.

2. **GCC 16 bool:** GCC 16.1 hat `bool` als eingebautes Keyword. `types.h` definiert daher kein eigenes `bool` -- `true`/`false` sind ebenfalls Built-ins.

3. **Clang-Warnungen in der IDE:** CLion zeigt Fehler fuer `inb`/`outb` (x86 inline asm) und include-Pfade, weil der lokale Clang (ARM) nicht der Cross-Compiler ist. Die Dateien kompilieren korrekt mit `i686-elf-gcc`.

4. **Kernel-Groesse:** Aktuell werden 64 Sektoren (32 KB) fuer den Kernel geladen. Bei groesserem Kernel muss `KERNEL_SECTORS` in `stage2.asm` erhoeht und ggf. Multi-Track-Loading implementiert werden (INT 13h kann nicht ueber Track-Grenzen lesen).

## Naechster Meilenstein: M4 Tastatureingabe

Erfordert:
- **IDT** (Interrupt Descriptor Table) einrichten
- **PIC** (8259A) programmieren -- IRQs auf ISR 32-47 remappen
- **ISR-Stubs** in Assembly (Register sichern, C-Handler aufrufen)
- **PS/2 Tastatur-Treiber** -- IRQ1 Handler, Scancode-Tabelle
