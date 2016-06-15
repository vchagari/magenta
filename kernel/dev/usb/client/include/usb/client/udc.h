// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009, Google Inc.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#ifndef __DEV_UDC_H
#define __DEV_UDC_H

#include <stdint.h>

typedef struct udc_request udc_request_t;
typedef struct udc_gadget udc_gadget_t;
typedef struct udc_device udc_device_t;

/* endpoints are opaque handles specific to the particular device controller */
typedef struct udc_endpoint udc_endpoint_t;

/* USB Device Controller Transfer Request */
struct udc_request {
    void *buffer;
    unsigned length;
    void (*complete)(udc_request_t *req, unsigned actual, int status);
    void *context;
};

udc_request_t *udc_request_alloc(void);
void udc_request_free(udc_request_t *req);
int udc_request_queue(udc_endpoint_t *ept, udc_request_t *req);
int udc_request_cancel(udc_endpoint_t *ept, udc_request_t *req);

#define UDC_BULK_IN    0x82
#define UDC_BULK_OUT   0x02

udc_endpoint_t *udc_endpoint_alloc(unsigned type, unsigned maxpkt);
void udc_endpoint_free(udc_endpoint_t *ept);

#define UDC_EVENT_ONLINE    1
#define UDC_EVENT_OFFLINE   2

struct udc_gadget {
    void (*notify)(udc_gadget_t *gadget, unsigned event);
    void *context;

    struct udc_gadget *next; // do not modify

    uint8_t ifc_class;
    uint8_t ifc_subclass;
    uint8_t ifc_protocol;
    uint8_t ifc_endpoints;
    const char *ifc_string;
    unsigned flags;

    udc_endpoint_t **ept;
};

struct udc_device {
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t version_id;

    const char *manufacturer;
    const char *product;
    const char *serialno;
};

int udc_init(udc_device_t *devinfo);
int udc_register_gadget(udc_gadget_t *gadget);
int udc_start(void);
int udc_stop(void);

#endif