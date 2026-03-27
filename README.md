## Threads Device (Multithreading Extension)

This fork adds a new **Threads device to Varvara**, exposing a minimal subset of the
POSIX threads API. The goal is to enable limited parallel execution in Uxn.

Device port map
Base: 0xD0–0xDA

- D0 `cmd` — command byte (write-only)
- D1 `errno` — status/error byte (read-only): 0=success, non-zero=error
- D2–D3 `a0` — argument 0 (16-bit, write)
- D4–D5 `a1` — argument 1 (16-bit, write)
- D6–D7 `a2` — argument 2 (16-bit, write)
- D8–D9 `r0` — result (16-bit, read)
- DA `USELOCALSTORAGEINDEX` — thread-local storage index (read/write)

Notes:
- a0/a1/a2 are per-command inputs; r0 is the per-command output.
- For >3 inputs or >1 output, use an arg-block convention: a2 = IN_PTR to a RAM struct (and optionally r0 = OUT_PTR).

I/O Device API and Uxntal Wrapper Examples

Status and error model (applies to all commands)
- `errno` (D1): 0x00 = success, non-zero = error
- On success: r0 (D8–D9) contains the valid 16-bit result if the command defines one
- On error: errno contains a command-specific error code (see Error Codes section below)
- The device does not alter the stack; only wrappers read/write stacks

Ports
- cmd: D0 (write)
- errno: D1 (read)
- a0: D2–D3 (write, 16-bit)
- a1: D4–D5 (write, 16-bit)
- a2: D6–D7 (write, 16-bit)
- r0: D8–D9 (read, 16-bit)


#### The Device does not mutate or interact with the Uxn stack directly; all stack interaction is done by the Uxntal wrappers.
---
Command: SELF (0x00)
Device API
- Writes:
  - cmd (D0): 0x00
- Reads:
  - r0 (D8–D9): current THREAD_ID (16-bit) on success
  - errno (D1): 0=success, 1=error

Uxntal wrapper
```tal
@thread-self  ( -- tid|err status )
    #00 .Threads/cmd DEO
    .Threads/r0 DEI2
    .Threads/errno DEI
    JMP2r
```
- Wrapper stack interaction:
  - Pops: 0 bytes
  - Pushes: 3 bytes (tid-or-err 2, status 1)

---

Command: CREATE (0x01)
Device API
- Writes:
  - a0 (D2–D3): ENTRY_PTR (16-bit)
  - a1 (D4–D5): ARG_PTR (16-bit, 0 if none)
  - cmd (D0): 0x01
- Reads:
  - r0 (D8–D9): THREAD_ID (16-bit) on success
  - errno (D1): ThreadCreateError code

Uxntal wrapper
```tal
@thread-create  ( entry arg -- tid|err status )
    .Threads/a1 DEO2
    .Threads/a0 DEO2
    #01 .Threads/cmd DEO
    .Threads/r0 DEI2
    .Threads/errno DEI
    JMP2r
```
- Wrapper stack interaction:
  - Pops: 4 bytes (entry 2, arg 2)
  - Pushes: 3 bytes (tid-or-err 2, status 1)
- errno codes (ThreadCreateError):
  - `0` — OK
  - `1` — SystemResources (EAGAIN)
  - `2` — InvalidAttributes (EINVAL)
  - `3` — PermissionDenied (EPERM)
  - `4` — ATTR_INIT_OUT_OF_MEMORY
  - `5` — ThreadLimitReached (all 16 slots in use)

---

Command: JOIN (0x02)
Device API
- Writes:
  - a0 (D2–D3): TARGET_TID (16-bit)
  - cmd (D0): 0x02
- Reads:
  - r0 (D8–D9): EXIT_CODE or join result (16-bit) on success
  - errno (D1): ThreadJoinError code

Uxntal wrapper
```tal
@thread-join  ( tid -- exit|err status )
    .Threads/a0 DEO2
    #02 .Threads/cmd DEO
    .Threads/r0 DEI2
    .Threads/errno DEI
    JMP2r
```
- Wrapper stack interaction:
  - Pops: 2 bytes (tid)
  - Pushes: 3 bytes (exit-or-err 2, status 1)
- errno codes (ThreadJoinError):
  - `0` — OK
  - `1` — Deadlock (EDEADLK)
  - `2` — NotJoinable (EINVAL)
  - `3` — NotFound (ESRCH)

---

