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

#ifndef __SERIALDEVICE_H__
#define __SERIALDEVICE_H__

#include "devices/ftdi_chips.h"

#include <psp2/types.h>
#include <stdint.h>


typedef struct
{
  /* USB specific */
  SceUID device_id;
  uint8_t type;
  int vendor;
  int product;

  /* Endpoints */
  SceUID in_pipe_id;
  SceUID out_pipe_id;
  SceUID control_pipe_id;

  /** FTDI chip type */
  enum ftdi_chip_type ftdi_type;

  /** ch34x fields */
  uint32_t ch34x_quirks;
  uint8_t ch34x_mcr;
  uint8_t ch34x_msr;
  uint8_t ch34x_lcr;
  uint8_t ch34x_version;

  /** baudrate */
  int baudrate;

  /** pointer to read buffer for read_data */
  unsigned char readbuffer[4096] __attribute__((aligned(64)));
  /** read buffer offset */
  unsigned int readbuffer_offset;
  /** number of remaining data in internal read buffer */
  unsigned int readbuffer_remaining;
  /** read buffer chunk size */
  unsigned int readbuffer_chunksize;

  unsigned char writebuffer[4096] __attribute__((aligned(64)));
  /** write buffer chunk size */
  unsigned int writebuffer_chunksize;
  /** maximum packet size. Needed for filtering modem status bytes every n packets. */
  unsigned int max_packet_size;

} serialDevice;

#endif // __SERIALDEVICE_H__
