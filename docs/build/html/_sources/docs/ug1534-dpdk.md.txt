# DPDK

DPDK compilation steps

1. Download the dpdk git repositories from [http://www.dpdk.org/browse](http://www.dpdk.org/browse).

2. Extract the downloaded dpdk files and download the required packages:

   ```bash
   apt-get install libnuma-dev liblua5.3-dev libpcap-dev [for ubuntu]
   ```

3. Create the DPDK build tree:

   Follow the steps mentioned at [Compilation Steps](http://doc.dpdk.org/guides/linux_gsg/build_dpdk.html) to do DPDK compilation.

4. Post successful compilation, allocate hugepage and bind the PCI device to run testpmd:

```bash
   a. `mkdir /mnt/huge`

   b. `mount -t hugetlbfs nodev /mnt/huge`
```

      You can use VFIO or UIO [uio_pci_generic or igb_uio]. Refer to the following link to install the Linux drivers for DPDK: [https://doc.dpdk.org/guides/linux_gsg/linux_drivers.html](https://doc.dpdk.org/guides/linux_gsg/linux_drivers.html).

      Added below the example commands with igb_uio driver. For the igb_uio driver, the path for dpdk-kmod can be found at [http://git.dpdk.org/dpdk-kmods](http://git.dpdk.org/dpdk-kmods). After downloading, go to the following path and compile:

      ```
      cd <dpdk-kmod>/src
      ```

   c. 
   ```
   `modprobe uio`
   ```
   d. 
   ```
   `insmod /<dpdk-kmod>/src/igb_uio.ko`
   ```
   e. Ensure that the VF interface is down and unbound from sfc before binding it to the Linux driver:

      ```
      ifconfig <U25eth0_VF> down
      . /usertools/dpdk-devbind.py -u <pci_id> [inside dpdk directory]
      ```

   f. Run the following command:

      ```
      . /usertools/dpdk-devbind.py -b igb_uio <pci_id> [inside dpdk directory]
      ```

      For example:

      ```
      ./usertools/dpdk-devbind.py -b igb_uio af:00.0
      ```

5. Command to run testpmd:

   Refer to [https://doc.dpdk.org/guides/testpmd_app_ug/run_app.html](https://doc.dpdk.org/guides/testpmd_app_ug/run_app.html) for more information about the testpmd runtime command. The following is an example command to run testpmd:

   ***Note*:** Make sure the dpdk-testpmd for specific PCI devices is running on the same NUMA node.

   Use the following command to get the numa node for a specific PCI device.

   ```
   cat /sys/bus/pci/devices/0000\:<pci device id>/numa_node
   ```

   The cores for the specific numa node could be found using the following command:

   ```
   lscpu | grep NUMA
   ```

   For example, `lscpu | grep NUMA`.

   ```
   NUMA node(s): 2
   NUMA node0 CPU(s): 0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30
   NUMA node1 CPU(s): 1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31
   ```
