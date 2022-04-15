# APPENDIX B VM Installation

***Note*:** This steps mentioned in this section needs to be performed only for VM cases. The following configuration steps are for Ubuntu OS.

Installation of libraries and dependencies is required for running VMs

1. Install qemu (IN HOST):

   ```
   sudo apt-get install qemu-system-x86
   ```

2. Update the following command in the path `/etc/sysctl.conf` for huge page configuration:

   ```
   sudo vim /etc/sysctl.conf
   kernel.shmmax = 25769803776 [ it allocates 24G 1G hugepages ]
   vm.hugetlb_shm_group = 0
   ```

   [//]: # (sometimes, "hugepage" is used. check for consistency, such as step 5 below)

3. Update Host Grub with the following command:

   ```
   sudo vim /etc/default/grub
   GRUB_CMDLINE_LINUX_DEFAULT="quiet splash hugepagesz=1GB hugepages=24
   default_hugepagesz=1GB iommu=pt intel_iommu=on pci=realloc"
   ```

4. After doing changes in Host Grub, update the Grub using the following command:

   ```
   sudo update-grub
   sudo reboot
   ```

5. Allocate the hugepage of size 8 Gb for a VM using this command:

   ```
   mkdir /mnt/huge_vm
   mount -t hugetlbfs -o size=8G none /mnt/huge_vm
   ```

6. Insert the VFIO driver using this command:

   ```
   sudo modprobe vfio-pci
   ```

   ***Note*:** Make sure the next step is done only after the VF representor interfaces are added to the OVS bridge.

7. Unbind the VF PCIe® id from sfc before binding to the vfio-pci driver.

   ***Note*:** The VF PCIe device ID can be listed with the `lspci -d 1924:1b03` command. An example of the device ID is *af:00.2 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (Virtual Function) (rev 01)*.

   ```
   echo '0000:<VF PCIe id>' > /sys/bus/pci/drivers/sfc/unbind
   ```

   For example:

   ```
   echo '0000:af:00.2' > /sys/bus/pci/drivers/sfc/unbind
   ```

8. Bind the Vendor ID and device ID of the U25N X2 to VFIO driver:

   ```
   echo "1924 1b03" > /sys/bus/pci/drivers/vfio-pci/new_id
   ```

9. Bind the respective PCIe device ID to the driver:

   ```
   echo '0000:<VF PCIe id>' > /sys/bus/pci/drivers/vfio-pci/bind
   ```

   For example:

   ```
   echo '0000:af:00.2' > /sys/bus/pci/drivers/vfio-pci/bind
   ```

10. QEMU command for launching the VMs from terminal are:

    ```
    qemu-system-x86_64 -cpu host -enable-kvm -m <Memory> -mem-prealloc -mempath
    /mnt/huge_vm -smp sockets=<cpu_socket>,cores=<no_of_cores> -hda
    <path_to_qcow2_imagr> -device e1000,netdev=net0 -netdev
    user,id=net0,hostfwd=tcp:<port>-:22 -device vfio-pci,host=<VF PCIe id> -
    vnc :<port> &
    ```

    For example:

    ```
    qemu-system-x86_64 -cpu host -enable-kvm -m 8192 -mem-prealloc -mempath
    /mnt/huge_vm -smp sockets=1,cores=4 -hda ubuntu.qcow2 -device
    e1000,netdev=net0 -netdev user,id=net0,hostfwd=tcp:5556-:22 -device vfiopci,
    host=af:00.2 -vnc :5556 &
    ```

    ***Note*:** In the above command, 8 GB memory has been allocated. Four cores and qcow2 images are named ubuntu.qcow2.

11. After the above command gets executed, run the VM using the following command in different terminals:

    ```
    ssh -p <port> <user_name>@127.0.0.1
    ```

    For example:

    ```
    ssh -p 5556 vm@127.0.0.1
    ```

