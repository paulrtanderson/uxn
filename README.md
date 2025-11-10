## Threads Device (Multithreading Extension)

This fork adds a new **Threads device to Varvara**, exposing a minimal subset of the
POSIX threads API. The goal is to enable limited parallel execution in Uxn.

Device port map
Base: 0xD0–0xDB (12 bytes total)

- D0 CMD
- D1 STATUS
  - 0=idle, 1=busy, 2=ok, 3=error
- D2–D3 A0 (16-bit)
- D4–D5 A1 (16-bit)
- D6–D7 A2 (16-bit)
- D8–D9 R0 (16-bit result)
- DA ERRNO (8-bit, set when STATUS=3) — optional; alternatively put error code in R0
- DB RESERVED

Notes:
- A0/A1/A2 are per-command inputs; R0 is the per-command output.
- For >3 inputs or >1 output, use an arg-block convention: A2 = IN_PTR to a RAM struct (and optionally R0 = OUT_PTR).

I/O Device API and Uxntal Wrapper Examples

Status and error model (applies to all commands)
- STATUS (D1): 0x00 = success, 0x01 = error
- On success: R0 (D8–D9) contains the valid 16-bit result if the command defines one
- On error: Prefer R0 (D8–D9) as a 16-bit error code;
- Devices do not alter the stack; only wrappers read/write stacks

Ports
- CMD: D0 (write)
- STATUS: D1 (read)
- A0: D2–D3 (write, 16-bit)
- A1: D4–D5 (write, 16-bit)
- A2: D6–D7 (write, 16-bit)
- R0: D8–D9 (read, 16-bit)


#### The Device does not mutate or interact with the Uxn stack directly; all stack interaction is done by the Uxntal wrappers.
---
Command: CREATE (0x01)
Device API
- Writes:
  - A0 (D2–D3): ENTRY_PTR (16-bit)
  - A1 (D4–D5): ARG_PTR (16-bit, 0 if none)
  - A2 (D6–D7): FLAGS (16-bit; e.g., bit0=detached)
  - CMD (D0): 0x01
- Reads:
  - R0 (D8–D9): THREAD_ID (16-bit) on success; error code on error
  - STATUS (D1)

Uxntal wrapper
```tal
@THREAD.CREATE  ( entry arg flags -- tid|err status )
    DEO2 Threads/A0
    DEO2 Threads/A1
    DEO2 Threads/A2
    #01   DEO Threads/CMD
    DEI2  Threads/R0
    DEI   Threads/STATUS
```
- Wrapper stack interaction:
  - Pops: 6 bytes (entry 2, arg 2, flags 2)
  - Pushes: 3 bytes (tid-or-err 2, status 1)

---

Command: JOIN (0x02)
Device API
- Writes:
  - A0 (D2–D3): TARGET_TID (16-bit)
  - CMD (D0): 0x02
- Reads:
  - R0 (D8–D9): EXIT_CODE or join result (16-bit) on success; error code on error
  - STATUS (D1)

Uxntal wrapper
```tal
@THREAD.JOIN  ( tid -- exit|err status )
    DEO2 Threads/A0
    #02   DEO Threads/CMD
    DEI2  Threads/R0
    DEI   Threads/STATUS
```
- Wrapper stack interaction:
  - Pops: 2 bytes (tid)
  - Pushes: 3 bytes (exit-or-err 2, status 1)

---

Command: MUTEX_INIT (0x10)
Device API
- Writes:
  - A0 (D2–D3): ATTR_PTR (16-bit, 0 if none)
  - CMD (D0): 0x10
- Reads:
  - R0 (D8–D9): MUTEX_ID (16-bit) on success; error code on error
  - STATUS (D1)

Uxntal wrapper
```tal
@MUTEX.INIT  ( -- mid|err status )
    #0000 DEO2 Threads/A0
    #10   DEO  Threads/CMD
    DEI2  Threads/R0
    DEI   Threads/STATUS
```
- Wrapper stack interaction:
  - Pops: 0 bytes
  - Pushes: 3 bytes (mid-or-err 2, status 1)

---

Command: MUTEX_LOCK (0x11)
Device API
- Writes:
  - A0 (D2–D3): MUTEX_ID (16-bit)
  - CMD (D0): 0x11
- Reads:
  - STATUS (D1)
  - (R0 unused)

Uxntal wrapper
```tal
@MUTEX.LOCK  ( mid -- status )
    DEO2 Threads/A0
    #11   DEO Threads/CMD
    DEI   Threads/STATUS
```
- Wrapper stack interaction:
  - Pops: 2 bytes (mid)
  - Pushes: 1 byte (status)

---

Command: MUTEX_UNLOCK (0x12)
Device API
- Writes:
  - A0 (D2–D3): MUTEX_ID (16-bit)
  - CMD (D0): 0x12
- Reads:
  - STATUS (D1)
  - (R0 unused)

Uxntal wrapper
```tal
@MUTEX.UNLOCK  ( mid -- status )
    DEO2 Threads/A0
    #12   DEO Threads/CMD
    DEI   Threads/STATUS
```
- Wrapper stack interaction:
  - Pops: 2 bytes (mid)
  - Pushes: 1 byte (status)

---

