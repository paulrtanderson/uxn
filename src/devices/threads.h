#ifndef DEVICES_THREADS_H
#define DEVICES_THREADS_H

void threads_deo(Uint8 addr);

/* exported so worker threads can boot new VMs from the same ROM */
extern const char *g_rom_path;
#endif
