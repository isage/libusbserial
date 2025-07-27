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

#include "libusbserial_private.h"
#include "libusbserial.h"
#include "devicelist.h"
#include "devices/ftdi.h"
#include "devices/ch34x.h"
#include "serialdevice.h"
#include "ringbuf.h"

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

#define EVF_SEND 1
#define EVF_RECV 2
#define EVF_CTRL 4

#define MAX_RINGBUF_SIZE 0x1000

SceUID transfer_ev;

static uint8_t started = 0;
static uint8_t plugged = 0;
static int transferred;

static serialDevice ctx;

int libusbserial_probe(int device_id);
int libusbserial_attach(int device_id);
int libusbserial_detach(int device_id);

static const SceUsbdDriver libusbserialDriver = {
    .name   = "libusbserial",
    .probe  = libusbserial_probe,
    .attach = libusbserial_attach,
    .detach = libusbserial_detach,
};

static int _init_ctx()
{
  ctx.in_pipe_id        = 0;
  ctx.out_pipe_id       = 0;
  ctx.control_pipe_id   = 0;

  ctx.type      = TYPE_UNKNOWN;
  ctx.vendor    = 0;
  ctx.product   = 0;
  ctx.ftdi_type = TYPE_BM; /* chip type */
  ctx.baudrate  = 9600;

  ctx.writebuffer_chunksize = 4096;
  ctx.max_packet_size       = 64;

  return 0;
}


static int libusbserial_sysevent_handler(int resume, int eventid, void *args, void *opt)
{
  if (resume && started)
  {
    ksceUsbServMacSelect(2, 0); // re-set host mode
  }
  return 0;
}

void _callback_control(int32_t result, int32_t count, void *arg)
{
  trace("config cb result: %08x, count: %d\n", result, count);
  ksceKernelSetEventFlag(transfer_ev, EVF_CTRL);
}

void _callback_send(int32_t result, int32_t count, void *arg)
{
  trace("send cb result: %08x, count: %d\n", result, count);
  if (result == 0)
    *(int *)arg = count;
  ksceKernelSetEventFlag(transfer_ev, EVF_SEND);
}

void usb_read(void);
void _callback_recv(int32_t result, int32_t count, void *arg)
{
  trace("recv cb result: %08x, count: %d\n", result, count);

  if (result == 0 && count > 0)
  {
    // filter FTDI
    if (ctx.type == TYPE_FTDI && count > 2)
    {
        ringbuf_put_clobber(ctx.read_buffer+2, count-2);
    }
    else if (ctx.type != TYPE_FTDI)
    {
        ringbuf_put_clobber(ctx.read_buffer, count);
    }
  }
  usb_read();
}

int _control_transfer(int rtype, int req, int val, int idx, void *data, int len)
{
  SceUsbdDeviceRequest _dr;
  _dr.bmRequestType = rtype; // (0x02 << 5)
  _dr.bRequest      = req;
  _dr.wValue        = val;
  _dr.wIndex        = idx;
  _dr.wLength       = len;

  int ret = ksceUsbdControlTransfer(ctx.control_pipe_id, &_dr, data, _callback_control, NULL);
  if (ret < 0)
    return ret;
  trace("waiting ef (cfg)\n");
  ksceKernelWaitEventFlag(transfer_ev, EVF_CTRL, SCE_EVENT_WAITCLEAR_PAT | SCE_EVENT_WAITAND, NULL, 0);
  return 0;
}

int _send(unsigned char *request, unsigned int length)
{
  transferred = 0;
  // transfer
  trace("sending 0x%08x\n", request);
  int ret = ksceUsbdBulkTransfer(ctx.out_pipe_id, request, length, _callback_send, &transferred);
  trace("send 0x%08x\n", ret);
  if (ret < 0)
    return ret;
  // wait for eventflag
  trace("waiting ef (send)\n");
  ksceKernelWaitEventFlag(transfer_ev, EVF_SEND, SCE_EVENT_WAITCLEAR_PAT | SCE_EVENT_WAITAND, NULL, 0);
  return transferred;
}

