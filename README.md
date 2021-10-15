# libvideo-scale - Video scaling library

_libvideo-scale_ is a C library to handle the scaling of video frames on
various platforms with a common API.

The library uses hardware-accelerated scaling when available.

## Implementations

The following implementations are available:

* _none for now_

The application can force using a specific implementation or let the library
decide according to what is supported by the platform.

## Dependencies

The library depends on the following Alchemy modules:

* libulog
* libfutils
* libpomp
* libvideo-buffers

## Building

Building is activated by enabling _libvideo-scale_ in the Alchemy build
configuration.

## Operation

Operations are asynchronous: the application pushes buffers to scale in the
input queue and is notified of scaled frames through a callback function.

Some scalers need input buffers to come from their own buffer pools; when the
input buffer pool returned by the library is not _NULL_ it must be used and
input buffers cannot be shared with other video pipeline elements.

### Threading model

The library is designed to run on a _libpomp_ event loop (_pomp_loop_, see
_libpomp_ documentation). All API functions must be called from the _pomp_loop_
thread. All callback functions (frame_output, flush or stop) are called from
the _pomp_loop_ thread.
