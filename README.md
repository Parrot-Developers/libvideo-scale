# libvideo-scale - Video scaling library

_libvideo-scale_ is a C library to handle the scaling of video frames on
various platforms with a common API.

The library uses hardware-accelerated scaling when available.

## Implementations

The following implementations are available:

* LibYUV (software scaling)

The application can force using a specific implementation or let the library
decide according to what is supported by the platform.

## Dependencies

The library depends on the following Alchemy modules:

* libfutils
* libmedia-buffers
* libpomp
* libulog
* libvideo-defs
* libvideo-scale-core
* (optional) libmedia-buffers-memory (for LibYUV support)
* (optional) libmedia-buffers-memory-generic (for LibYUV support)
* (optional) libvideo-metadata (for LibYUV support)
* (optional) libyuv (for LibYUV support)

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

## Testing

The library can be tested using the provided _vscale_ command-line tool which
takes as input a raw YUV file and outputs a scaled YUV file.

To build the tool, enable _vscale_ in the Alchemy build configuration.

For a list of available options, run

    $ vscale -h
