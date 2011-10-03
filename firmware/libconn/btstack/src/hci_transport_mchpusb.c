/*
 * Copyright 2011 Ytai Ben-Tsvi. All rights reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without modification, are
 * permitted provided that the following conditions are met:
 *
 *    1. Redistributions of source code must retain the above copyright notice, this list of
 *       conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above copyright notice, this list
 *       of conditions and the following disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL ARSHAN POURSOHI OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are those of the
 * authors and should not be interpreted as representing official policies, either expressed
 * or implied.
 */

// Implementation of btstack hci transport layer over IOIO's bluetooth dongle
// driver within the MCHPUSB host framework.

#include "config.h"

#include "usb_host_bluetooth.h"
#include "logging.h"

#include "debug.h"
#include "hci.h"
#include "hci_transport.h"
#include "hci_dump.h"

#include "../../../app_layer_v1/byte_queue.h"

static uint8_t bulk_in[64];
static uint8_t int_in[64];

DEFINE_STATIC_BYTE_QUEUE(bulk_out, 1024);
DEFINE_STATIC_BYTE_QUEUE(ctrl_out, 1024);
int bosize;
int cosize;

void hci_transport_mchpusb_tasks() {
  if (!USBHostBlueToothIntInBusy()) {
    USBHostBluetoothReadInt(int_in, sizeof int_in);
  }
  if (!USBHostBlueToothBulkInBusy()) {
    USBHostBluetoothReadBulk(bulk_in, sizeof bulk_in);
  }
  if (ByteQueueSize(&bulk_out) && !USBHostBlueToothBulkOutBusy()) {
    const BYTE *data;
    ByteQueuePeek(&bulk_out, &data, &bosize);
    USBHostBluetoothWriteBulk(data, bosize);
  }
  if (ByteQueueSize(&ctrl_out) && !USBHostBlueToothControlOutBusy()) {
    const BYTE *data;
    ByteQueuePeek(&ctrl_out, &data, &cosize);
    USBHostBluetoothWriteControl(data, cosize);
  }
}

// prototypes
static void dummy_handler(uint8_t packet_type, uint8_t *packet, uint16_t size);

// single instance
static hci_transport_t hci_transport_mchpusb;

static void (*packet_handler)(uint8_t packet_type, uint8_t *packet, uint16_t size) = dummy_handler;

static int usb_open(void *transport_config) {
  ByteQueueInit(&bulk_out, bulk_out_buf, sizeof bulk_out_buf);
  ByteQueueInit(&ctrl_out, ctrl_out_buf, sizeof ctrl_out_buf);
  return 0;
}

static int usb_close() {
  return 0;
}

static int usb_send_cmd_packet(uint8_t *packet, int size) {
  ByteQueuePushBuffer(&ctrl_out, packet, size);
  return 0;
}

static int usb_send_acl_packet(uint8_t *packet, int size) {
  ByteQueuePushBuffer(&bulk_out, packet, size);
  return 0;
}

static int usb_send_packet(uint8_t packet_type, uint8_t * packet, int size) {
  switch (packet_type) {
    case HCI_COMMAND_DATA_PACKET:
      return usb_send_cmd_packet(packet, size);
    case HCI_ACL_DATA_PACKET:
      return usb_send_acl_packet(packet, size);
    default:
      return -1;
  }
}

static int usb_can_send_packet(uint8_t packet_type) {
  return 1;
}

static void usb_register_packet_handler(void (*handler)(uint8_t packet_type, uint8_t *packet, uint16_t size)) {
  log_info("registering packet handler\n");
  packet_handler = handler;
}

static const char * usb_get_transport_name() {
  return "USB";
}

static void dummy_handler(uint8_t packet_type, uint8_t *packet, uint16_t size) {
}

// get usb singleton

hci_transport_t * hci_transport_mchpusb_instance() {
  hci_transport_mchpusb.open = usb_open;
  hci_transport_mchpusb.close = usb_close;
  hci_transport_mchpusb.send_packet = usb_send_packet;
  hci_transport_mchpusb.register_packet_handler = usb_register_packet_handler;
  hci_transport_mchpusb.get_transport_name = usb_get_transport_name;
  hci_transport_mchpusb.set_baudrate = NULL;
  hci_transport_mchpusb.can_send_packet_now = usb_can_send_packet;
  return &hci_transport_mchpusb;
}

BOOL USBHostBluetoothCallback(BLUETOOTH_EVENT event,
        USB_EVENT status,
        void *data,
        DWORD size) {
  switch (event) {
    case BLUETOOTH_EVENT_WRITE_BULK_DONE:
      ByteQueuePull(&bulk_out, bosize);
      return TRUE;

    case BLUETOOTH_EVENT_WRITE_CONTROL_DONE:
      ByteQueuePull(&ctrl_out, cosize);
      return TRUE;

    case BLUETOOTH_EVENT_ATTACHED:
    case BLUETOOTH_EVENT_DETACHED:
      return TRUE;

    case BLUETOOTH_EVENT_READ_BULK_DONE:
      if (status == USB_SUCCESS) {
        if (size) {
          if (packet_handler) {
            packet_handler(HCI_ACL_DATA_PACKET, data, size);
          }
        }
      } else {
        log_printf("Read bulk failure");
      }
      return TRUE;

    case BLUETOOTH_EVENT_READ_INTERRUPT_DONE:
      if (status == USB_SUCCESS) {
        if (size) {
          if (packet_handler) {
            packet_handler(HCI_EVENT_PACKET, data, size);
          }
        }
      } else {
        log_printf("Read bulk failure");
      }
      return TRUE;

    default:
      return FALSE;
  }
}