Introduction
------------



Architecture
------------

Fundamentally, NetFR is designed to be a simple and fast way to interact
with network hardware. While it is called NetFR for **Network Frame
Relay**, and it is designed with the express purpose of frame relay in
mind, it does not actually have the concept of "frames" per se; it is
designed to send large buffers of data reliably with RDMA in order and
with low latency. In the context of the Looking Glass project, this
means frame and cursor data.

It is intentionally designed this way to reduce the amount of
modifications which must be made to Looking Glass, as well as reduce the
complexity of the NetFR implementation itself. 

When we compare NetFR to our previous system, the Telescope Remote
Framebuffer Library (libtrf), we had to reimplement a lot of the Looking
Glass KVMFR functionality. Instead, NetFR does not attempt to
reimplement KVMFR functionality; it pushes the job of handling the
incoming data to the application itself. There are a few reasons for
this:

- It reduces the amount of code which must be written to implement NetFR.
- It allows for the use of the same KVMFR code across both the shared
  memory and network implementation.
- NetFR does not need to be aware of the contents of the data being sent, which
  allows for much easier integration with Looking Glass. In particular, updates
  that change the format of KVMFRFrame and similar structures do not need to be
  reflected in NetFR, as they were never parsed in the first place.
- There is no translation layer between the network and shared memory
  implementation, potentially reducing computational overhead and latency.

A brief introduction to RDMA
----------------------------

Remote Direct Memory Access (RDMA) is a technology that allows for network
adapters to read and write to the memory of another machine without involving
the host processor. Contrary to what the name implies, RDMA is not actually
direct memory access in the traditional sense, as you cannot simply map a remote
machine's memory into your program's address space. RDMA libraries allow for the
network cards to read and write to memory buffers that are registered with the
RDMA library, which are then copied directly to userspace buffers without CPU
involvement.

The main benefits of RDMA for this project can be boiled down to three main
points: low latency, high throughput, and low CPU usage. Particularly points 2
and 3 are important when sending large amounts of data, because the CPU is
burdened with less work; the CPU only needs to set up the RDMA operation and go
back to doing higher priority tasks. With the right hardware and software
configuration, you can transfer hundreds of gigabits per second using RDMA with
near-zero CPU usage.

The Libfabric Library
~~~~~~~~~~~~~~~~~~~~~

Telescope uses the Libfabric library to interface with RDMA hardware, as it
supports a wide variety of RDMA-enabled hardware across different operating
systems, while providing a (mostly) unified API for all of them. The effect is
that the NetFR low-level communication code runs on Windows, Linux, and macOS
without significant per-platform changes required. The prerequisite is that the
version of the wire protocol in use matches.

**RDMA** accelerated transports using the Verbs library are available on Windows
and Linux and are interoperable so long as all machines involved use
little-endian and support the same RDMA technology. For instance, a RoCEv2
network adapter will work with another RoCEv2 network adapter, but NetFR will
have to fall back to TCP if one side of the connection supports RoCEv2 and the
other side only supports iWARP. The penalty can be mitigated by using software
RDMA emulation, such as RXE or SoftiWARP, if available, which ensures the side
with RDMA capabilities can still benefit from hardware acceleration.

**TCP** transports are available on Windows, Linux, and macOS. The transport is
universally interoperable.

Data Transfer API
-----------------

NetFR abstracts RDMA operations into a high-level set of functions required to
enable Looking Glass over local networks. The API is designed to mimic the LGMP
API, with some modifications to better fit within the constraints of RDMA.

Two modes of data transfer are supported.

The ``nfr*SendData`` and ``nfr*RecvData`` functions perform reliable datagram
send and receive operations. These are used to support the similarly named
functions in LGMP used for cursor metadata messages. However, since the memory
buffers on the two machines are independent, this channel is also used to
exchange data on the required buffer sizes so they can be allocated. Other data,
such as pointers and remote access keys to make RDMA operations possible, is
transferred automatically in the background by NetFR when users register memory
buffers.

The ``nfrHostWriteBuffer`` operation performs a copy from a host buffer, which
can be controlled by the API user, to an available remote client buffer, which
is automatically selected by the API depending on the client's state. This is
used for frame data as well as cursor texture data.

NetFR guarantees the completion ordering of messages sent over this API, with
the limitation that if multiple clients are connected, messages may be delivered
out of order between clients. This is a limitation of the RDMA protocol itself,
as strong ordering is only guaranteed between a single pair of endpoints (QPs).

Because of this guarantee, care must be taken to order messages on the user side
to ensure head of line blocking does not occur. This is why NetFR provides two
independent channels-- otherwise, if a cursor message was sent after a frame
message, the cursor message would be delayed until the frame message was
delivered, which causes a delay of up to one frame period.

