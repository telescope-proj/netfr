/*
 * Telescope Network Frame Relay System
 *
 * Copyright (c) 2023-2024 Tim Dettmar
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* This file demonstrates NetFR messaging and RDMA write operations, as well
   as its callback system. 
*/

#include "netfr/netfr_host.h"

#include <stdio.h>

void sleepMs(int ms)
{
#ifdef _WIN32
  Sleep(ms);
#else
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000;
  nanosleep(&ts, NULL);
#endif
}

unsigned long getTimeMsec(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void calcDataRate(const void ** udata)
{
  unsigned long startTime = (unsigned long) (uintptr_t) udata[0];
  unsigned long len       = (unsigned long) (uintptr_t) udata[1];
  uint8_t * busyFlag      = (uint8_t *) udata[2];
  unsigned long tdiff     = getTimeMsec() - startTime;
  double rate             = (double) len / (double) tdiff / 1048576.0 * 8.0;
  printf("Data rate: %.2f Gbit/s\n", rate);
  *busyFlag = 0;
}

int main(int argc, char ** argv)
{
  if (argc < 4)
  {
    fprintf(stderr, "Usage: %s <transport> <ip> <port>\n", argv[0]);
    return -EINVAL;
  }
  
  nfrSetLogLevel(NFR_LOG_LEVEL_DEBUG);

  struct NFRInitOpts opts;
  memset(&opts, 0, sizeof(opts));
  
  uint8_t transport = strcmp(argv[1], "tcp") == 0 ? NFR_TRANSPORT_TCP 
                                                  : NFR_TRANSPORT_RDMA;

  opts.addrs[0].sin_addr.s_addr = inet_addr(argv[2]);
  opts.addrs[0].sin_port        = htons(atoi(argv[3]));
  opts.addrs[0].sin_family      = AF_INET;
  opts.transportTypes[0]        = transport;
  
  opts.addrs[1].sin_addr.s_addr = inet_addr(argv[2]);
  opts.addrs[1].sin_port        = htons(atoi(argv[3]) + 1);
  opts.addrs[1].sin_family      = AF_INET;
  opts.transportTypes[1]        = transport;

  opts.apiVersion = FI_VERSION(1, 18);

  PNFRHost host;
  int ret = nfrHostInit(&opts, &host);
  if (ret < 0)
  {
    fprintf(stderr, "Failed to initialize host: %d\n", ret);
    return ret;
  }

  const size_t frameMemSize = 1048576 * 128;

  void * mem = aligned_alloc(4096, frameMemSize);
  if (!mem)
  {
    fprintf(stderr, "Failed to allocate memory\n");
    goto cleanup;
  }

  PNFRMemory frameMem = nfrHostAttachMemory(host, mem, frameMemSize, 0);
  if (!frameMem)
  {
    fprintf(stderr, "Failed to attach memory\n");
    goto cleanup;
  }

  uint8_t busyWriting = 0;

  char msgBuf[256];
  memset(msgBuf, 0, sizeof(msgBuf));

  while (1)
  {
    ret = nfrHostProcess(host);
    if (ret < 0)
    {
      switch (ret)
      {
        case -ENOTCONN:
        case -EAGAIN:
          sleepMs(100);
          continue;
        case -ECONNREFUSED:
          fprintf(stderr, "Connection refused\n");
          goto cleanup;
        default:
          fprintf(stderr, "Failed to perform host tasks: %d\n", ret);
          goto cleanup;
      }
    }

    int nClients = nfrHostClientsConnected(host, 0);
    if (!nClients)
    {
      sleepMs(100);
      continue;
    }

    if (!busyWriting)
    {
      struct NFRCallbackInfo cbInfo;
      cbInfo.callback = calcDataRate;
      cbInfo.uData[0] = (void *) (uintptr_t) getTimeMsec();
      cbInfo.uData[1] = (void *) (uintptr_t) frameMemSize;
      cbInfo.uData[2] = &busyWriting;

      ret = nfrHostWriteBuffer(frameMem, 0, 0, frameMemSize, &cbInfo);
      if (ret < 0 && ret != -ENOBUFS && ret != -EAGAIN)
      {
        fprintf(stderr, "Failed to write buffer: %d\n", ret);
        goto cleanup;
      }
      
      if (ret >= 0)
      {
        printf("Writing buffer\n");
        busyWriting = 1;
      }
    }

    uint32_t len = sizeof(msgBuf); 
    uint64_t udata = 0;
    ret = nfrHostReadData(host, 1, msgBuf, &len, &udata);
    if (ret < 0 && ret != -EAGAIN)
    {
      fprintf(stderr, "Failed to read data: %d\n", ret);
      goto cleanup;
    }

    sleepMs(1);
  }

cleanup:
  nfrFreeMemory(&frameMem);
  free(mem);
  nfrHostFree(&host);
  return ret;
}