Command: MUTEX_CREATE (0x03)
Device API
- Writes:
  - a0 (D2–D3): ATTR_PTR (16-bit, 0 if none)
  - cmd (D0): 0x03
- Reads:
  - r0 (D8–D9): MUTEX_ID (16-bit) on success
  - errno (D1): MutexInitError code

Uxntal wrapper
```tal
@mutex-create  ( -- mid|err status )
    #0000 .Threads/a0 DEO2
    #03 .Threads/cmd DEO
    .Threads/r0 DEI2
    .Threads/errno DEI
    JMP2r
```
- Wrapper stack interaction:
  - Pops: 0 bytes
  - Pushes: 3 bytes (mid-or-err 2, status 1)
- errno codes (MutexInitError):
  - `0` — OK
  - `1` — InvalidAttributes
  - `2` — SystemResources
  - `3` — PermissionDenied

---

Command: MUTEX_DESTROY (0x04)
Device API
- Writes:
  - a0 (D2–D3): MUTEX_ID (16-bit)
  - cmd (D0): 0x04
- Reads:
  - errno (D1): 0=success, 1=error
  - (r0 unused)

Uxntal wrapper
```tal
@mutex-destroy  ( mid -- status )
    .Threads/a0 DEO2
    #04 .Threads/cmd DEO
    .Threads/errno DEI
    JMP2r
```
- Wrapper stack interaction:
  - Pops: 2 bytes (mid)
  - Pushes: 1 byte (status)
- errno: `0`=success, `1`=error

---

Command: MUTEX_LOCK (0x05)
Device API
- Writes:
  - a0 (D2–D3): MUTEX_ID (16-bit)
  - cmd (D0): 0x05
- Reads:
  - errno (D1): 0=success, 1=error
  - (r0 unused)

Uxntal wrapper
```tal
@mutex-lock  ( mid -- status )
    .Threads/a0 DEO2
    #05 .Threads/cmd DEO
    .Threads/errno DEI
    JMP2r
```
- Wrapper stack interaction:
  - Pops: 2 bytes (mid)
  - Pushes: 1 byte (status)
- errno: `0`=success, `1`=error

---

Command: MUTEX_UNLOCK (0x06)
Device API
- Writes:
  - a0 (D2–D3): MUTEX_ID (16-bit)
  - cmd (D0): 0x06
- Reads:
  - errno (D1): 0=success, 1=error
  - (r0 unused)

Uxntal wrapper
```tal
@mutex-unlock  ( mid -- status )
    .Threads/a0 DEO2
    #06 .Threads/cmd DEO
    .Threads/errno DEI
    JMP2r
```
- Wrapper stack interaction:
  - Pops: 2 bytes (mid)
  - Pushes: 1 byte (status)
- errno: `0`=success, `1`=error

---

Command: MUTEX_TRYLOCK (0x07)
Device API
- Writes:
  - a0 (D2–D3): MUTEX_ID (16-bit)
  - cmd (D0): 0x07
- Reads:
  - r0 (D8–D9): 1 if acquired, 0 if not acquired
  - errno (D1): 0=success, 1=error

Uxntal wrapper
```tal
@mutex-trylock  ( mid -- status )
    .Threads/a0 DEO2
    #07 .Threads/cmd DEO
    .Threads/errno DEI
    JMP2r
```
- Wrapper stack interaction:
  - Pops: 2 bytes (mid)
  - Pushes: 1 byte (status)
- errno: `0`=success, `1`=error

---

Command: COND_CREATE (0x08)
Device API
- Writes:
  - cmd (D0): 0x08
- Reads:
  - r0 (D8–D9): COND_ID (16-bit) on success
  - errno (D1): CondInitError code

Uxntal wrapper
```tal
@cond-create  ( -- cid|err status )
    #08 .Threads/cmd DEO
    .Threads/r0 DEI2
    .Threads/errno DEI
    JMP2r
```
- Wrapper stack interaction:
  - Pops: 0 bytes
  - Pushes: 3 bytes (cid-or-err 2, status 1)
- errno codes (CondInitError):
  - `0` — OK
  - `1` — InvalidAttributes
  - `2` — SystemResources
  - `3` — PermissionDenied

---

Command: COND_DESTROY (0x09)
Device API
- Writes:
  - a0 (D2–D3): COND_ID (16-bit)
  - cmd (D0): 0x09
- Reads:
  - errno (D1): 0=success, 1=error
  - (r0 unused)

