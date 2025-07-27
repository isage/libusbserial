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

#ifndef __USBSERIAL_H__
#define __USBSERIAL_H__

#include <psp2/types.h>
#include <stdint.h>

/** Parity mode for usbserial_set_line_property() */
enum parity_type
{
  PARITY_NONE  = 0,
  PARITY_ODD   = 1,
  PARITY_EVEN  = 2,
  PARITY_MARK  = 3,
  PARITY_SPACE = 4
};
/** Number of stop bits for usbserial_set_line_property() */
enum stopbits_type
{
  STOP_BIT_1  = 0,
  STOP_BIT_15 = 1,
  STOP_BIT_2  = 2
};
/** Number of bits for usbserial_set_line_property() */
enum bits_type
{
  BITS_7 = 7,
  BITS_8 = 8
};
/** Break type for usbserial_set_line_property2() */
enum break_type
{
  BREAK_OFF = 0,
  BREAK_ON  = 1
};

#ifdef __cplusplus
extern "C"
{
#endif

  int libusbserial_start(void);
  int libusbserial_stop(void);

  int libusbserial_device_connected(void);

  int libusbserial_set_baudrate(int baudrate);
  int libusbserial_set_line_property(enum bits_type bits, enum stopbits_type sbit, enum parity_type parity,
                              enum break_type break_type);

  int libusbserial_write_data(const unsigned char *buf, int size);
  int libusbserial_read_data(unsigned char *buf, int size);
  int libusbserial_read_data_blocking(unsigned char *buf, int size, SceUInt timeout);
  int libusbserial_available_count(void);

  int libusbserial_tciflush(void);
  int libusbserial_tcoflush(void);
  int libusbserial_tcioflush(void);

  /* flow control */
  int libusbserial_setflowctrl(int flowctrl);
  int libusbserial_setflowctrl_xonxoff(unsigned char xon, unsigned char xoff);
  int libusbserial_setdtr_rts(int dtr, int rts);
  int libusbserial_setdtr(int state);
  int libusbserial_setrts(int state);

#ifdef __cplusplus
}
#endif

#endif // __USBSERIAL_H__