void usb_read()
{
  int ret = ksceUsbdBulkTransfer(ctx.in_pipe_id, ctx.read_buffer, ctx.max_packet_size, _callback_recv, NULL);

  if (ret < 0)
  {
    ksceDebugPrintf("ksceUsbdInterruptTransfer(in) error: 0x%08x\n", ret);
    // error out
  }
}

/*
 *  Driver
 */

int libusbserial_probe(int device_id)
{
  SceUsbdDeviceDescriptor *device;
  trace("probing device: %x\n", device_id);
  device = (SceUsbdDeviceDescriptor *)ksceUsbdScanStaticDescriptor(device_id, 0, SCE_USBD_DESCRIPTOR_DEVICE);
  if (device)
  {
    trace("vendor: %04x\n", device->idVendor);
    trace("product: %04x\n", device->idProduct);

    int i;
    for (i = 0; _devices[i].type != TYPE_UNKNOWN; i++)
    {
      if (_devices[i].idVendor == device->idVendor && _devices[i].idProduct == device->idProduct)
        break;
    }

    if (_devices[i].type == TYPE_UNKNOWN)
    {
      ksceDebugPrintf("Not supported!\n");
      return SCE_USBD_PROBE_FAILED;
    }

    trace("found usbserial\n");
    return SCE_USBD_PROBE_SUCCEEDED;
  }
  return SCE_USBD_PROBE_FAILED;
}

