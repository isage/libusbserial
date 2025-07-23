/*
        libusbserial
        Copyright (C) 2025 Cat (Ivan Epifanov)

        This program is free software: you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation, either version 3 of the License, or
        (at your option) any later version.

        This program is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef __LIBUSBSERIAL_PRIVATE_H__
#define __LIBUSBSERIAL_PRIVATE_H__

#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/debug.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <stdint.h>

#ifndef NDEBUG
#define _error_return(code, str)                                                                                       \
  do                                                                                                                   \
  {                                                                                                                    \
    ksceDebugPrintf("%s\n", str);                                                                                      \
    EXIT_SYSCALL(state);                                                                                               \
    return code;                                                                                                       \
  } while (0);

#define trace(...) ksceKernelDelayThread(5000);ksceDebugPrintf(__VA_ARGS__)

#else
#define _error_return(code, str)                                                                                       \
  do                                                                                                                   \
  {                                                                                                                    \
    EXIT_SYSCALL(state);                                                                                               \
    return code;                                                                                                       \
  } while (0);

#define trace(...)

#endif

#define BIT(nr) (1UL << (nr))
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

void _callback_control(int32_t result, int32_t count, void *arg);
void _callback_send(int32_t result, int32_t count, void *arg);
void _callback_recv(int32_t result, int32_t count, void *arg);
int _control_transfer(int rtype, int req, int val, int idx, void *data, int len);

#endif // __LIBUSBSERIAL_PRIVATE_H__