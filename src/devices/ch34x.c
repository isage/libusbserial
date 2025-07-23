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
#include "ch34x.h"

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

#define CH34X_CLKRATE       48000000
#define CH34X_CLK_DIV(ps, fact) (1 << (12 - 3 * (ps) - (fact)))
#define CH34X_MIN_RATE(ps)  (CH34X_CLKRATE / (CH34X_CLK_DIV((ps), 1) * 512))

static const uint32_t ch34x_min_rates[] = {
    CH34X_MIN_RATE(0),
    CH34X_MIN_RATE(1),
    CH34X_MIN_RATE(2),
    CH34X_MIN_RATE(3),
};

/* Supported range is 46 to 3000000 bps. */
#define CH34X_MIN_BPS   DIV_ROUND_UP(CH34X_CLKRATE, CH34X_CLK_DIV(0, 0) * 256)
#define CH34X_MAX_BPS   (CH34X_CLKRATE / (CH34X_CLK_DIV(3, 0) * 2))

static uint32_t clamp_val(uint32_t val, uint32_t min, uint32_t max)
{
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/*
 * The device line speed is given by the following equation:
 *
 *  baudrate = 48000000 / (2^(12 - 3 * ps - fact) * div), where
 *
 *      0 <= ps <= 3,
 *      0 <= fact <= 1,
 *      2 <= div <= 256 if fact = 0, or
 *      9 <= div <= 256 if fact = 1
 */
static int _ch34x_get_divisor(serialDevice *ctx, uint32_t speed)
{
    uint32_t fact, div, clk_div;
    uint8_t force_fact0 = 0;
    int ps;

    /*
     * Clamp to supported range, this makes the (ps < 0) and (div < 2)
     * sanity checks below redundant.
     */
    speed = clamp_val(speed, CH34X_MIN_BPS, CH34X_MAX_BPS);

    /*
     * Start with highest possible base clock (fact = 1) that will give a
     * divisor strictly less than 512.
     */
    fact = 1;
    for (ps = 3; ps >= 0; ps--) {
        if (speed > ch34x_min_rates[ps])
            break;
    }

    if (ps < 0)
        return -1;

    /* Determine corresponding divisor, rounding down. */
    clk_div = CH34X_CLK_DIV(ps, fact);
    div = CH34X_CLKRATE / (clk_div * speed);

    /* Some devices require a lower base clock if ps < 3. */
    if (ps < 3 && (ctx->ch34x_quirks & CH34X_QUIRK_LIMITED_PRESCALER))
        force_fact0 = 1;

    /* Halve base clock (fact = 0) if required. */
    if (div < 9 || div > 255 || force_fact0) {
        div /= 2;
        clk_div *= 2;
        fact = 0;
    }

    if (div < 2)
        return -1;

    /*
     * Pick next divisor if resulting rate is closer to the requested one,
     * scale up to avoid rounding errors on low rates.
     */
    if (16 * CH34X_CLKRATE / (clk_div * div) - 16 * speed >=
            16 * speed - 16 * CH34X_CLKRATE / (clk_div * (div + 1)))
        div++;

    /*
     * Prefer lower base clock (fact = 0) if even divisor.
     *
     * Note that this makes the receiver more tolerant to errors.
     */
    if (fact == 1 && div % 2 == 0) {
        div /= 2;
        fact = 0;
    }

    return (0x100 - div) << 8 | fact << 2 | ps;
}

static int _ch34x_set_baudrate_lcr(serialDevice *ctx, uint32_t baud_rate, uint8_t lcr)
{
    int val;
    int r;

    if (!baud_rate)
        return -1;

    val = _ch34x_get_divisor(ctx, baud_rate);
    if (val < 0)
        return -1;

    /*
     * CH341A buffers data until a full endpoint-size packet (32 bytes)
     * has been received unless bit 7 is set.
     *
     * At least one device with version 0x27 appears to have this bit
     * inverted.
     */
    if (ctx->ch34x_version > 0x27)
        val |= BIT(7);

    r = _control_transfer(SCE_USBD_REQTYPE_TYPE_VENDOR | SCE_USBD_REQTYPE_RECIP_DEVICE | SCE_USBD_REQTYPE_DIR_TO_DEVICE, CH34X_REQ_WRITE_REG, CH34X_REG_DIVISOR << 8 | CH34X_REG_PRESCALER, val, NULL, 0);
    if (r < 0)
        return -1;

    /*
     * Chip versions before version 0x30 as read using
     * CH341_REQ_READ_VERSION used separate registers for line control
     * (stop bits, parity and word length). Version 0x30 and above use
     * CH341_REG_LCR only and CH341_REG_LCR2 is always set to zero.
     */
    if (ctx->ch34x_version < 0x30)
        return 0;

    r = _control_transfer(SCE_USBD_REQTYPE_TYPE_VENDOR | SCE_USBD_REQTYPE_RECIP_DEVICE | SCE_USBD_REQTYPE_DIR_TO_DEVICE, CH34X_REQ_WRITE_REG, CH34X_REG_LCR2 << 8 | CH34X_REG_LCR, lcr, NULL, 0);
    if (r < 0)
        return -1;

    return 0;
}

static int _ch34x_set_handshake(uint8_t control)
{
    if(_control_transfer(SCE_USBD_REQTYPE_TYPE_VENDOR | SCE_USBD_REQTYPE_RECIP_DEVICE | SCE_USBD_REQTYPE_DIR_TO_DEVICE, CH34X_REQ_MODEM_CTRL, ~control, 0, NULL, 0) < 0)
        return -1;
    return 0;
}

static int _ch34x_get_status(serialDevice *ctx)
{
    const uint32_t size = 2;
    unsigned char buffer[64] __attribute__((aligned(64)));
    int r;

    r = _control_transfer(SCE_USBD_REQTYPE_TYPE_VENDOR | SCE_USBD_REQTYPE_RECIP_DEVICE | SCE_USBD_REQTYPE_DIR_TO_HOST, CH34X_REQ_READ_REG, 0x0706, 0, buffer, size);
    if (r < 0)
        return -1;

    ctx->ch34x_msr = (~(*buffer)) & CH34X_BITS_MODEM_STAT;

    return 0;
}

static int _ch34x_configure(serialDevice *ctx)
{
    const uint32_t size = 2;
    unsigned char buffer[64] __attribute__((aligned(64)));
    int r = 0;

    /* expect two bytes 0x27 0x00 */
    r = _control_transfer(SCE_USBD_REQTYPE_TYPE_VENDOR | SCE_USBD_REQTYPE_RECIP_DEVICE | SCE_USBD_REQTYPE_DIR_TO_HOST, CH34X_REQ_READ_VERSION, 0, 0, buffer, size);
    if (r < 0)
        return -1;

    ctx->ch34x_version = buffer[0];
    trace("Chip version: 0x%02x\n", ctx->ch34x_version);

    r = _control_transfer(SCE_USBD_REQTYPE_TYPE_VENDOR | SCE_USBD_REQTYPE_RECIP_DEVICE | SCE_USBD_REQTYPE_DIR_TO_DEVICE, CH34X_REQ_SERIAL_INIT, 0, 0, NULL, 0);
    if (r < 0)
        return -1;

    r = _ch34x_set_baudrate_lcr(ctx, ctx->baudrate, ctx->ch34x_lcr);
    if (r < 0)
        return -1;

    ctx->ch34x_mcr |= CH34X_BIT_RTS;
    ctx->ch34x_mcr |= CH34X_BIT_DTR;

    r = _ch34x_set_handshake(ctx->ch34x_mcr);
    if (r < 0)
        return -1;

    return 0;
}

static int _ch34x_detect_quirks(serialDevice* ctx)
{
    const uint32_t size = 2;
    uint32_t quirks = 0;
    unsigned char buffer[64] __attribute__((aligned(64)));
    int r;

    /*
     * A subset of CH34x devices does not support all features. The
     * prescaler is limited and there is no support for sending a RS232
     * break condition. A read failure when trying to set up the latter is
     * used to detect these devices.
     */
    r = _control_transfer(SCE_USBD_REQTYPE_TYPE_VENDOR | SCE_USBD_REQTYPE_RECIP_DEVICE | SCE_USBD_REQTYPE_DIR_TO_HOST, CH34X_REQ_READ_REG, CH34X_REG_BREAK, 0, buffer, size);
/*    if (r < 0) {
        trace("break control not supported, using simulated break\n");
        quirks = CH34X_QUIRK_LIMITED_PRESCALER | CH34X_QUIRK_SIMULATE_BREAK;
        r = 0;
    } else*/ if (r < 0) {
        trace("failed to read break control: 0x%08X\n", r);
        return -1;
    }

    if (quirks) {
        trace("enabling quirk flags: 0x%02lx\n", quirks);
        ctx->ch34x_quirks |= quirks;
    }

    return 0;
}

int _ch34x_reset(serialDevice* ctx)
{
    int r = 0;

    trace("ctx: 0x%08x\n", ctx);

    ctx->baudrate = 9600;

    /*
     * Some CH340 devices appear unable to change the initial LCR
     * settings, so set a sane 8N1 default.
     */
    ctx->ch34x_lcr = CH34X_LCR_ENABLE_RX | CH34X_LCR_ENABLE_TX | CH34X_LCR_CS8;

    trace("configure\n");
    r = _ch34x_configure(ctx);
    if (r < 0)
      return -1;

    trace("detect quircs\n");
    r = _ch34x_detect_quirks(ctx);
    if (r < 0)
      return -1;

    trace("get status\n");
    r = _ch34x_get_status(ctx);
    if (r < 0)
      return -1;

    return 0;
}

static int _ch34x_set_flow_control(serialDevice* ctx, uint16_t flow_ctl)
{
    int r;

//    if (C_CRTSCTS(tty))
//        flow_ctl = CH34X_FLOW_CTL_RTSCTS;
//    else
//        flow_ctl = CH34X_FLOW_CTL_NONE;

    r = _control_transfer(SCE_USBD_REQTYPE_TYPE_VENDOR | SCE_USBD_REQTYPE_RECIP_DEVICE | SCE_USBD_REQTYPE_DIR_TO_DEVICE, CH34X_REQ_WRITE_REG, (CH34X_REG_FLOW_CTL << 8) | CH34X_REG_FLOW_CTL, (flow_ctl << 8) | flow_ctl, NULL, 0);
    if (r < 0) return -1;
    return 0;
}


int _ch34x_set_baudrate(serialDevice* ctx, int baudrate)
{
  int r;
  ctx->baudrate = baudrate;
  r = _ch34x_set_baudrate_lcr(ctx, ctx->baudrate, ctx->ch34x_lcr);
  return r;
}

int _ch34x_set_line_property(serialDevice* ctx, enum bits_type bits, enum stopbits_type sbit, enum parity_type parity, enum break_type break_type)
{
    int r;
    uint8_t lcr;
    lcr = CH34X_LCR_ENABLE_RX | CH34X_LCR_ENABLE_TX | CH34X_LCR_CS8;
    switch (parity)
    {
      case PARITY_NONE:
        break;
      case PARITY_ODD:
        lcr |= CH34X_LCR_ENABLE_PAR;
        break;
      case PARITY_EVEN:
        lcr |= CH34X_LCR_ENABLE_PAR;
        lcr |= CH34X_LCR_PAR_EVEN;
        break;
      case PARITY_MARK:
        lcr |= CH34X_LCR_ENABLE_PAR;
        break;
      case PARITY_SPACE:
        lcr |= CH34X_LCR_ENABLE_PAR;
        lcr |= CH34X_LCR_MARK_SPACE;
        break;
    }

    switch (bits)
    {
      case BITS_7:
        lcr |= CH34X_LCR_CS7;
        break;
      case BITS_8:
        lcr |= CH34X_LCR_CS8;
        break;
    }


  switch (sbit)
  {
    case STOP_BIT_1:
      break;
    case STOP_BIT_15:
      break;
    case STOP_BIT_2:
      lcr |= CH34X_LCR_STOP_BITS_2;
      break;
  }

  // todo
  switch (break_type)
  {
    case BREAK_OFF:
      break;
    case BREAK_ON:
      break;
  }
  ctx->ch34x_lcr = lcr;

  r = _ch34x_set_baudrate_lcr(ctx, ctx->baudrate, ctx->ch34x_lcr);
  return r;
}

int _ch34x_tciflush()
{
    // TODO
    return 0;
}

int _ch34x_tcoflush()
{
    // TODO
    return 0;
}

int _ch34x_setflowctrl(serialDevice* ctx, int flowctrl)
{
  uint16_t flow_ctl = 0;
  return _ch34x_set_flow_control(ctx, flow_ctl);
}

int _ch34x_setflowctrl_xonxoff(serialDevice* ctx, unsigned char xon, unsigned char xoff)
{
    // TODO
    return -1;
}

int _ch34x_setdtr_rts(serialDevice* ctx, int dtr, int rts)
{
    if (rts)
        ctx->ch34x_mcr |= CH34X_BIT_RTS;
    else
        ctx->ch34x_mcr &= ~CH34X_BIT_RTS;
    if (dtr)
        ctx->ch34x_mcr |= CH34X_BIT_DTR;
    else
        ctx->ch34x_mcr &= ~CH34X_BIT_DTR;

    return _ch34x_set_handshake(ctx->ch34x_mcr);
}

int _ch34x_setdtr(serialDevice* ctx, int dtrstate)
{
    if (dtrstate)
        ctx->ch34x_mcr |= CH34X_BIT_DTR;
    else
        ctx->ch34x_mcr &= ~CH34X_BIT_DTR;

    return _ch34x_set_handshake(ctx->ch34x_mcr);
}

int _ch34x_setrts(serialDevice* ctx, int rtsstate)
{
    if (rtsstate)
        ctx->ch34x_mcr |= CH34X_BIT_RTS;
    else
        ctx->ch34x_mcr &= ~CH34X_BIT_RTS;
    return _ch34x_set_handshake(ctx->ch34x_mcr);

}

unsigned int _ch34x_determine_max_packet_size(serialDevice* ctx)
{
    return 32; // TODO
}