Unlike the ``libtrf`` API, no abstraction for frame or cursor data is provided.
The upper layer protocol must add its own metadata in-band as part of the data
payload to ensure that the client can correctly interpret what it receives.

Data Transfer Internals
-----------------------

Internally, NetFR stores and keeps track of in-flight messages using a context
array. Each in-flight message is exclusively assigned a context for the
operation's lifetime, which is a structure that contains details about the
message, the RDMA operation being performed, and a callback to be invoked when
the operation completes.

RDMA operations in Libfabric and Verbs provide the user with a context
parameter, which is a 64-bit value. This can be used to store a pointer to any
user-defined location, which is what NetFR uses to keep track of its operations.

These callbacks are used to simplify the core NetFR code by allowing messages to
be correctly routed without the core context manager code needing to interpret
the message contents. This allows for granular per-message handling, and for the
same core context manager code to be shared between the client, host, and all
data transfer modes. For this use case, the overhead of this implementation is
not significant enough to warrant more complexity.

.. code-block:: c

  struct NFRFabricContext
  {
    struct NFRResource     * parentResource;
    uint8_t                  state;
    struct NFR_CallbackInfo  cbInfo;
    struct NFRDataSlot     * slot;
  };

As for the message payload, each context contains a buffer pointer which is used
to store or receive messages depending on the operation. This is allocated once
as a single contiguous block sliced into slots and shared across all contexts,
as registering and de-registering memory for RDMA is a relatively expensive and
slow operation. There is also a limited number of memory regions supported in
hardware by network adapters.

.. code-block:: c

  struct NFRDataSlot
  {
    uint32_t         msgSerial;
    uint32_t         channelSerial;
    alignas(16) char data[0];
  };

Credit System
~~~~~~~~~~~~~

NetFR uses a credit system to prevent buffer exhaustion. A fixed number of
transfer credits are allocated to the server and client, and each time a message
is sent, a credit is consumed. When the other side receives the message and
processes it, it sends an acknowledgement and a credit is returned. If the
credit count reaches zero, any further message operations will be blocked until
the other side sends acknowledgements.

Send Operations
~~~~~~~~~~~~~~~

When a message is sent, the context is allocated and the message is copied into
the data slot. The message is then sent using ``fi_send``. Once the operation
completes, the context is made available for reuse.

Receive Operations
~~~~~~~~~~~~~~~~~~

NetFR handles message receives internally by posting a buffer for every
available context dedicated to receives. The ``fi_recv`` operation is called
with this context, and when a message is received, the data slot state will
change to indicate that the message is ready to be read. A unique incrementing
serial number is used to order messages, because while messages can arrive in
order, they are not laid out in order in the context array.

Write Operations
~~~~~~~~~~~~~~~~

RDMA write operations are known as one-sided operations. This means that the
remote side has its memory written to and read from, but the remote side is not
informed of this change. Therefore, NetFR remote writes are actually handled as
two separate operations: an RDMA write and message send operation. The write
operation performs the copying of the bulk data, while the send operation is
used to notify the client that the data is ready to be read out of the buffer
which has just been written to.

As RDMA guarantees completion ordering, we minimize the latency of the
notification by calling both operations in sequence. ``fi_write`` is called,
followed by an ``fi_send`` operation. When the write operation completes, the
send operation is already in the queue, and thus the notification is sent
without any host-side processing delay, minimizing latency.

The host API user provides the local buffer to be used, but has no control over
which remote buffer in which the message will be placed. The remote buffer
details are synchronized using internal functions whenever a client registers a
memory buffer, and NetFR will select a suitable buffer to write to depending on
the state of the client and the availability of buffers. The host will search
through all available client memory buffers available and select the smallest
buffer large enough to hold the requested payload size. If no buffer is found,
the host is informed of this.

Host Receives
^^^^^^^^^^^^^

The NetFR host only supports receiving data through the regular message channel
and not the RDMA write method. When the host calls ``nfrHostRecvData``, the
context array is scanned and the lowest message serial number is found, its data
are copied out, and the context is made available for reuse. As the amount of
data being received by the host is relatively low in comparison to the frame
data, the slight latency and memory usage penalty is outweighed by the
simplicitly of the implementation. 

Client Receives
^^^^^^^^^^^^^^^

The client receives events through the ``nfrClientProcess`` call, which performs
background event processing and other ancillary tasks. Messages can either be
received through RDMA writes with a confirmation through the message channel, or
regular message sends. 


Regular RDMA transports do not usually guarantee that the order of messages sent
on different connections are maintained relative to each other. However, NetFR
adds additional metadata and thereby guarantees a total order between these two
messaging modes, which are implemented using two different queue pairs. The
context array is scanned and the lowest message serial number among both of the
transfer modes is found and returned.