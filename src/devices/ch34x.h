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

#ifndef __CH34X_H__
#define __CH34X_H__

#include "../libusbserial_private.h"
#include "../libusbserial.h"
#include "../serialdevice.h"

#include <psp2/types.h>
#include <stdint.h>

#define CH34X_BIT_RTS (1 << 6)
#define CH34X_BIT_DTR (1 << 5)
#define CH34X_MULT_STAT 0x04
#define CH34X_BIT_CTS 0x01
#define CH34X_BIT_DSR 0x02
#define CH34X_BIT_RI  0x04
#define CH34X_BIT_DCD 0x08
#define CH34X_BITS_MODEM_STAT 0x0f /* all bits */

#define CH34X_REQ_READ_VERSION 0x5F
#define CH34X_REQ_WRITE_REG    0x9A
#define CH34X_REQ_READ_REG     0x95
#define CH34X_REQ_SERIAL_INIT  0xA1
#define CH34X_REQ_MODEM_CTRL   0xA4

#define CH34X_REG_BREAK        0x05
#define CH34X_REG_PRESCALER    0x12
#define CH34X_REG_DIVISOR      0x13
#define CH34X_REG_LCR          0x18
#define CH34X_REG_LCR2         0x25
#define CH34X_REG_FLOW_CTL     0x27

#define CH34X_NBREAK_BITS      0x01

#define CH34X_LCR_ENABLE_RX    0x80
#define CH34X_LCR_ENABLE_TX    0x40
#define CH34X_LCR_MARK_SPACE   0x20
#define CH34X_LCR_PAR_EVEN     0x10
#define CH34X_LCR_ENABLE_PAR   0x08
#define CH34X_LCR_STOP_BITS_2  0x04
#define CH34X_LCR_CS8          0x03
#define CH34X_LCR_CS7          0x02
#define CH34X_LCR_CS6          0x01
#define CH34X_LCR_CS5          0x00

#define CH34X_FLOW_CTL_NONE    0x00
#define CH34X_FLOW_CTL_RTSCTS  0x01

#define CH34X_QUIRK_LIMITED_PRESCALER   BIT(0)
#define CH34X_QUIRK_SIMULATE_BREAK  BIT(1)


unsigned int _ch34x_determine_max_packet_size(serialDevice* ctx);
int _ch34x_reset(serialDevice* ctx);
int _ch34x_set_baudrate(serialDevice* ctx, int baudrate);
int _ch34x_set_line_property(serialDevice* ctx, enum bits_type bits, enum stopbits_type sbit, enum parity_type parity, enum break_type break_type);
int _ch34x_tciflush();
int _ch34x_tcoflush();
int _ch34x_setflowctrl(serialDevice* ctx, int flowctrl);
int _ch34x_setflowctrl_xonxoff(serialDevice* ctx, unsigned char xon, unsigned char xoff);
int _ch34x_setdtr_rts(serialDevice* ctx, int dtr, int rts);
int _ch34x_setdtr(serialDevice* ctx, int dtrstate);
int _ch34x_setrts(serialDevice* ctx, int rtsstate);


#endif // __FTDI_H__