int libusbserial_attach(int device_id)
{
  trace("attaching device: %x\n", device_id);
  SceUsbdDeviceDescriptor *device;

  device = (SceUsbdDeviceDescriptor *)ksceUsbdScanStaticDescriptor(device_id, 0, SCE_USBD_DESCRIPTOR_DEVICE);

  int i;
  for (i = 0; _devices[i].type != TYPE_UNKNOWN; i++)
  {
    if (_devices[i].idVendor == device->idVendor && _devices[i].idProduct == device->idProduct)
      break;
  }

  if (_devices[i].type == TYPE_UNKNOWN)
  {
    ksceDebugPrintf("Not supported!\n");
    return SCE_USBD_ATTACH_FAILED;
  }

  trace("scanning descriptors\n");

  {
    SceUsbdConfigurationDescriptor *cdesc;
    if ((cdesc = (SceUsbdConfigurationDescriptor *)ksceUsbdScanStaticDescriptor(device_id, NULL,
                                                                                SCE_USBD_DESCRIPTOR_CONFIGURATION))
        == NULL)
      return SCE_USBD_ATTACH_FAILED;

    if (cdesc->bNumInterfaces != 1)
      return SCE_USBD_ATTACH_FAILED;

    ctx.type = _devices[i].type;

    if (ctx.type == TYPE_FTDI)
    {
      if (device->bcdDevice == 0x400 || (device->bcdDevice == 0x200 && device->iSerialNumber == 0))
        ctx.ftdi_type = TYPE_BM;
      else if (device->bcdDevice == 0x200)
        ctx.ftdi_type = TYPE_AM;
      else if (device->bcdDevice == 0x500)
        ctx.ftdi_type = TYPE_2232C;
      else if (device->bcdDevice == 0x600)
        ctx.ftdi_type = TYPE_R;
      else if (device->bcdDevice == 0x700)
        ctx.ftdi_type = TYPE_2232H;
      else if (device->bcdDevice == 0x800)
        ctx.ftdi_type = TYPE_4232H;
      else if (device->bcdDevice == 0x900)
        ctx.ftdi_type = TYPE_232H;
      else if (device->bcdDevice == 0x1000)
        ctx.ftdi_type = TYPE_230X;

      trace("ftdi_type = %d\n", ctx.ftdi_type);

      // Determine maximum packet size
      ctx.max_packet_size = _ftdi_determine_max_packet_size(&ctx);

    }
    else if (ctx.type == TYPE_CH34X)
    {
      ctx.max_packet_size = _ch34x_determine_max_packet_size(&ctx);
    }

    trace("max_packet_size = %d\n", ctx.max_packet_size);


    SceUsbdEndpointDescriptor *endpoint;
    trace("scanning endpoints\n");
    endpoint
        = (SceUsbdEndpointDescriptor *)ksceUsbdScanStaticDescriptor(device_id, device, SCE_USBD_DESCRIPTOR_ENDPOINT);
    while (endpoint)
    {
      trace("got EP: %02x\n", endpoint->bEndpointAddress);
      if ((endpoint->bEndpointAddress & SCE_USBD_ENDPOINT_DIRECTION_BITS) == SCE_USBD_ENDPOINT_DIRECTION_IN && endpoint->bmAttributes == 2)
      {
        trace("opening in pipe\n");
        ctx.in_pipe_id = ksceUsbdOpenPipe(device_id, endpoint);
        trace("= 0x%08x\n", ctx.in_pipe_id);
      }
      else if ((endpoint->bEndpointAddress & SCE_USBD_ENDPOINT_DIRECTION_BITS) == SCE_USBD_ENDPOINT_DIRECTION_OUT)
      {
        trace("opening out pipe\n");
        ctx.out_pipe_id = ksceUsbdOpenPipe(device_id, endpoint);
        trace("= 0x%08x\n", ctx.out_pipe_id);
      }
      endpoint = (SceUsbdEndpointDescriptor *)ksceUsbdScanStaticDescriptor(device_id, endpoint,
                                                                           SCE_USBD_DESCRIPTOR_ENDPOINT);

      if (ctx.out_pipe_id > 0 && ctx.in_pipe_id > 0) break;

    }

    ctx.control_pipe_id = ksceUsbdOpenPipe(device_id, NULL);
    // set default config
    int r = ksceUsbdSetConfiguration(ctx.control_pipe_id, cdesc->bConfigurationValue, _callback_control, NULL);
#ifdef NDEBUG
    (void)r;
#endif
    trace("ksceUsbdSetConfiguration = 0x%08x\n", r);
    trace("waiting ef (cfg)\n");
    ksceKernelWaitEventFlag(transfer_ev, EVF_CTRL, SCE_EVENT_WAITCLEAR_PAT | SCE_EVENT_WAITAND, NULL, 0);

    trace("doing reset\n");

    if (ctx.type == TYPE_FTDI)
    {
      _ftdi_reset();
    }
    else if (ctx.type == TYPE_CH34X)
    {
      _ch34x_reset(&ctx);
    }

    ringbuf_reset();

    if (ctx.type == TYPE_FTDI)
    {
        if (_ftdi_set_baudrate(&ctx, 9600) != 0)
        {
          trace("can't set baudrate\n");
          return SCE_USBD_ATTACH_FAILED;
        }
    }
    else if (ctx.type == TYPE_CH34X)
    {
        if (_ch34x_set_baudrate(&ctx, 9600) != 0)
        {
          trace("can't set baudrate\n");
          return SCE_USBD_ATTACH_FAILED;
        }
    }

    if (ctx.out_pipe_id > 0 && ctx.in_pipe_id > 0 && ctx.control_pipe_id)
    {
      plugged = 1;
      usb_read();
      return SCE_USBD_ATTACH_SUCCEEDED;
    }
  }
  return SCE_USBD_ATTACH_FAILED;
}

int libusbserial_detach(int device_id)
{
  ctx.in_pipe_id  = 0;
  ctx.out_pipe_id = 0;
  plugged         = 0;
  return -1;
}

/*
 *  PUBLIC
 */

