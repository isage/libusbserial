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

#include "../libusbserial.h"
#include "../libusbserial_private.h"
#include "../serialdevice.h"
#include "ftdi.h"

#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/debug.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/suspend.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/kernel/sysmem/data_transfers.h>
#include <psp2kern/kernel/threadmgr/event_flags.h>
#include <psp2kern/usbd.h>
#include <psp2kern/usbserv.h>
#include <string.h>

int _ftdi_reset()
{
  _control_transfer(FTDI_DEVICE_OUT_REQTYPE, SIO_RESET_REQUEST, SIO_RESET_SIO, 0, NULL, 0);
  return 0;
}

unsigned int _ftdi_determine_max_packet_size(serialDevice* ctx)
{
  unsigned int packet_size;

  // Determine maximum packet size.
  // New hi-speed devices from FTDI use a packet size of 512 bytes
  if (ctx->ftdi_type == TYPE_2232H || ctx->ftdi_type == TYPE_4232H || ctx->ftdi_type == TYPE_232H)
    packet_size = 512;
  else
    packet_size = 64;

  return packet_size;
}

/*  ftdi_to_clkbits_AM For the AM device, convert a requested baudrate
                    to encoded divisor and the achievable baudrate
    Function is only used internally
    \internal

    See AN120
   clk/1   -> 0
   clk/1.5 -> 1
   clk/2   -> 2
   From /2, 0.125/ 0.25 and 0.5 steps may be taken
   The fractional part has frac_code encoding
*/
static int _ftdi_to_clkbits_AM(int baudrate, unsigned long *encoded_divisor)

{
  static const char frac_code[8]    = {0, 3, 2, 4, 1, 5, 6, 7};
  static const char am_adjust_up[8] = {0, 0, 0, 1, 0, 3, 2, 1};
  static const char am_adjust_dn[8] = {0, 0, 0, 1, 0, 1, 2, 3};
  int divisor, best_divisor, best_baud, best_baud_diff;
  int i;
  divisor = 24000000 / baudrate;

  // Round down to supported fraction (AM only)
  divisor -= am_adjust_dn[divisor & 7];

  // Try this divisor and the one above it (because division rounds down)
  best_divisor   = 0;
  best_baud      = 0;
  best_baud_diff = 0;
  for (i = 0; i < 2; i++)
  {
    int try_divisor = divisor + i;
    int baud_estimate;
    int baud_diff;

    // Round up to supported divisor value
    if (try_divisor <= 8)
    {
      // Round up to minimum supported divisor
      try_divisor = 8;
    }
    else if (divisor < 16)
    {
      // AM doesn't support divisors 9 through 15 inclusive
      try_divisor = 16;
    }
    else
    {
      // Round up to supported fraction (AM only)
      try_divisor += am_adjust_up[try_divisor & 7];
      if (try_divisor > 0x1FFF8)
      {
        // Round down to maximum supported divisor value (for AM)
        try_divisor = 0x1FFF8;
      }
    }
    // Get estimated baud rate (to nearest integer)
    baud_estimate = (24000000 + (try_divisor / 2)) / try_divisor;
    // Get absolute difference from requested baud rate
    if (baud_estimate < baudrate)
    {
      baud_diff = baudrate - baud_estimate;
    }
    else
    {
      baud_diff = baud_estimate - baudrate;
    }
    if (i == 0 || baud_diff < best_baud_diff)
    {
      // Closest to requested baud rate so far
      best_divisor   = try_divisor;
      best_baud      = baud_estimate;
      best_baud_diff = baud_diff;
      if (baud_diff == 0)
      {
        // Spot on! No point trying
        break;
      }
    }
  }
  // Encode the best divisor value
  *encoded_divisor = (best_divisor >> 3) | (frac_code[best_divisor & 7] << 14);
  // Deal with special cases for encoded value
  if (*encoded_divisor == 1)
  {
    *encoded_divisor = 0; // 3000000 baud
  }
  else if (*encoded_divisor == 0x4001)
  {
    *encoded_divisor = 1; // 2000000 baud (BM only)
  }
  return best_baud;
}

