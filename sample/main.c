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

#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libusbserial.h>

int main(int argc, char *argv[])
{
    unsigned char buf;
    libusbserial_start();

    sceClibPrintf("waiting for device\n");
    while(!libusbserial_device_connected())
    {
        sceKernelDelayThread(1000);
    }

    sceClibPrintf("device found\n");
    int f = libusbserial_set_baudrate(115200);
    if (f < 0)
    {
        sceClibPrintf("unable to set baudrate\n");
        exit(-1);
    }

    f = libusbserial_set_line_property(8, STOP_BIT_1, PARITY_NONE, BREAK_OFF);
    if (f < 0)
    {
        sceClibPrintf("unable to set line parameters\n");
        exit(-1);
    }

    sceClibPrintf("set line parameters, waiting read\n");


    char *hello = "hello";

    f = libusbserial_write_data(hello, 6);
    if (f < 0)
    {
        sceClibPrintf("unable to write\n");
        exit(-1);
    }

    while(1)
    {
        f = libusbserial_available_count();
        if (f > 0)
            sceClibPrintf("Available: %d\n", f);

        f = libusbserial_read_data(&buf, sizeof(buf));

        if(f > 0)
        {
            sceClibPrintf("%c\n", buf);
        }
        else
        {
            sceKernelDelayThread(300000);
        }
    }

    libusbserial_stop();

    return 0;
}