Command: MUTEX_DESTROY (0x13)
Device API
- Writes:
  - A0 (D2–D3): MUTEX_ID (16-bit)
  - CMD (D0): 0x13
- Reads:
  - STATUS (D1)
  - (R0 unused)

Uxntal wrapper
```tal
@MUTEX.DESTROY  ( mid -- status )
    DEO2 Threads/A0
    #13   DEO Threads/CMD
    DEI   Threads/STATUS
```
- Wrapper stack interaction:
  - Pops: 2 bytes (mid)
  - Pushes: 1 byte (status)

Error handling
- All wrappers **push STATUS** onto the stack. Callers **must check STATUS** after each invocation.
- `STATUS` values:
  - `0`: success
  - `1`: error — R0 may contain a 16-bit error code


Currently implemented commands:
- `CMD_CREATE (1)` — Spawn a new thread executing at the address in `PTR`
- `CMD_JOIN (2)` — Wait for a thread specified by `TARGET_THREAD` to finish
- These are not yet implemented to the above specification - fix coming soon!

Below is the readme of the canonical uxn implementation, the build instructions remain the same for this fork.

# Uxn

An assembler and emulator for the [Uxn stack-machine](https://wiki.xxiivv.com/site/uxn.html), written in ANSI C. 

## Build

### Linux/OS X

To build the Uxn emulator, you must install [SDL2](https://wiki.libsdl.org/) for your distro. If you are using a package manager:

```sh
sudo pacman -Sy sdl2             # Arch
sudo apt install libsdl2-dev     # Ubuntu
sudo xbps-install SDL2-devel     # Void Linux
brew install sdl2                # OS X
```

Build the assembler and emulator by running the `build.sh` script. The assembler(`uxnasm`) and emulator(`uxnemu`) are created in the `./bin` folder.

```sh
./build.sh 
	--debug # Add debug flags to compiler
	--format # Format source code
	--install # Copy to ~/bin
```

If you wish to build the emulator without graphics mode:

```sh
cc src/devices/datetime.c src/devices/system.c src/devices/console.c src/devices/file.c src/uxn.c -DNDEBUG -Os -g0 -s src/uxncli.c -o bin/uxncli
```

### Plan 9 

To build and install the Uxn emulator on [9front](http://9front.org/), via [npe](https://git.sr.ht/~ft/npe):

```rc
mk install
```

If the build fails on 9front because of missing headers or functions, try again after `rm -r /sys/include/npe`.

### Windows

Uxn can be built on Windows with [MSYS2](https://www.msys2.org/). Install by downloading from their website or with Chocolatey with `choco install msys2`. In the MSYS shell, type:

```sh
pacman -S git mingw-w64-x86_64-gcc mingw64/mingw-w64-x86_64-SDL2
export PATH="${PATH}:/mingw64/bin"
git clone https://git.sr.ht/~rabbits/uxn
cd uxn
./build.sh
```

If you'd like to work with the Console device in `uxnemu.exe`, run `./build.sh --console` instead: this will bring up an extra window for console I/O unless you run `uxnemu.exe` in Command Prompt or PowerShell.

## Getting Started

### Emulator

To launch a `.rom` in the emulator, point the emulator to the target rom file:

```sh
bin/uxnemu bin/piano.rom
```

You can also use the emulator without graphics by using `uxncli`. You can find additional roms [here](https://sr.ht/~rabbits/uxn/sources), you can find prebuilt rom files [here](https://itch.io/c/248074/uxn-roms). 

### Assembler 

The following command will create an Uxn-compatible rom from an [uxntal file](https://wiki.xxiivv.com/site/uxntal.html). Point the assembler to a `.tal` file, followed by and the rom name:

```sh
bin/uxnasm projects/examples/demos/life.tal bin/life.rom
```

### I/O

You can send events from Uxn to another application, or another instance of uxn, with the Unix pipe. For a companion application that translates notes data into midi, see the [shim](https://git.sr.ht/~rabbits/shim).

```sh
uxnemu orca.rom | shim
```

## GUI Emulator Options

- `-2x` Force medium scale
- `-3x` Force large scale
- `-f`  Force fullscreen mode
- `--`  Force next argument to be read as ROM name. (This is useful if your ROM file is named `-v`, `-2x`, and so forth.)

## GUI Emulator Controls

- `F1` toggle zoom
- `F2` toggle debugger
- `F3` quit
- `F4` reboot
- `F5` reboot(soft)
- `F11` toggle fullscreen
- `F12` toggle decorations

### GUI Buttons

- `LCTRL` A
- `LALT` B
- `LSHIFT` SEL 
- `HOME` START

## Need a hand?

The following resources are a good place to start:

* [XXIIVV — uxntal](https://wiki.xxiivv.com/site/uxntal.html)
* [XXIIVV — uxntal reference](https://wiki.xxiivv.com/site/uxntal_reference.html)
* [compudanzas — uxn tutorial](https://compudanzas.net/uxn_tutorial.html)
* [Fediverse — #uxn tag](https://merveilles.town/tags/uxn)

## Contributing

Submit patches using [`git send-email`](https://git-send-email.io/) to the [~rabbits/public-inbox mailing list](https://lists.sr.ht/~rabbits/public-inbox).
