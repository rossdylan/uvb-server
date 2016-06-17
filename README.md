UVB - Ultimate Victory Battle
-----------------------------
[![Build Status](https://travis-ci.org/rossdylan/uvb-server.svg?branch=master)](https://travis-ci.org/rossdylan/uvb-server)

Ultimate Victory Battle is a game created by Computer Science House. The basic
premise is this. There is a server, every one else tries to crash the server.
There are also counters and statistics, but thats just to rank how hard each
player is hitting the server.

Most people end up writing a client which floods a server with traffic. I
thought it would be more fun to write a server which can handle the flood of
traffic. That server is what I have attempted to build here.


Revision 4 - Changelog
-------------------------
1. Store counters in LMDB
    In the LMDB branch counters are persisted to disk using LMDB. In order
    to achieve reasonable performance we use the MDB_WRITEMAP and MDB_MAPASYNC
    options. This sacrifices write durability for speed, which in the case of
    UVB is fine (for now).

2. Modified buffer_append and added buffer_fast_clear.
    These changes make it possible to avoid reallocing and memseting the buffer
    memory during clear, we still use the default realloc/memset way, but for
    printing the status page we avoid that and just use buffer_fast_clear which
    resets the buffer index to 0; These changes will probably be propogated out
    the master soon.

3. Filter out non azAZ09 characters, limit names to 15 characters
    Because people are shitlords

4. Pipelined requests.
    Previously there was a 1-1 mapping between tcp stream and request. So when
    a single request was finished, we clean everything up, and close the
    connection. Now we don't assume this, and instead just keep handling
    http requests from a connection until it stops.

5. Use SO_REUSEPORT
    Previous connections were loadbalanced acrossed threads via EPOLL_ONESHOT
    and a shared epoll fd. While this works fine, we might be able to get more
    performance if SO_REUSEPORT is used since it allows multiple threads to
    bind on the same port while the kernel balances connections acrossed them.
    This way we hopefully get better cache locality because each connection is
    pinned to a single thread which is pinned to a single cpu.

6. Each thread is pinned to a single CPU.
    Connections are no longer shared among threads so if we pin threads to CPUs
    we might improve performance.
    

People
------
* Ross Delinger
* Rob Glossop
