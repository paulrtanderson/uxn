## Threads Device (Multithreading Extension)

This fork adds a new **Threads device to Varvara**, exposing a minimal subset of the
POSIX threads API. The goal is to enable limited parallel execution in Uxn.

Currently implemented commands:
- `CMD_CREATE (1)` — Spawn a new thread executing at the address in `PTR`
- `CMD_JOIN (2)` — Wait for a thread specified by `TARGET_THREAD` to finish

`CMD_DETACH (3)` is reserved for future support.

### Device Map
The device occupies addresses **0xd0–0xdf** (16 bytes total).

| Address | Name | Description |
|----------|------|-------------|
| `d0` | `CMD` | Command register (`1=Create`, `2=Join`, `3=Detach`) |
| `d1` | `STATUS` | `0=Idle`, `2=OK`, `3=Error` |
| `d2–d3` | `PTR` | 16‑bit entry pointer |
| `d4` | `ERRNO` | Error code |
| `d5–d7` | — | Reserved |
| `d8–d9` | `RESULT` | 16‑bit result or return value |
| `dA–dB` | `OUT_THREAD` | ID of the newly created thread |
| `dC–dD` | — | Reserved |
| `dE–dF` | `TARGET_THREAD` | ID of the thread to target for next command |

Example uxntal threads program:
```tal

|10 @Console                     ( Console device mapped at 0x10 )
  &vector $2 &read $1 &pad $4
  &type $1 &write $1 &error $1

|d0 @Threads                     ( Threads device mapped at 0xd0 )
  &cmd $1 &status $1 &ptr $2 &errno $1 &pad $3
  &result $2 &outtid $2 &pad3 $2 &targettid $2

|0100                            ( Reset vector )

@on-reset
    ;worker  .Threads/ptr DEO2   ( set entry pointer )
    #01      .Threads/cmd DEO    ( CMD_CREATE — start worker thread )
    .Threads/outtid DEI2         ( read thread id )
    .Threads/targettid DEO2      ( set target thread id )
    #02      .Threads/cmd DEO    ( CMD_JOIN — wait for completion )

    .Threads/result DEI2         ( read 16‑bit result = 42 )
    .Console/write DEO2          ( write result to console → prints '*' )
    BRK

@worker
    #06 #07 MUL                  ( compute 6×7 = 42 )
    .Threads/result DEO2         ( store result in device result field )
    BRK
```

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
