/*
 * Copyright (c) 2013, North Carolina State University Aerial Robotics Club
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the North Carolina State University Aerial Robotics Club
 *       nor the names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <Python.h>
#include <structmember.h>
#include <sys/queue.h>
#include <libsvgige/svgige.h>
#include "svs_core.h"

/* Camera released when heartbeat missing this long (ms) */
#define HEARTBEAT_TIMEOUT  3000

/* Packet bookkeeping and resend begin after this timeout (ms) */
#define PACKET_RESEND_TIMEOUT   1000

static void svs_core_Camera_dealloc(svs_core_Camera *self);
static int svs_core_Camera_init(svs_core_Camera *self, PyObject *args, PyObject *kwds);

PyMemberDef svs_core_Camera_members[] = {
    {NULL}
};

PyTypeObject svs_core_CameraType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "svs_core.Camera",              /* tp_name */
    sizeof(svs_core_Camera),        /* tp_basicsize */
    0,                         /* tp_itemsize */
    (destructor) svs_core_Camera_dealloc,        /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_reserved */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash  */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,   /* tp_flags */
    "Camera(ip, source_ip, [buffer_count=10, packet_size=9000,\n"
    "       queue_length=50]) -> Camera object\n\n"
    "Wrapper object for the SVS-VISTEK SVGigE SDK.  Provides a simpler interface\n"
    "to use for controlling cameras.  Exposes various camera settings as\n"
    "attributes, and provides methods for capturing images from the camera.\n\n"
    "Arguments:\n"
    "   ip: IP address of camera to connect to.\n"
    "   source_ip: IP address of local interface used for connection.\n"
    "   buffer_count (optional): Number of internal buffers for SVGigE\n"
    "       streaming channels.\n"
    "   packet_size (optional): MTU packet size.\n"
    "   queue_length (optional): Maximum number of images to queue for\n"
    "       return by next().  Once this limit is reached, old images are\n"
    "       dropped from the queue.  A length of zero allows infinite\n"
    "       images to queue.\n",    /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    svs_core_Camera_methods,        /* tp_methods */
    svs_core_Camera_members,        /* tp_members */
    svs_core_Camera_getseters,      /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)svs_core_Camera_init, /* tp_init */
    0,                         /* tp_alloc */
    0,                         /* tp_new */
};

static void svs_core_Camera_dealloc(svs_core_Camera *self) {
    /* Use ready flag to determine state of readiness to deallocate */
    switch (self->ready) {
    case READY:
        closeStream(self->stream);
    case NAME_ALLOCATED:
        Py_DECREF(self->name);
    case CONNECTED:
        closeCamera(self->handle);
        break;
    }

    Py_TYPE(self)->tp_free((PyObject*)self);
}

static int svs_core_Camera_init(svs_core_Camera *self, PyObject *args, PyObject *kwds) {
    static char *kwlist[] = {
        "ip", "source_ip", "buffer_count", "packet_size", "queue_length", NULL
    };

    const char *ip = NULL;
    const char *source_ip = NULL;
    unsigned int buffer_count = 10;
    unsigned int packet_size = 9000;
    uint32_t ip_num, source_ip_num;
    char *manufacturer, *model;
    int ret;

    self->main_thread = PyGILState_GetThisThreadState();
    self->ready = NOT_READY;
    TAILQ_INIT(&self->images);
    self->images_length = 0;
    self->images_max = 50;

    /*
     * This means the definition is:
     * def __init__(self, ip, source_ip, buffer_count=10, packet_size=9000,
     *              queue_length=50):
     */
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ss|III", kwlist,
                &ip, &source_ip, &buffer_count, &packet_size,
                &self->images_max)) {
        return -1;
    }

    ip_num = ip_string_to_int(ip);
    source_ip_num = ip_string_to_int(source_ip);

    ret = openCamera(&self->handle, ip_num, source_ip_num, HEARTBEAT_TIMEOUT, MULTICAST_MODE_NONE);
    if (ret != SVGigE_SUCCESS) {
        raise_general_error(ret);
        return -1;
    }

    self->ready = CONNECTED;

    manufacturer = strdup(Camera_getManufacturerName(self->handle));
    if (!manufacturer) {
        PyErr_SetString(PyExc_MemoryError, "Unable to allocate name");
        return -1;
    }

    model = strdup(Camera_getModelName(self->handle));
    if (!model) {
        free(manufacturer);
        PyErr_SetString(PyExc_MemoryError, "Unable to allocate name");
        return -1;
    }

    self->name = PyBytes_FromFormat("%s %s", manufacturer, model);
    free(manufacturer);
    free(model);
    if (!self->name) {
        return -1;
    }

    self->ready = NAME_ALLOCATED;

    ret = Camera_getTimestampTickFrequency(self->handle, &self->tick_frequency);
    if (ret != SVGigE_SUCCESS) {
        raise_general_error(ret);
        return -1;
    }

    ret = Camera_getImagerWidth(self->handle, &self->width);
    if (ret != SVGigE_SUCCESS) {
        raise_general_error(ret);
        return -1;
    }

    ret = Camera_getImagerHeight(self->handle, &self->height);
    if (ret != SVGigE_SUCCESS) {
        raise_general_error(ret);
        return -1;
    }

    /* 12-bit pixel depth */
    self->depth = 12;
    ret = Camera_setPixelDepth(self->handle, SVGIGE_PIXEL_DEPTH_12);
    if (ret != SVGigE_SUCCESS) {
        raise_general_error(ret);
        return -1;
    }

    /* Image buffer size in bytes */
    ret = Camera_getBufferSize(self->handle, &self->buffer_size);
    if (ret != SVGigE_SUCCESS) {
        raise_general_error(ret);
        return -1;
    }

    /* Open stream */
    ret = addStream(self->handle, &self->stream, &self->stream_ip,
                    &self->stream_port, self->buffer_size, buffer_count,
                    packet_size, PACKET_RESEND_TIMEOUT,
                    svs_core_Camera_stream_callback, self);
    if (ret != SVGigE_SUCCESS) {
        raise_general_error(ret);
        return -1;
    }

    ret = enableStream(self->stream, 1);
    if (ret != SVGigE_SUCCESS) {
        raise_general_error(ret);
        return -1;
    }

    self->ready = READY;

    return 0;
}

void raise_general_error(int error) {
    const char *message;

    message = getErrorMessage(error);
    if (!message) {
        message = "Unknown error";
    }

    PyErr_Format(SVSError, "SVGigE SDK error %d: %s", error, message);
}
