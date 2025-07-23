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

#ifndef __FTDI_CHIPS_H__
#define __FTDI_CHIPS_H__

#include <psp2/types.h>
#include <stdint.h>


/** FTDI chip type */
enum ftdi_chip_type
{
  TYPE_AM    = 0,
  TYPE_BM    = 1,
  TYPE_2232C = 2,
  TYPE_R     = 3,
  TYPE_2232H = 4,
  TYPE_4232H = 5,
  TYPE_232H  = 6,
  TYPE_230X  = 7,
};

#endif // __FTDI_CHIPS_H__
