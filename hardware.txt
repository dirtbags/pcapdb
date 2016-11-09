Hardware Requirements
---------------------

Hardware requirements depend greatly on the peak time capture rates of the 
network being monitored.

Capture rates listed below are for the peak-time-average traffic. For 
example, a deployment on a 10 Gb link sees an average of 3 Gb/s of traffic
during the busiest part of the work week, though it does have momentary spikes
of up to 10 Gb/s of traffic. For our purposes, this is a 3Gb/s network. 

Core System
-----------
 - 6 dedicated cores for the OS and search, plus an and additional
   core for every 1 Gb/s of capture. 
   - Disabling Hyperthreading is recommended.
 - 128 GB system memory, minimum (ECC preferred)

Capture Card
------------

While PcapDB can technically run (in libpcap mode) using just about any network card, 
for best performance you'll want a card compatible with the PFring ZC library and
it's custom drivers. Currently, the Intel X520 line of server adaptors is the most 
affordable option at around $300.  Myricom cards are another, albeit more expensive,
option.

Storage
-------
 - Physical 'Capture Disks' scaled to the raw capture history needs of the site.
  - A 'Capture Disk' is logical unit dedicated entirely to storing captured packets.
  - Capture disks can be essentially any logical disk unit, as long as it can
    offer sustained writes proportional to twice its share of the capture rate.
    A single 9 disk RAID 5 (using 7500 RPM Nearline SAS disks) can easily 
    handle 1 Gb/s.
  - For each day of capture history, you typically need around
    5.5 TB of disk per 1 Gb/s of capture rate.
  - The physical disks may be directly attached to the system, as components 
    of a JBOD (software RAIDS can be managed via the PcapDB interface),
    or as separate logical units.  Capture will be balanced across
    all capture storage devices according to size.
 - A separate 'Index' device. A single 7500 RPM Nearline SAS disk, or
   a pair in RAID 1, works fine. You typically need about 1% of your
   total capture disk as index disk. 
 - Disks as needed for the OS.