int libusbserial_start()
{
  uint32_t state;
  ENTER_SYSCALL(state);

  trace("starting libusbserial\n");
  if (started)
  {
    _error_return(-1, "Already started");
  }

  // reset ctx
  _init_ctx();

  if (ringbuf_init(MAX_RINGBUF_SIZE) < 0)
  {
      EXIT_SYSCALL(state);
      return -1;
  }

  started = 1;
  int ret = ksceUsbServMacSelect(2, 0);
#ifdef NDEBUG
  (void)ret;
#endif
  trace("MAC select = 0x%08x\n", ret);
  ret = ksceUsbdRegisterDriver(&libusbserialDriver);
  trace("ksceUsbdRegisterDriver = 0x%08x\n", ret);
  EXIT_SYSCALL(state);
  if (ret < 0) return ret;
  return 0;
}

int libusbserial_stop()
{
  uint32_t state;
  ENTER_SYSCALL(state);
  if (!started)
  {
    _error_return(-1, "Not started");
  }

  started = 0;
  plugged = 0;
  if (ctx.in_pipe_id)
    ksceUsbdClosePipe(ctx.in_pipe_id);
  if (ctx.out_pipe_id)
    ksceUsbdClosePipe(ctx.out_pipe_id);
  if (ctx.control_pipe_id)
    ksceUsbdClosePipe(ctx.control_pipe_id);
  ksceUsbdUnregisterDriver(&libusbserialDriver);
  ksceUsbServMacSelect(2, 1);

  ksceKernelSetEventFlag(transfer_ev, EVF_CTRL);
  ksceKernelSetEventFlag(transfer_ev, EVF_SEND);
  ksceKernelSetEventFlag(transfer_ev, EVF_RECV);

  ringbuf_term();

  EXIT_SYSCALL(state);

  return 1;
  // TODO: restore udcd?
}

int libusbserial_device_connected()
{
  return (started && plugged);
}

int libusbserial_set_baudrate(int baudrate)
{
  int ret = 0;
  uint32_t state;
  ENTER_SYSCALL(state);

  if (!started || !plugged)
    _error_return(-2, "USB device unavailable");

  if (ctx.type == TYPE_FTDI)
      ret = _ftdi_set_baudrate(&ctx, baudrate);
  else if (ctx.type == TYPE_CH34X)
      ret = _ch34x_set_baudrate(&ctx, baudrate);

  if (ret < 0)
  {
    EXIT_SYSCALL(state);
    return ret;
  }

  EXIT_SYSCALL(state);
  return 0;
}

int libusbserial_set_line_property(enum bits_type bits, enum stopbits_type sbit, enum parity_type parity,
                            enum break_type break_type)
{
  uint32_t state;
  ENTER_SYSCALL(state);

  if (!started || !plugged)
    _error_return(-2, "USB device unavailable");

  trace("libusbserial_set_line_property(%d,%d,%d,%d)\n", bits, sbit, parity, break_type);

  if (ctx.type == TYPE_FTDI)
  {
    if (_ftdi_set_line_property(bits, sbit, parity, break_type) < 0)
    {
      EXIT_SYSCALL(state);
      return -1;
    }
  }
  else if (ctx.type == TYPE_CH34X)
  {
    if (_ch34x_set_line_property(&ctx, bits, sbit, parity, break_type) < 0)
    {
      EXIT_SYSCALL(state);
      return -1;
    }
  }
  EXIT_SYSCALL(state);

  return 0;
}

int libusbserial_write_data(const unsigned char *buf, int size)
{
  int offset = 0;
  int actual_length;
  uint32_t state;
  ENTER_SYSCALL(state);

  if (!started || !plugged)
    _error_return(-2, "USB device unavailable");

  trace("size: %d\n", size);

  while (offset < size)
  {
    int write_size = ctx.writebuffer_chunksize;

    if (offset + write_size > size)
      write_size = size - offset;

    trace("write size: %d\n", write_size);

    ksceKernelMemcpyUserToKernel(ctx.writebuffer, buf + offset, write_size);

    actual_length = _send((unsigned char *)ctx.writebuffer, write_size);
    if (actual_length < 0)
      return -1;

    offset += actual_length;
  }

  EXIT_SYSCALL(state);

  return offset;
}

