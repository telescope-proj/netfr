# Telescope Network Frame Relay System (NetFR)

NetFR is a light RDMA-enabled messaging protocol designed to be tightly
integrated with Looking Glass. It is designed as an equivalent to LGMP to remove
the need (and overhead) of the Looking Glass Proxy project.

The Looking Glass Proxy, our original project, is difficult to maintain due to
limited resources, the need to replicate the entire KVMFR/LGMP protocol on both
ends of the connection, its support for texture compression, as well as the size
of the codebase as a result of the previous requirement. The code base is also
quite convoluted as a result of it being written quickly to meet a project
deadline.

NetFR + Looking Glass plans to have the following features:
- Native Windows support (requires Windows 10/11 Pro **for Workstations** or
  Server)
- Direct integration into Looking Glass, with the same KVMFR codebase being
  shared between LGMP and NetFR

Some features which are quite complex to implement and/or are unlikely to be
used are slated to be removed:
- Support for texture compression
- Support for automatic protocol negotiation

The goal with LGProxy is to gradually phase it out, integrating its features
either directly into Looking Glass or NetFR. We would like to gather feedback on
what users want to see from such a solution.

As for keyboard and mouse input, we are evaluating the Winspice project, and are
planning on a minimal version that only supports the features required by
Looking Glass.

I say "we", but at the moment there is only one maintainer. Pull requests are
appreciated...

## Copyright

Copyright (c) 2023 - 2024 Tim Dettmar  
This software is licensed under GPL v2 (or later).  
See the file LICENSE for the full license text.