Uxntal wrapper
```tal
@cond-destroy  ( cid -- status )
    .Threads/a0 DEO2
    #09 .Threads/cmd DEO
    .Threads/errno DEI
    JMP2r
```
- Wrapper stack interaction:
  - Pops: 2 bytes (cid)
  - Pushes: 1 byte (status)
- errno: `0`=success, `1`=error

---

Command: COND_WAIT (0x0A)
Device API
- Writes:
  - a0 (D2–D3): COND_ID (16-bit)
  - a1 (D4–D5): MUTEX_ID (16-bit) — mutex must be locked by caller
  - cmd (D0): 0x0A
- Reads:
  - errno (D1): 0=success, 1=error
  - (r0 unused)

Uxntal wrapper
```tal
@cond-wait  ( cid mid -- status )
    .Threads/a1 DEO2
    .Threads/a0 DEO2
    #0a .Threads/cmd DEO
    .Threads/errno DEI
    JMP2r
```
- Wrapper stack interaction:
  - Pops: 4 bytes (cid 2, mid 2)
  - Pushes: 1 byte (status)
- errno: `0`=success, `1`=error
- Note: The mutex is atomically released while waiting and re-acquired before returning.

---

Command: COND_SIGNAL (0x0B)
Device API
- Writes:
  - a0 (D2–D3): COND_ID (16-bit)
  - cmd (D0): 0x0B
- Reads:
  - errno (D1): 0=success, 1=error
  - (r0 unused)

Uxntal wrapper
```tal
@cond-signal  ( cid -- status )
    .Threads/a0 DEO2
    #0b .Threads/cmd DEO
    .Threads/errno DEI
    JMP2r
```
- Wrapper stack interaction:
  - Pops: 2 bytes (cid)
  - Pushes: 1 byte (status)
- errno: `0`=success, `1`=error
- Note: Wakes up one thread waiting on the condition variable.

---

Command: COND_BROADCAST (0x0C)
Device API
- Writes:
  - a0 (D2–D3): COND_ID (16-bit)
  - cmd (D0): 0x0C
- Reads:
  - errno (D1): 0=success, 1=error
  - (r0 unused)

Uxntal wrapper
```tal
@cond-broadcast  ( cid -- status )
    .Threads/a0 DEO2
    #0c .Threads/cmd DEO
    .Threads/errno DEI
    JMP2r
```
- Wrapper stack interaction:
  - Pops: 2 bytes (cid)
  - Pushes: 1 byte (status)
- errno: `0`=success, `1`=error
- Note: Wakes up all threads waiting on the condition variable.

---

Command: COND_TIMEDWAIT (0x0D)
Device API
- Writes:
  - a0 (D2–D3): COND_ID (16-bit)
  - a1 (D4–D5): MUTEX_ID (16-bit) — mutex must be locked by caller
  - a2 (D6–D7): TIMEOUT_MS (16-bit) — timeout in milliseconds (0–65535)
  - cmd (D0): 0x0D
- Reads:
  - r0 (D8–D9): CondTimedWaitResult code (0=signalled, 1=timed out, 2=invalid args, 3=clock failure)
  - errno (D1): 0=success (for both signal and timeout), 1=error

Uxntal wrapper
```tal
@cond-timedwait  ( cid mid timeout_ms -- result status )
    .Threads/a2 DEO2
    .Threads/a1 DEO2
    .Threads/a0 DEO2
    #0d .Threads/cmd DEO
    .Threads/r0 DEI2
    .Threads/errno DEI
    JMP2r
```
- Wrapper stack interaction:
  - Pops: 6 bytes (cid 2, mid 2, timeout 2)
  - Pushes: 3 bytes (result 2, status 1)
- errno: `0`=success (for both signal and timeout), `1`=error
- r0 result codes (CondTimedWaitResult):
  - `0` — OK (condition was signalled)
  - `1` — Timeout (wait timed out)
  - `2` — InvalidArgs (bad cond/mutex handle)
  - `3` — ClockFail (failed to read system clock)
- Note: Like `pthread_cond_timedwait` but takes a relative timeout in milliseconds rather than an absolute timestamp, since Uxn is 16-bit. The mutex is atomically released while waiting and re-acquired before returning. errno=0 on both signal and timeout; check r0 to distinguish.

---

Command: BARRIER_CREATE (0x0E)
Device API
- Writes:
  - a0 (D2–D3): COUNT (16-bit) — number of threads that must call barrier-wait before any are released; must be >0
  - cmd (D0): 0x0E