int libusbserial_read_data_blocking(unsigned char *buf, int size, SceUInt timeout)
{
  uint32_t state;
  unsigned char kbuf[64];
  ENTER_SYSCALL(state);
  int ret = -1;
  int left = size;
  int pos = 0;
  while (left > 0)
  {
    if (left >= 64)
      ret = ringbuf_get_wait(kbuf, 64, timeout);
    else
      ret = ringbuf_get_wait(kbuf, left, timeout);
    if (ret > 0)
      ksceKernelMemcpyKernelToUser(buf+pos, kbuf, ret);
    else
      break;

    pos += ret;
    left -= ret;
  }
  EXIT_SYSCALL(state);
  return ret;

}

int libusbserial_read_data(unsigned char *buf, int size)
{
  uint32_t state;
  // we use small batches because we have small stack by default.
  // TODO: do batch copy in ringbuf?
  unsigned char kbuf[64];
  ENTER_SYSCALL(state);
  int ret = -1;
  int left = size;
  int pos = 0;
  while (left > 0)
  {
    if (left >= 64)
      ret = ringbuf_get(kbuf, 64);
    else
      ret = ringbuf_get(kbuf, left);
    if (ret > 0)
      ksceKernelMemcpyKernelToUser(buf+pos, kbuf, ret);
    else
      break;

    pos += ret;
    left -= ret;
  }
  EXIT_SYSCALL(state);
  return ret;
}

int libusbserial_available_count()
{
    return ringbuf_available();
}

int libusbserial_tciflush()
{
  uint32_t state;
  ENTER_SYSCALL(state);

  if (!started || !plugged)
    _error_return(-2, "USB device unavailable");

  if (ctx.type == TYPE_FTDI)
  {
    if (_ftdi_tciflush() < 0)
      _error_return(-1, "Purge of RX buffer failed");
  }
  else if (ctx.type == TYPE_CH34X)
  {
    if (_ch34x_tciflush() < 0)
      _error_return(-1, "Purge of RX buffer failed");
  }

  // Invalidate data in the readbuffer
  ringbuf_reset();

  EXIT_SYSCALL(state);
  return 0;
}


int libusbserial_tcoflush()
{
  uint32_t state;
  ENTER_SYSCALL(state);

  if (!started || !plugged)
    _error_return(-2, "USB device unavailable");

  if (ctx.type == TYPE_FTDI)
  {
    if (_ftdi_tcoflush() < 0)
      _error_return(-1, "Purge of TX buffer failed");
  }
  else if (ctx.type == TYPE_CH34X)
  {
    if (_ch34x_tcoflush() < 0)
      _error_return(-1, "Purge of TX buffer failed");
  }

  EXIT_SYSCALL(state);
  return 0;
}

int libusbserial_tcioflush()
{
  int result;
  uint32_t state;
  ENTER_SYSCALL(state);

  if (!started || !plugged)
    _error_return(-3, "USB device unavailable");


  if (ctx.type == TYPE_FTDI)
  {
    result = _ftdi_tcoflush();
    if (result < 0)
      _error_return(-1, "Purge of TX buffer failed");

    result = _ftdi_tciflush();
    if (result < 0)
      _error_return(-2, "Purge of RX buffer failed");
  }
  else if (ctx.type == TYPE_CH34X)
  {
    result = _ch34x_tcoflush();
    if (result < 0)
      _error_return(-1, "Purge of TX buffer failed");

    result = _ch34x_tciflush();
    if (result < 0)
      _error_return(-2, "Purge of RX buffer failed");
  }

  // Invalidate data in the readbuffer
  ringbuf_reset();

  EXIT_SYSCALL(state);
  return 0;
}