/*  ftdi_to_clkbits Convert a requested baudrate for a given system clock  and predivisor
                    to encoded divisor and the achievable baudrate
    Function is only used internally
    \internal

    See AN120
   clk/1   -> 0
   clk/1.5 -> 1
   clk/2   -> 2
   From /2, 0.125 steps may be taken.
   The fractional part has frac_code encoding

   value[13:0] of value is the divisor
   index[9] mean 12 MHz Base(120 MHz/10) rate versus 3 MHz (48 MHz/16) else

   H Type have all features above with
   {index[8],value[15:14]} is the encoded subdivisor

   FT232R, FT2232 and FT232BM have no option for 12 MHz and with
   {index[0],value[15:14]} is the encoded subdivisor

   AM Type chips have only four fractional subdivisors at value[15:14]
   for subdivisors 0, 0.5, 0.25, 0.125
*/
static int _ftdi_to_clkbits(int baudrate, int clk, int clk_div, unsigned long *encoded_divisor)
{
  static const char frac_code[8] = {0, 3, 2, 4, 1, 5, 6, 7};
  int best_baud                  = 0;
  int divisor, best_divisor;
  if (baudrate >= clk / clk_div)
  {
    *encoded_divisor = 0;
    best_baud        = clk / clk_div;
  }
  else if (baudrate >= clk / (clk_div + clk_div / 2))
  {
    *encoded_divisor = 1;
    best_baud        = clk / (clk_div + clk_div / 2);
  }
  else if (baudrate >= clk / (2 * clk_div))
  {
    *encoded_divisor = 2;
    best_baud        = clk / (2 * clk_div);
  }
  else
  {
    /* We divide by 16 to have 3 fractional bits and one bit for rounding */
    divisor = clk * 16 / clk_div / baudrate;
    if (divisor & 1) /* Decide if to round up or down*/
      best_divisor = divisor / 2 + 1;
    else
      best_divisor = divisor / 2;
    if (best_divisor > 0x20000)
      best_divisor = 0x1ffff;
    best_baud = clk * 16 / clk_div / best_divisor;
    if (best_baud & 1) /* Decide if to round up or down*/
      best_baud = best_baud / 2 + 1;
    else
      best_baud = best_baud / 2;
    *encoded_divisor = (best_divisor >> 3) | (frac_code[best_divisor & 0x7] << 14);
  }
  return best_baud;
}
/**
    ftdi_convert_baudrate returns nearest supported baud rate to that requested.
    Function is only used internally
    \internal
*/
static int _ftdi_convert_baudrate(serialDevice* ctx, int baudrate, unsigned short *value, unsigned short *index)
{
  int best_baud;
  unsigned long encoded_divisor;

  if (baudrate <= 0)
  {
    // Return error
    return -1;
  }

#define H_CLK 120000000
#define C_CLK 48000000
  if ((ctx->ftdi_type == TYPE_2232H) || (ctx->ftdi_type == TYPE_4232H) || (ctx->ftdi_type == TYPE_232H))
  {
    if (baudrate * 10 > H_CLK / 0x3fff)
    {
      /* On H Devices, use 12 000 000 Baudrate when possible
         We have a 14 bit divisor, a 1 bit divisor switch (10 or 16)
         three fractional bits and a 120 MHz clock
         Assume AN_120 "Sub-integer divisors between 0 and 2 are not allowed" holds for
         DIV/10 CLK too, so /1, /1.5 and /2 can be handled the same*/
      best_baud = _ftdi_to_clkbits(baudrate, H_CLK, 10, &encoded_divisor);
      encoded_divisor |= 0x20000; /* switch on CLK/10*/
    }
    else
      best_baud = _ftdi_to_clkbits(baudrate, C_CLK, 16, &encoded_divisor);
  }
  else if ((ctx->ftdi_type == TYPE_BM) || (ctx->ftdi_type == TYPE_2232C) || (ctx->ftdi_type == TYPE_R) || (ctx->ftdi_type == TYPE_230X))
  {
    best_baud = _ftdi_to_clkbits(baudrate, C_CLK, 16, &encoded_divisor);
  }
  else
  {
    best_baud = _ftdi_to_clkbits_AM(baudrate, &encoded_divisor);
  }
  // Split into "value" and "index" values
  *value = (unsigned short)(encoded_divisor & 0xFFFF);
  if (ctx->ftdi_type == TYPE_2232H || ctx->ftdi_type == TYPE_4232H || ctx->ftdi_type == TYPE_232H)
  {
    *index = (unsigned short)(encoded_divisor >> 8);
    *index &= 0xFF00;
    *index |= 0;
  }
  else
    *index = (unsigned short)(encoded_divisor >> 16);

  // Return the nearest baud rate
  return best_baud;
}