- Reads:
  - r0 (D8–D9): BARRIER_ID (16-bit) on success
  - errno (D1): BarrierInitError code

Uxntal wrapper
```tal
@barrier-create  ( count -- bid|err status )
    .Threads/a0 DEO2
    #0e .Threads/cmd DEO
    .Threads/r0 DEI2
    .Threads/errno DEI
    JMP2r
```
- Wrapper stack interaction:
  - Pops: 2 bytes (count)
  - Pushes: 3 bytes (bid-or-err 2, status 1)
- errno codes (BarrierInitError):
  - `0` — OK
  - `1` — InvalidAttributes (e.g. count=0)
  - `2` — SystemResources
  - `3` — PermissionDenied

---

Command: BARRIER_DESTROY (0x0F)
Device API
- Writes:
  - a0 (D2–D3): BARRIER_ID (16-bit)
  - cmd (D0): 0x0F
- Reads:
  - errno (D1): 0=success, 1=error
  - (r0 unused)

Uxntal wrapper
```tal
@barrier-destroy  ( bid -- status )
    .Threads/a0 DEO2
    #0f .Threads/cmd DEO
    .Threads/errno DEI
    JMP2r
```
- Wrapper stack interaction:
  - Pops: 2 bytes (bid)
  - Pushes: 1 byte (status)
- errno: `0`=success, `1`=error

---

Command: BARRIER_WAIT (0x10)
Device API
- Writes:
  - a0 (D2–D3): BARRIER_ID (16-bit)
  - cmd (D0): 0x10
- Reads:
  - r0 (D8–D9): 1 if this thread is the "serial thread" (exactly one per barrier release), 0 otherwise
  - errno (D1): 0=success, 1=error

Uxntal wrapper
```tal
@barrier-wait  ( bid -- serial? status )
    .Threads/a0 DEO2
    #10 .Threads/cmd DEO
    .Threads/r0 DEI2
    .Threads/errno DEI
    JMP2r
```
- Wrapper stack interaction:
  - Pops: 2 bytes (bid)
  - Pushes: 3 bytes (serial? 2, status 1)
- errno: `0`=success, `1`=error
- Note: Blocks until all threads reach the barrier. Exactly one thread receives serial?=1 (the "serial thread"); all others receive 0.

---

Error handling
- All wrappers **push errno** onto the stack. Callers **must check errno** after each invocation.
- `errno` = `0` means success; non-zero means error.
- Commands with specific error enums document them inline above. All other commands use generic `0`=success, `1`=error.

---

Currently implemented commands:
- `CMD_SELF (0x00)` — Return current thread ID in r0
- `CMD_CREATE (0x01)` — Spawn a new thread executing at the address in a0
- `CMD_JOIN (0x02)` — Wait for a thread specified by a0 to finish
- `CMD_MUTEX_CREATE (0x03)` — Create a new mutex, returns handle in r0
- `CMD_MUTEX_DESTROY (0x04)` — Destroy a mutex given handle in a0
- `CMD_MUTEX_LOCK (0x05)` — Lock a mutex given handle in a0
- `CMD_MUTEX_UNLOCK (0x06)` — Unlock a mutex given handle in a0
- `CMD_MUTEX_TRYLOCK (0x07)` — Try to lock a mutex given handle in a0, r0=1 if acquired else 0
- `CMD_COND_CREATE (0x08)` — Create a condition variable, returns handle in r0
- `CMD_COND_DESTROY (0x09)` — Destroy a condition variable given handle in a0
- `CMD_COND_WAIT (0x0A)` — Wait on a condition variable (a0=cond, a1=mutex)
- `CMD_COND_SIGNAL (0x0B)` — Signal one thread waiting on condition variable in a0
- `CMD_COND_BROADCAST (0x0C)` — Signal all threads waiting on condition variable in a0
- `CMD_COND_TIMEDWAIT (0x0D)` — Timed wait on a condition variable (a0=cond, a1=mutex, a2=timeout_ms)
- `CMD_BARRIER_CREATE (0x0E)` — Create a barrier for a0 threads, returns handle in r0
- `CMD_BARRIER_DESTROY (0x0F)` — Destroy a barrier given handle in a0
- `CMD_BARRIER_WAIT (0x10)` — Wait on a barrier given handle in a0, r0=1 if serial thread else 0

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
