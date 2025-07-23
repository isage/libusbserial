/*
        libusBserial
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

#ifndef __FTDI_H__
#define __FTDI_H__

#include "../libusbserial.h"
#include "../serialdevice.h"
#include "ftdi_chips.h"

#include <psp2/types.h>
#include <stdint.h>

#define FTDI_DEVICE_OUT_REQTYPE ((0x02 << 5) | 0x00)
#define FTDI_DEVICE_IN_REQTYPE ((0x02 << 5) | 0x80)

#define SIO_RESET 0         /* Reset the port */
#define SIO_MODEM_CTRL 1    /* Set the modem control register */
#define SIO_SET_FLOW_CTRL 2 /* Set flow control register */
#define SIO_SET_BAUD_RATE 3 /* Set baud rate */
#define SIO_SET_DATA 4      /* Set the data characteristics of the port */

/* Requests */
#define SIO_RESET_REQUEST SIO_RESET
#define SIO_SET_BAUDRATE_REQUEST SIO_SET_BAUD_RATE
#define SIO_SET_DATA_REQUEST SIO_SET_DATA
#define SIO_SET_FLOW_CTRL_REQUEST SIO_SET_FLOW_CTRL
#define SIO_SET_MODEM_CTRL_REQUEST SIO_MODEM_CTRL
#define SIO_POLL_MODEM_STATUS_REQUEST 0x05
#define SIO_SET_EVENT_CHAR_REQUEST 0x06
#define SIO_SET_ERROR_CHAR_REQUEST 0x07
#define SIO_SET_LATENCY_TIMER_REQUEST 0x09
#define SIO_GET_LATENCY_TIMER_REQUEST 0x0A
#define SIO_SET_BITMODE_REQUEST 0x0B
#define SIO_READ_PINS_REQUEST 0x0C
#define SIO_READ_EEPROM_REQUEST 0x90
#define SIO_WRITE_EEPROM_REQUEST 0x91
#define SIO_ERASE_EEPROM_REQUEST 0x92

#define SIO_RESET_SIO 0

#define SIO_TCIFLUSH 2
#define SIO_TCOFLUSH 1

#define SIO_DISABLE_FLOW_CTRL 0x0
#define SIO_RTS_CTS_HS (0x1 << 8)
#define SIO_DTR_DSR_HS (0x2 << 8)
#define SIO_XON_XOFF_HS (0x4 << 8)

#define SIO_SET_DTR_MASK 0x1
#define SIO_SET_DTR_HIGH (1 | (SIO_SET_DTR_MASK << 8))
#define SIO_SET_DTR_LOW (0 | (SIO_SET_DTR_MASK << 8))
#define SIO_SET_RTS_MASK 0x2
#define SIO_SET_RTS_HIGH (2 | (SIO_SET_RTS_MASK << 8))
#define SIO_SET_RTS_LOW (0 | (SIO_SET_RTS_MASK << 8))

#define SIO_RTS_CTS_HS (0x1 << 8)

unsigned int _ftdi_determine_max_packet_size(serialDevice* ctx);
int _ftdi_reset();
int _ftdi_set_baudrate(serialDevice* ctx, int baudrate);
int _ftdi_set_line_property(enum bits_type bits, enum stopbits_type sbit, enum parity_type parity, enum break_type break_type);
int _ftdi_tciflush();
int _ftdi_tcoflush();
int _ftdi_setflowctrl(int flowctrl);
int _ftdi_setflowctrl_xonxoff(unsigned char xon, unsigned char xoff);
int _ftdi_setdtr_rts(int dtr, int rts);
int _ftdi_setdtr(int dtrstate);
int _ftdi_setrts(int rtsstate);


#endif // __FTDI_H__
