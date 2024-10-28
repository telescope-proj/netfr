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

#include "netfr/netfr_client.h"
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

int main(int argc, char ** argv)
{
  if (argc < 6)
  {
    fprintf(stderr, 
            "Usage: %s <transport> <ip> <port> <remote_ip> <remote_port> "
            " [log_level]\n",
            argv[0]);
    return -EINVAL;
  }

  nfrSetLogLevel(NFR_LOG_LEVEL_DEBUG);

  struct NFRInitOpts opts;
  memset(&opts, 0, sizeof(opts));
  
  printf("Connecting from %s:%s to %s:%s\n", argv[2], argv[3], argv[4], argv[5]);

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

  struct NFRInitOpts remoteOpts;
  memset(&remoteOpts, 0, sizeof(remoteOpts));

  remoteOpts.addrs[0].sin_addr.s_addr = inet_addr(argv[4]);
  remoteOpts.addrs[0].sin_port        = htons(atoi(argv[5]));
  remoteOpts.addrs[0].sin_family      = AF_INET;
  remoteOpts.transportTypes[0]        = transport;

  remoteOpts.addrs[1].sin_addr.s_addr = inet_addr(argv[4]);
  remoteOpts.addrs[1].sin_port        = htons(atoi(argv[5]) + 1);
  remoteOpts.addrs[1].sin_family      = AF_INET;
  remoteOpts.transportTypes[1]        = transport;

  remoteOpts.apiVersion = FI_VERSION(1, 18);

  PNFRClient client;
  int ret = nfrClientInit(&opts, &remoteOpts, &client);
  if (ret < 0)
  {
    fprintf(stderr, "Failed to initialize client: %d\n", ret);
    return ret;
  }

  for (int i = 0; i < 300; ++i)
  {
    ret = nfrClientSessionInit(client);
    if (ret < 0 && ret != -EAGAIN)
    {
      fprintf(stderr, "Failed to initialize session: %d\n", ret);
      nfrClientFree(&client);
      return ret;
    }
    if (ret == -EAGAIN)
    {
      sleepMs(100);
    }
  }

  if (ret != 0)
  {
    fprintf(stderr, "Failed to connect to server: %d\n", ret);
    nfrClientFree(&client);
    return ret;
  }

  printf("Client connected\n");

  const size_t frameMemSize = 1048576 * 128;

  PNFRMemory mem = nfrClientAttachMemory(client, 0, frameMemSize, 0);
  if (!mem)
  {
    fprintf(stderr, "Failed to attach memory\n");
    return ENOMEM;
  }

  uint64_t prevSendTime = 0;

  while (1)
  {
    struct NFRClientEvent evt;
    memset(&evt, 0, sizeof(evt));
    ret = nfrClientProcess(client, -1, &evt);
    if (ret < 0)
    {
      switch (ret)
      {
        case -EAGAIN:
          sleepMs(100);
          continue;
        case -ECONNREFUSED:
          fprintf(stderr, "Connection refused\n");
          return ret;
        default:
          fprintf(stderr, "Failed to perform client tasks: %d\n", ret);
          return ret;
      }
    }

    if (ret == 0)
    {
      sleepMs(1);
      continue;
    }

    switch (evt.type)
    {
      case NFR_CLIENT_EVENT_MEM_WRITE:
        printf("Received memory write event\n");
        nfrAckBuffer(mem);
        break;
      case NFR_CLIENT_EVENT_DATA:
        printf("Received data: %s\n", evt.inlineData);
        break;
      default:
        break;
    }

    if (getTimeMsec() - prevSendTime > 1000)
    {
      const char msg[] = "Hello server";
      ret = nfrClientSendData(client, 1, msg, sizeof(msg));
      if (ret < 0)
      {
        fprintf(stderr, "Failed to send data: %d\n", ret);
        goto cleanup;
      }
      prevSendTime = getTimeMsec();
    }
  }

cleanup:
  nfrClientFree(&client);
  return ret;
}