int libusbserial_setflowctrl(int flowctrl)
{
  uint32_t state;
  ENTER_SYSCALL(state);
  if (!started || !plugged)
    _error_return(-2, "USB device unavailable");

  if (ctx.type == TYPE_FTDI)
  {
    if (_ftdi_setflowctrl(flowctrl) < 0)
      _error_return(-1, "set flow control failed");
  }
  else if (ctx.type == TYPE_CH34X)
  {
    if (_ch34x_setflowctrl(&ctx, flowctrl) < 0)
      _error_return(-1, "set flow control failed");
  }

  EXIT_SYSCALL(state);
  return 0;
}

int libusbserial_setflowctrl_xonxoff(unsigned char xon, unsigned char xoff)
{
  uint32_t state;
  ENTER_SYSCALL(state);

  if (!started || !plugged)
    _error_return(-2, "USB device unavailable");

  if (ctx.type == TYPE_FTDI)
  {
    if (_ftdi_setflowctrl_xonxoff(xon, xoff) < 0)
      _error_return(-1, "set flow control failed");
  }
  else if (ctx.type == TYPE_CH34X)
  {
    if (_ch34x_setflowctrl_xonxoff(&ctx, xon, xoff) < 0)
      _error_return(-1, "set flow control failed");
  }

  EXIT_SYSCALL(state);
  return 0;
}

int libusbserial_setdtr_rts(int dtr, int rts)
{
  uint32_t state;
  ENTER_SYSCALL(state);

  if (!started || !plugged)
    _error_return(-2, "USB device unavailable");

  if (ctx.type == TYPE_FTDI)
  {
    if (_ftdi_setdtr_rts(dtr, rts) < 0)
      _error_return(-1, "set of rts/dtr failed");
  }
  else if (ctx.type == TYPE_CH34X)
  {
    if (_ch34x_setdtr_rts(&ctx, dtr, rts) < 0)
      _error_return(-1, "set of rts/dtr failed");
  }

  EXIT_SYSCALL(state);
  return 0;
}

int libusbserial_setdtr(int dtrstate)
{
  uint32_t state;
  ENTER_SYSCALL(state);

  if (!started || !plugged)
    _error_return(-2, "USB device unavailable");

  if (ctx.type == TYPE_FTDI)
  {
    if (_ftdi_setdtr(dtrstate) < 0)
      _error_return(-1, "set dtr failed");
  }
  else if (ctx.type == TYPE_CH34X)
  {
    if (_ch34x_setdtr(&ctx, dtrstate) < 0)
      _error_return(-1, "set dtr failed");
  }

  EXIT_SYSCALL(state);
  return 0;
}

int libusbserial_setrts(int rtsstate)
{
  uint32_t state;
  ENTER_SYSCALL(state);

  if (!started || !plugged)
    _error_return(-2, "USB device unavailable");

  if (ctx.type == TYPE_FTDI)
  {
    if (_ftdi_setrts(rtsstate) < 0)
      _error_return(-1, "set of rts failed");
  }
  else if (ctx.type == TYPE_CH34X)
  {
    if (_ch34x_setrts(&ctx, rtsstate) < 0)
      _error_return(-1, "set of rts failed");
  }

  EXIT_SYSCALL(state);
  return 0;
}

void _start() __attribute__((weak, alias("module_start")));

int module_start(SceSize args, void *argp)
{
  trace("libusbserial starting\n");
  ksceKernelRegisterSysEventHandler("zlibusbserial_sysevent", libusbserial_sysevent_handler, NULL);
  transfer_ev = ksceKernelCreateEventFlag("libusbserial_transfer", 0, 0, NULL);
  trace("ef: 0x%08x\n", transfer_ev);
  return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp)
{
  libusbserial_stop();
  return SCE_KERNEL_STOP_SUCCESS;
}