int _ftdi_set_baudrate(serialDevice* ctx, int baudrate)
{
  unsigned short value, index;
  int actual_baudrate;

  trace("setting baudrate %d\n", baudrate);

  actual_baudrate = _ftdi_convert_baudrate(ctx, baudrate, &value, &index);

  if (actual_baudrate <= 0)
  {
    return -1;
  }

  // Check within tolerance (about 5%)
  if ((actual_baudrate * 2 < baudrate /* Catch overflows */)
      || ((actual_baudrate < baudrate) ? (actual_baudrate * 21 < baudrate * 20)
                                       : (baudrate * 21 < actual_baudrate * 20)))
  {
    return -1;
  }

  if (_control_transfer(FTDI_DEVICE_OUT_REQTYPE, SIO_SET_BAUDRATE_REQUEST, value, index, NULL, 0) < 0)
  {
    return -2;
  }

  ctx->baudrate = baudrate;
  return 0;
}


int _ftdi_set_line_property(enum bits_type bits, enum stopbits_type sbit, enum parity_type parity,
                            enum break_type break_type)
{
  unsigned short value = bits;

  switch (parity)
  {
    case PARITY_NONE:
      value |= (0x00 << 8);
      break;
    case PARITY_ODD:
      value |= (0x01 << 8);
      break;
    case PARITY_EVEN:
      value |= (0x02 << 8);
      break;
    case PARITY_MARK:
      value |= (0x03 << 8);
      break;
    case PARITY_SPACE:
      value |= (0x04 << 8);
      break;
  }

  switch (sbit)
  {
    case STOP_BIT_1:
      value |= (0x00 << 11);
      break;
    case STOP_BIT_15:
      value |= (0x01 << 11);
      break;
    case STOP_BIT_2:
      value |= (0x02 << 11);
      break;
  }

  switch (break_type)
  {
    case BREAK_OFF:
      value |= (0x00 << 14);
      break;
    case BREAK_ON:
      value |= (0x01 << 14);
      break;
  }

  if (_control_transfer(FTDI_DEVICE_OUT_REQTYPE, SIO_SET_DATA_REQUEST, value, 0, NULL, 0) < 0)
  {
    return -1;
  }

  return 0;
}


int _ftdi_tciflush()
{
  if (_control_transfer(FTDI_DEVICE_OUT_REQTYPE, SIO_RESET_REQUEST, SIO_TCIFLUSH, 0, NULL, 0) < 0)
    return -1;

  return 0;
}


int _ftdi_tcoflush()
{
  if (_control_transfer(FTDI_DEVICE_OUT_REQTYPE, SIO_RESET_REQUEST, SIO_TCOFLUSH, 0, NULL, 0) < 0)
    return -1;

  return 0;
}


int _ftdi_setflowctrl(int flowctrl)
{

  if (_control_transfer(FTDI_DEVICE_OUT_REQTYPE, SIO_SET_FLOW_CTRL_REQUEST, 0, (flowctrl | 0), NULL, 0) < 0)
    return -1;

  return 0;
}

int _ftdi_setflowctrl_xonxoff(unsigned char xon, unsigned char xoff)
{
  uint16_t xonxoff = xon | (xoff << 8);
  if (_control_transfer(FTDI_DEVICE_OUT_REQTYPE, SIO_SET_FLOW_CTRL_REQUEST, xonxoff, (SIO_XON_XOFF_HS | 0), NULL, 0)
      < 0)
    return -1;

  return 0;
}

int _ftdi_setdtr_rts(int dtr, int rts)
{
  unsigned short usb_val;

  if (dtr)
    usb_val = SIO_SET_DTR_HIGH;
  else
    usb_val = SIO_SET_DTR_LOW;

  if (rts)
    usb_val |= SIO_SET_RTS_HIGH;
  else
    usb_val |= SIO_SET_RTS_LOW;

  if (_control_transfer(FTDI_DEVICE_OUT_REQTYPE, SIO_SET_MODEM_CTRL_REQUEST, usb_val, 0, NULL, 0) < 0)
    return -1;

  return 0;
}

int _ftdi_setdtr(int dtrstate)
{
  unsigned short usb_val;

  if (dtrstate)
    usb_val = SIO_SET_DTR_HIGH;
  else
    usb_val = SIO_SET_DTR_LOW;

  if (_control_transfer(FTDI_DEVICE_OUT_REQTYPE, SIO_SET_MODEM_CTRL_REQUEST, usb_val, 0, NULL, 0) < 0)
    return -1;

  return 0;
}

int _ftdi_setrts(int rtsstate)
{
  unsigned short usb_val;

  if (rtsstate)
    usb_val = SIO_SET_RTS_HIGH;
  else
    usb_val = SIO_SET_RTS_LOW;

  if (_control_transfer(FTDI_DEVICE_OUT_REQTYPE, SIO_SET_MODEM_CTRL_REQUEST, usb_val, 0, NULL, 0) < 0)
    return -1;

  return 0;
}
