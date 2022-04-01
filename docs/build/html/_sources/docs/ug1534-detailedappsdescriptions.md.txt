# Detailed Applications Description

## Legacy and Switchdev Modes

### Legacy NIC (Default)

Packets from the external MAC0 are forwarded to the internal MAC2 without any modifications on flow entry miss, and vice versa. Similarly, packets from the external MAC1 are forwarded to the internal MAC3 without any modifications on flow entry miss, and vice versa.

*Figure 5:* **Legacy Mode**

![X25712-091321](media/cgl1631062304607_LowRes.png)

### Switchdev Mode

When changed to switchdev mode, the U25N can support OVS switching. Devlink features are added to the PF0 interface in each adapter to support the switch mode. Switchdev mode can be added for a single adapter or both. A new representor network interface comes up for each VF when a VM is connected to the VF via SR-IOV virtual ports provided by X2 VNICs.

*Figure 6:* **Switchdev Mode**

![X25549-091321](media/lpe1631063727073_LowRes.png)

## OVS

### Installing OVS

OVS is a multilayer software switch licensed under the open source Apache 2 license. It implements a production quality switch platform that supports standard management interfaces and opens the forwarding functions to programmatic extension and control. OVS is well suited to function as a virtual switch in VM environments. Carry out the following step to install OVS:

1. The OVS source code is its Git repository, which you can clone into a directory named ovs with the git clone command (see [https://github.com/openvswitch/ovs.git](https://github.com/openvswitch/ovs.git)).

2. After it has been cloned, the ovs directory will be in the current directory path. Move inside the ovs directory using the cd command. For example:

   ```
   cd ovs
   ```

3. Execute the following commands one by one as the root user:

   ```
   ./boot.sh
   ./configure
   make -j8
   make install
   ```

4. Export the path:

   ```
   export PATH=$PATH:/usr/local/share/openvswitch/scripts
   ```

5. Perform a version check:

   ```
   ovs-vswitchd --version
   ```

   ***Note*:** Version 2.12 and 2.14 have been tested.

Maximum flows supported: 8k

### Classification Fields (Matches)

Key

1. ipv4/ipv6 src_ip

2. ipv4/ipv6 dst_ip

3. ip_tos

4. ip_proto

5. ovlan Outer

6. ivlan Inner

7. ether_type

8. tcp/udp src_port

9. tcp/udp dst_port

10. src_mac

11. dst_mac

12. vni

13. Ingress port

Action

1. do_decap

2. do_decr_ip_ttl

3. do_src_mac

4. do_dst_mac

5. do_vlan_pop

6. do_vlan_push

7. do_encap

8. do_deliver

### Port to Port

The U25N PF is added to the OVS bridge. Packets are sent at an external MAC, and OVS does the switching based on the packet received.

1. Refer to [Basic Requirements and Component Versions Supported](./ug1534-installation.md#basic-requirements-and-component-versions-supported) for the required OS/ software version.

2. If the U25N driver version is 5.3.3.1003, ignore this step. Check the driver version of the U25N interface using the following command:

   ```
   ethtool -i U25_eth0 | grep version
   ```

   This should give the output as version 5.3.3.1003.

   ***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.md#u25n-driver).

3. Make both PF interfaces up:

   1. List the interfaces using the `ifconfig -a` command.

   2. Find the U25N interface using the `ethtool -i <interface_name>` command:

      ```
      ifconfig <u25_interface> up
      ```

      For example:

      ```
      ifconfig U25_eth0 up
      ifconfig U25_eth1 up
      ```

4. Make the PF interfaces into switchdev mode.

   ***Note*:** Make sure the PF interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives us the PCIe® device bus ID:

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example:

   ```
   devlink dev eswitch set pci/0000:af:00.0 mode switchdev
   devlink dev eswitch set pci/0000:af:00.1 mode switchdev
   ```

5. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.md#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, continue to the next step.

6. Add external ports to the OVS bridge:

   ```
   ovs-vsctl add-port br0 <PF interface>
   ```

   For example:

   ```
   ovs-vsctl add-port br0 U25_eth0
   ovs-vsctl add-port br0 U25_eth1
   ```

7. Print a brief overview of the database contents:

   ```
   ovs-vsctl show
   ```

   Refer to [Functionality Check](link) to check the OVS functionality.

*Figure 7:* **Port to Port**

![X25550-090721 this seems messed up and missing a portion](media/cfd1631064119641_LowRes.png)

### Port to VM or VM to Port

***Note*:** SR-IOV must be enabled in BIOS. For the Port to VM or VM
to Port case, a tunnel could be created with two server setups. Here
the tunnel can be VXLAN or L2GRE.

1. Refer to [Basic Requirements and Component Versions Supported](./ug1534-installation.md#basic-requirements-and-component-versions-supported) for the required OS/software version. For VM use cases, VFs need to be created at the corresponding PF for binding to the VM. The number of VF counts should be configured in the sfboot command and in sriov_numvfs.

   ***Note*:** For more information, refer to [sfboot Configuration](link).

2. Check the driver version of the U25N interface using the following command:

   ***Note*:** Ignore this step if the U25N driver version is 5.3.3.1003.

   ```
   ethtool -i U25_eth0 | grep version
   ```

   This should give the output as version 5.3.3.1003.

   ***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.md#basic-requirements-and-component-versions-supported).

   ***Note*:** Refer to [Deployment Image Flashing](link) for flashing images to check OVS functionality.

3. Make the U25N PF interface up:

   a. List the PF interface using the `ifconfig -a` command.

   b. Find the U25N PF interface using the `ethtool -i <interface_name>` command.

   For example:

   ```
   ethtool -i <U25_eth0>
   ```

   driver: sfc

   version: 5.3.3.1003

   ```
   ifconfig <U25_interface> up
   ```

   For example:

   ```
   ifconfig U25eth0 up
   ```

4. Allocate the number of VF to PF. Here, a single VF is allocated to the PF0 interface:

   ***Note*:** The VF could also be created in the PF1 interface based on use case. The sriov_numvfs count should be less than or equal to the VF count given in the sfboot command. The sriov_numvfs should be done only in legacy mode. To check the U25N mode, excecute the following steps.

   ```
   devlink dev eswitch show pci/0000:<pci_id>
   ```

   The output of the above command would be:

   ```
   pci/0000:af:00.0 mode legacy
   ```

   If not in legacy mode, change to legacy mode using the following command:

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
   echo 1 > /sys/class/net/<interface>/device/sriov_numvfs
   ```

   For example:

   ```
   echo 1 > sys/class/net/U25_eth0/device/sriov_numvfs
   ```

   ***Note*:** After the above command is executed, a VF PCIe ID and VF interface are created. The VF PCIe device ID can be listed with the command `lspci -d 1924:1b03`. An example of the device ID is 86:00.2 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (Virtual Function) (rev 01). This VF PCIe ID is used for binding the VF to a VM.

5. The VF interface can be found using the `ifconfig -a` command. To differentiate VF from PF, use the ip link show command. This gives the VF interface ID and VF interface mac address under the PF interface.

6. Make the VF interface up:

   ```
   ifconfig <vf_interface> up
   ```

7. Make the PF interfaces into switchdev mode:

   ***Note*:** Make sure the PF interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives the PCIe device bus ID.

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example, `devlink dev eswitch set pci/0000:af:00.0 mode switchdev`.

8. Running the above command creates a VF representor interface. The VF representor interface name will be the PF interface name followed by `_0` for the first VF representor and `_1` for the second V representor, and so on.

   ***Note*:** Here, the number of VF representor interfaces created is based on the sriov_numvfs value configured.

   ```
   ip link show | grep <PF_interface>
   ```

   For example, `ip link show | grep <u25eth0>`.

   ***Note*:** Here u25eth0 is the PF interface and u25eth0_0 is the VF representor interface.

   Now make the VF representor interface up using the ifconfig command:

   ```
   ifconfig <vf_rep_interface> up
   ```

9. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.md#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, proceed to the next step.

10. Add PF interfaces as ports to the OVS bridge:

    ```
    ovs-vsctl add-port br0 <x2 interface>
    ```

    For example, `ovs-vsctl add-port br0 U25eth0`.

11. Add a VF representator interface as a port to the OVS bridge:

    ```
    ovs-vsctl add-port <bridge-name> <VF rep interface>
    ```

    For example, `ovs-vsctl add-port br0 U25eth0_0`.

12. Make the OVS bridge up:

    ```
    ifconfig <bridge-name> up
    ```

13. Print a brief overview of the database contents:

    ```
    ovs-vsctl show
    ```

14. Refer to [VM Installation](link) to make the VM up. After the VM is up, proceed to the next step to check functionality.

15. Refer to [Functionality Check](link) to check OVS functionality.

*Figure 8:* **Port to VM or VM to Port**

![seems messed up a little bit X25551-090721](media/cgm1631064433686_LowRes.png)

### VM to VM

***Note*:** SR-IOV must be enabled in BIOS. For a VM to VM case, a
tunnel could be created with two server setups. Here the tunnel can be
VXLAN or L2GRE.

1. Refer to [U25N Driver](./ug1534-installation.md#basic-requirements-and-component-versions-supported) for the required OS/software version. For VM use cases, VFs need to be created at the corresponding PF for binding to the VM. The number of VF counts should be configured in the sfboot command and in sriov_numvfs. Offload will occur only between VMs created using same the PF's VF.

   ***Note*:** For more information, refer to sfboot Configuration.

2. Check the driver version of the U25N interface using the following command:

   ***Note*:** Ignore this step if the U25N driver version is 5.3.3.1003.

   ```
   ethtool -i U25_eth0 | grep version
   ```

   This should give the output as version 5.3.3.1003.

   ***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.md#basic-requirements-and-component-versions-supported).

   ***Note*:** Refer to [Deployment Image Flashing](link) for flashing images to check OVS functionality.

3. Make the U25N X2 PF interface up:

   1. List the PF interface using the ifconfig -a command.

   2. Find the U25N PF interface using the `ethtool -i <interface_name>` command.

      For example:

      ```
      ethtool -i <U25_eth0>
      ```

      driver: sfc

      version: 5.3.3.1003

      `ifconfig <U25_interface> up`

      For example:

      ```
      ifconfig U25eth0 up
      ```

4. Allocate the number of VF to PF. Here, two VFs are allocated to the PF0 interface:

   ***Note*:** The VF could also be created in the PF1 interface based on the use case. The sriov_numvfs count should be less than or equal to the VF count given in the sfboot command. The sriov_numvfs should be done only in legacy mode. To check the U25N mode, execute the following steps.

   ```
   devlink dev eswitch show pci/0000:<pci_id>
   ```

   The output of the above command would be:

   ```
   pci/0000:af:00.0 mode legacy
   ```

   If not in legacy mode, change to legacy mode using the following command:

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
   echo 2 > /sys/class/net/<interface>/device/sriov_numvfs
   ```

   For example:

   ```
   echo 1 /sys/class/net/U25_eth0/device/sriov_numvfs
   ```

   ***Note*:** After the above command is executed, two VF PCIe IDs and two VF interfaces are created. The VF PCIe device ID can be listed with the `lspci -d 1924:1b03` command. An example of the device ID is *af:00.2 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (Virtual Function) (rev 01)* and *af:00.3 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (Virtual Function) (rev 01)*. These VF PCIe IDs would be used for binding VFs to VMs.

5. The two VF interfaces can be found using the `ifconfig -a` command. To differentiate VF from PF, use the `ip link show` command. This gives the VF interface ID and VF interface mac address under the PF interface.

6. Make the two VF interfaces up:

   ```
   ifconfig <vf_interfaceup>
   ```

7. Make the PF interfaces into switchdev mode.

   ***Note*:** Make sure the PF interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives the PCIe device bus ID:

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example, `devlink dev eswitch set pci/0000:af:00.0 mode switchdev`.

8. Running the above command creates two VF representor interfaces. The VF representor interface name will be the PF interface name and `_0` for the first VF representor and `_1` for the second VF representor.

   ***Note*:** Here, the number of VF representor interfaces created is based on the sriov_numvfs value configured.

   ip link show | grep <PF_interface>

   For example, `<ip link show | grep <u25eth0>`.

   ```
   u25eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP
   mode DEFAULT group default qlen 1000
   u25eth0_0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel
   master ovs-system state UP mode DEFAULT group default qlen 1000
   u25eth0_1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel
   master ovs-system state UP mode DEFAULT group default qlen 1000
   ```

   ***Note*:** Here u25eth0 is the PF interface, and u25eth0_0 and u25eth0_1 are the VF representor interfaces.

   Now make the VF representor interfaces up using the ifconfig command:

   ```
   ifconfig <vf_rep_interface> up
   ```

9. Follow the steps mentioned in OVS Configuration to create an OVS bridge. After creating the OVS bridge, proceed to the next step.

10. Add two VF representator interfaces to the OVS bridge:

    ```
    ovs-vsctl add-port <bridge-name> <VF rep interface 1>
    ovs-vsctl add-port <bridge-name> <VF rep interface 2>
    ```

    For example:

    ```
    ovs-vsctl add-port br0 u25eth0_0
    ovs-vsctl add-port br0 u25eth0_1
    ```

11. Make the OVS bridge up:

    ```
    ifconfig <bridge-name> up
    ```

12. Print a brief overview of the database contents:

    ```
    ovs-vsctl show
    ```

13. Refer to [VM Installation](./ug1534-vminstall.md) to make the VM up. After the VM is up, proceed to the next step to check functionality.

14. Refer to [Functionality Check(link)] to check OVS functionality.

*Figure 9:* **VM to VM**

![X25552-090721 might be messed up](media/cxa1631064658045_LowRes.png)

### Tunnels (Encapsulation/Decapsulation)

U25N hardware supports offloading of tunnels using encapsulation and decapsulation actions.

- **Encapsulation:** Pushing of tunnel header is supported on TX

- **Decapsulation:** Popping of tunnel header is supported on RX

Supported tunnels:

- VXLAN

- L2GRE

#### L2GRE

***Note*:** For Port to VM or VM to Port or VM to VM case, a tunnel
could be created with two server setups. Here the tunnel can be L2GRE.

- Maximum tunnel support = 1K

- Maximum supported flows = 8K

- Maximum MTU size = 1400

Refer to [Basic Requirements and Component Versions Supported](./ug1534-installation.md#basic-requirements-and-component-versions-supported) for the required OS/software version. An L2GRE tunnel can be formed between two servers. Tunnel endpoint IP should be added to the PF interface where the tunnel needs to be created.

***Note*:** Two tunnels could be formed between two PFs of U25N SmartNICs in two different servers for the VM to VM case.

##### **Server 1 Configuration**

1. Check the driver version of the U25N interface using this command:

   ***Note*:** Ignore this step if the U25N driver version is 5.3.3.1003.

   ```
   ethtool -i U25_eth0 | grep version
   ```

   This should give the output as version 5.3.3.1003

   ***Note*:** To install the latest sfc driver, refer to U25N Driver.

   ***Note*:** Refer to Deployment Image Flashing for flashing images to check OVS functionality.

2. Make the U25N PF interfaces up:

   List the PF interfaces using the `ifconfig -a` command. Find the U25N PF interface using the `ethtool -i <interface_name>` command.

   For example:

   ```
   ethtool -i <U25_eth0>
   ```

   driver: sfc

   version: 5.3.3.1003

   ```
   ifconfig <U25_interface> up
   ```

   For example:

   ```
   ifconfig U25eth0 up
   ifconfig U25eth1 up
   ```

3. Assign tunnel IP to PF0 interface:

   ```
   ifconfig <interface_1> <ip> up
   ```

   For example, ifconfig U25eth0 10.16.0.2/24 up.

4. Make PF0 interface into switchdev mode.

   ***Note*:** Make sure the PF0 interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives the PCIe device bus ID.

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example:

   ```
   devlink dev eswitch set pci/0000:af:00.0 mode switchdev
   ```

5. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.md#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, proceed to the next step.

6. Create GRE interfaces:

   ```
   ovs-vsctl add-port <bridge_name> gre0 -- set interface gre0 type=gre
   options:local_ip=<ip_address> options:remote_ip=<ip_address>
   ```

   For example:

   ```
   ovs-vsctl add-port br0 gre0 -- set interface gre0 type=gre
   options:local_ip=10.16.0.2 options:remote_ip=10.16.0.1
   ```

7. Add a PF1 interface as a port to the OVS bridge:

   ```
   ovs-vsctl add-port <bridge-name> <U25N interface_2>
   ```

   For example:

   ```
   ovs-vsctl add-port br0 U25eth1
   ```

8. Make the bridge up:

   ```
   ifconfig <bridge_name> up
   ```

   For example:

   ```
   ifconfig br0 up
   ```

9. Print a brief overview of the database contents:

   ```
   ovs-vsctl show
   ```

##### Server 2 Configuration

1. Check the driver version of the U25N interface using this command:

   ***Note*:** Ignore this step if the U25N X2 driver version is 5.3.3.1003.

   ```
   ethtool -i U25_eth0 | grep version
   ```

   This should give the output as version 5.3.3.1003.

   ***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.md#basic-requirements-and-component-versions-supported).

2. Make the U25N PF interfaces up:

   List the PF interfaces using the `ifconfig -a` command. Find the U25N PF interface using the `ethtool -i <interface_name>` command.

   For example:

   ```
   ethtool -i <U25_eth0>
   ```

   driver: sfc

   version: 5.3.3.1000

   ```
   ifconfig <U25_interface> up
   ```

   For example:

3. Assign tunnel IP to PF0 interface:

   ```
   ifconfig <interface_1> <ip> up
   ```

   For example:

   ```
   ifconfig U25eth0 10.16.0.1/24 up
   ```

4. Make PF0 interface into switchdev mode.

   ***Note*:** Make sure the PF0 interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives the PCIe device bus ID.

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example:

   ```
   devlink dev eswitch set pci/0000:af:00.0 mode switchdev
   ```

5. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.md#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, proceed to the next step.

6. Create GRE interfaces:

   ```
   ovs-vsctl add-port <bridge_name> gre0 -- set interface gre0 type=gre
   options:local_ip=<ip_address> options:remote_ip=<ip_address>
   ```

   For example:

   ```
   ovs-vsctl add-port br0 gre0 -- set interface gre0 type=gre
   options:local_ip=10.16.0.1 options:remote_ip=10.16.0.2
   ```

7. Add a PF1 interface as a port to the OVS bridge:

   ```
   ovs-vsctl add-port <bridge_name> <U25N interface_2>
   ```

   For example:

   ```
   ovs-vsctl add-port br0 U25eth1
   ```

8. Make the bridge up:

   ```
   ifconfig <bridge_name> up
   ```

   For example:

   ```
   ifconfig br0 up
   ```

9. Print a brief overview of the database contents:

   ```
   ovs-vsctl show
   ```

10. Refer to [Functionality Check](link) to check OVS functionality.

*Figure 10:* **L2GRE**

![X25553-091321](media/idz1631575855541_LowRes.png)

#### VXLAN

***Note*:** For Port to VM or VM to Port or VM to VM case, a tunnel could be created with two server setups. Here the tunnel can be VXLAN.

- Maximum tunnel support = 1K

- Maximum supported flows = 8K

- Maximum MTU size = 1400

Refer to [Basic Requirements and Component Versions Supported](./ug1534-installation.md#basic-requirements-and-component-versions-supported) for the required OS/software version. A VXLAN tunnel can be formed between two servers. Tunnel endpoint IP should be added to the PF interface where the tunnel needs to be created.

##### Server 1 Configuration

1. Check the driver version of the U25N interface using this command:

   ***Note*:** Ignore this step if the U25N X2 driver version is 5.3.3.1003.

   ```
   ethtool -i U25_eth0 | grep version
   ```

   This should give the output as version 5.3.3.1003.

   ***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.md#basic-requirements-and-component-versions-supported).

   ***Note*:** Refer to Deployment Image Flashing for flashing images to check OVS functionality.

2. Make the U25N PF interfaces up:

   List the PF interfaces using the ifconfig -a command. Find the U25N PF interface using the `ethtool -i <interface_name>` command.

   For example:

   ```
   ethtool -i <U25_eth0>
   ```

   driver: sfc

   version: 5.3.3.1003

   ```
   ifconfig <U25_interface> up
   ```

   For example:

   ```
   ifconfig U25eth0 up
   ifconfig U25eth1 up
   ```

3. Assign tunnel IP to PF0 interface:

   ```
   ifconfig <interface_1> <ip> up
   ```

   For example:

   ```
   ifconfig U25eth0 10.16.0.2/24 up
   ```

4. Make PF0 interface into switchdev mode.

   ***Note*:** Make sure the PF0 interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives the PCIe device bus ID.

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example, `devlink dev eswitch set pci/0000:af:00.0 mode switchdev`.

5. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.md#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, proceed to the next step.

6. Create VXLAN interfaces:

   ```
   ovs-vsctl add-port br0 vxlan0 -- set interface vxlan0 type=vxlan
   options:local_ip=<ip_address> options:remote_ip=<ip_address>
   options:key=<key_id>
   ovs-vsctl add-port br0 vxlan0 -- set interface vxlan0 type=vxlan
   options:local_ip=10.16.0.2 options:remote_ip=10.16.0.1 options:key=123
   ```

7. Add a PF1 interface as a port to the OVS bridge:

   ```
   ovs-vsctl add-port <bridge-name> <U25N interface_2>
   ```

   For example, `ovs-vsctl add-port br0 U25eth1`.

8. Make the bridge up:

   ```
   ifconfig <bridge_name> up
   ```

   For example:

   ```
   ifconfig br0 up
   ```

9. Print a brief overview of the database contents:

   ```
   ovs-vsctl show
   ```

##### Server 2 Configuration

1. Check the driver version of the U25N X2 interface using this command:

   ***Note*:** Ignore this step if the U25N driver version is 5.3.3.1003.

   ```
   ethtool -i U25_eth0 | grep version
   ```

   This should give the output as version 5.3.3.1003.

   ***Note*:** To install the latest sfc driver, refer to U25N Driver.

2. Make the U25N PF interfaces up:

   List the PF interfaces using the `ifconfig -a` command. Find the U25N PF interface using the `ethtool -i <interface_name>` command.

   For example:

   ```
   ethtool -i <U25_eth0>
   ```

   driver: sfc

   version: 5.3.3.1003

   ```
   ifconfig <U25_interface> up
   ```

   For example:

   ```
   ifconfig U25eth0 up
   ifconfig U25eth1 up
   ```

3. Assign tunnel IP to PF0 interface:

   ```
   ifconfig <interface_1> <ip> up
   ```

   For example:

   ```
   ifconfig U25eth0 10.16.0.1/24 up
   ```

4. Make PF0 interface into switchdev mode.

   ***Note*:** Make sure the PF0 interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives the PCIe device bus ID.

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example:

   ```
   devlink dev eswitch set pci/0000:af:00.0 mode switchdev
   ```

5. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.md#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, proceed to the next step.

6. Create VXLAN interfaces:

   ```
   ovs-vsctl add-port br0 vxlan0 -- set interface vxlan0 type=vxlan
   options:local_ip=<ip_address> options:remote_ip=<ip_address>
   options:key=<key_id>
   ovs-vsctl add-port br0 vxlan0 -- set interface vxlan0 type=vxlan
   options:local_ip=10.16.0.1 options:remote_ip=10.16.0.2 options:key=123
   ```

7. Add a PF1 interface as a port to the OVS bridge:

   ```
   ovs-vsctl add-port <bridge_name> <U25N interface_2>
   ```

   For example:

   ```
   ovs-vsctl add-port br0 U25eth1
   ```

8. Make the bridge up:

   ```
   ifconfig <bridge_name> up
   ```

   For example:

   ```
   ifconfig br0 up
   ```

9. Print a brief overview of the database contents:

   ```
   ovs-vsctl show
   ```

10. Refer to [Functionality Check](link) to check OVS functionality.

*Figure 11:* **VXLAN**

![X25554-091321](media/rcv1631576075856_LowRes.png)

#### VM to VM or VM to Port or Port to VM Tunnel

- Maximum tunnel support = 1K

- Maximum supported flows = 8K

- Maximum MTU size = 1400

Refer to [Basic Requirements and Component Versions Supported](./ug1534-installation.md#basic-requirements-and-component-versions-supported). A VXLAN tunnel can be formed between two
servers. Tunnel endpoint IP should be added to the PF interface where
the tunnel needs to be created.

##### Server 1 Configuration

1. Check the driver version of the U25N interface using this command:

   ***Note*:** Ignore this step if the U25N X2 driver version is 5.3.3.1003.

   ```
   ethtool -i U25_eth0 | grep version
   ```

   This should give the output as version 5.3.3.1003.

   ***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.md#basic-requirements-and-component-versions-supported).

   ***Note*:** Refer to [Deployment Image Flashing](link) for flashing images to check OVS functionality.

2. Make the U25N PF interfaces up:

   List the PF interfaces using the `ifconfig -a` command. Find the U25N PF interface using the `ethtool -i <interface_name>` command.

   For example:

   ```
   ethtool -i <U25_eth0>
   ```

   Driver: sfc

   Version: 5.3.3.1003

   ```
   ifconfig <U25_interface> up
   ```

   For example:

   ```
   ifconfig U25eth0 up
   ifconfig U25eth1 up
   ```

3. Assign tunnel IP to PF0 and PF1 interfaces:

   ```
   ifconfig <interface_1> <ip> up
   ifconfig <interface_2> <ip> up
   ```
   
   For example:

   ```
   ifconfig <interface_1> <ip> up
   ifconfig <interface_2> <ip> up
   ```

4. Allocate the number of VF to PF. Here, one VF is allocated to the PF0 and PF1 interfaces:

   ***Note*:** The sriov_numvfs count should be less than or equal to VF count given in sfboot command. The sriov_numvfs should be done only in legacy mode. To check mode, please follow the below steps.

   ```
   devlink dev eswitch show pci/0000:<pci_id>
   ```

   The output of the above command would be:

   ```
   pci/0000:af:00.0 mode legacy
   ```

   If not in legacy mode, change to legacy mode using the following command:

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
   echo 1 > /sys/class/net/<interface>/device/sriov_numvfs
   ```

   For example:

   ```
   echo 1 > /sys/class/net/U25_eth0/device/sriov_numvfs
   echo 1 > /sys/class/net/U25_eth1/device/sriov_numvfs
   ```

   ***Note*:** After the above command is executed, a VF PCIe ID and a VF interface are created corresponding to each PF0 and PF1. The VF PCIe device ID can be listed with the `lspci -d 1924:1b03` command. An example of the device ID is af:00.2 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (Virtual Function) (rev 01) and af:00.6 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (Virtual Function) (rev 01). These VF PCIe IDs would be used for binding VFs to VMs.

5. The two VF interfaces can be found using the `ifconfig -a` command. To differentiate VF from PF, use the `ip link show` command. This gives the VF interface ID and VF interface mac address under the PF interface.

6. Make the two VF interfaces up:

   ```
   ifconfig <vf_interface> up
   ```

7. Make the PF0 and PF1 interfaces into switchdev mode.

   ***Note*:** Make sure the PF0 and PF1 interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives the PCIe device bus ID:

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example:

   ```
   devlink dev eswitch set pci/0000:af:00.0 mode switchdev
   devlink dev eswitch set pci/0000:af:00.1 mode switchdev
   ```

8. Running the above command creates a VF representor interface
    corresponding to each PF interface. The VF representor interface
    name will be the PF interface name along with `_0`.

   ***Note*:** Here, the number of VF representor created is based on the sriov_numvfs value configured.

   ```
   ip link show | grep <PF_interface>
   ```

   For example, `ip link show | grep <u25eth0>`.

   ```
   u25eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP
   mode DEFAULT group default qlen 1000
   u25eth0_0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel
   master ovs-system state UP mode DEFAULT group default qlen 1000
   ip link show | grep <u25eth1>
   u25eth1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP
   mode DEFAULT group default qlen 1000
   u25eth1_0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel
   master ovs-system state UP mode DEFAULT group default qlen 1000
   ```

   ***Note*:** Here u25eth0 and u25eth1 are the PF interfaces, and u25eth0_0 and u25eth1_0 are the VF representor interfaces.

   Now make the VF representor interfaces up using the ifconfig command:

   ```
   ifconfig <vf_rep_interface> up
   ```

9. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.md#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, proceed to the next step.

10. Create VXLAN/GRE interfaces:

    ***Note*:** The following configuration is for the VXLAN. Similarly, the GRE tunnel could also be used.

    ```
    ovs-vsctl add-port br0 vxlan0 -- set interface vxlan0 type=vxlan
    options:local_ip=<ip_address> options:remote_ip=<ip_address>
    options:key=<key_id>
    ovs-vsctl add-port br1 vxlan1 -- set interface vxlan0 type=vxlan
    options:local_ip=<ip_address> options:remote_ip=<ip_address>
    options:key=<key_id>
    ```

    For example:

    ```
    ovs-vsctl add-port br0 vxlan0 -- set interface vxlan0 type=vxlan
    options:local_ip=10.16.0.2 options:remote_ip=10.16.0.1 options:key=123
    ovs-vsctl add-port br1 vxlan1 -- set interface vxlan type=vxlan
    options:local_ip=10.16.0.3 options:remote_ip=10.16.0.4 options:key=456
    ```

11. Adding VF representor of each PF interface to separate OVS bridge.

    ```
    ovs-vsctl add-port <bridge-name_0> <VF rep interface 1>
    ovs-vsctl add-port <bridge-name_1> <VF rep interface 2>
    ```

    For example:

    ```
    ovs-vsctl add-port br0 u25eth0_0
    ovs-vsctl add-port br1 u25eth1_0
    ```

12. Make the two bridges up:

    ```
    ifconfig <bridge_name> up
    ```

    For example:

    ```
    ifconfig br0 up
    ifconfig br1 up
    ```

13. Print a brief overview of the database contents:

    ```
    ovs-vsctl show
    ```

14. Refer to [VM Installation](./ug1534-vminstall.md) to make the virtual machine up.

##### Server 2 Configuration

1. Check the driver version of the U25N interface using this command:

   ***Note*:** Ignore this step if the U25N X2 driver version is 5.3.3.1003.

   ```
   ethtool -i U25_eth0 | grep version
   ```

   This should give the output as version 5.3.3.1003.

   ***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.md#basic-requirements-and-component-versions-supported).

   ***Note*:** Refer to [Deployment Image Flashing](link) for flashing images to check OVS functionality.

2. Make the U25N PF interfaces up:

   List the PF interfaces using the `ifconfig -a` command. Find the U25N PF interface using the `ethtool -i <interface_name>`command.

   For example:

   ```
   ethtool -i <U25_eth0>
   ```

   Driver: sfc

   Version: 5.3.3.1003

   ```
   ifconfig <U25_interface> up
   ```

   For example:

   ```
   ifconfig U25eth0 up
   ifconfig U25eth1 up
   ```

3. Assign tunnel IP to PF0 and PF1 interfaces:

   ```
   ifconfig <interface_1> <ip> up
   ifconfig <interface_2> <ip> up
   ```

   For example:

   ```
   ifconfig U25eth0 10.16.0.1/24 up
   ifconfig U25eth1 10.16.0.4/24 up
   ```

4. Allocate the number of VF to PF. Here, one VF is allocated to the PF0 and PF1 interfaces:

   ***Note*:** The sriov_numvfs count should be less than or equal to VF count given in sfboot command. The sriov_numvfs should be done only in legacy mode. To check mode, please follow the below steps.

   ```
   devlink dev eswitch show pci/0000:<pci_id>
   ```

   The output of the above command would be:

   ```
   pci/0000:af:00.0 mode legacy
   ```

   If not in legacy mode, change to legacy mode using the following command:

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
   echo 1 > /sys/class/net/<interface>/device/sriov_numvfs
   ```

   For example:

   ```
   echo 1 > /sys/class/net/U25_eth0/device/sriov_numvfs
   echo 1 > /sys/class/net/U25_eth1/device/sriov_numvfs
   ```

   ***Note*:** After the above command is executed, a VF PCIe ID and a VF interface are created corresponding to each PF0 and PF1. The VF PCIe device ID can be listed with the `lspci -d 1924:1b03` command. An example of the device ID is af:00.2 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (Virtual Function) (rev 01) and af:00.6 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (Virtual Function) (rev 01). These VF PCIe IDs would be used for binding VFs to VMs.

5. The two VF interfaces can be found using the `ifconfig -a` command. To differentiate VF from PF, use the `ip link show` command. This gives the VF interface ID and VF interface mac address under the PF interface.

6. Make the two VF interfaces up:

   ```
   ifconfig <vf_interface> up
   ```

7. Make the PF0 and PF1 interfaces into switchdev mode.

   ***Note*:** Make sure the PF0 and PF1 interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives the PCIe device bus ID:

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus idm ode switchdev
   ```

   For example:

   ```
   devlink dev eswitch set pci/0000:af:00.0 mode switchdev
   devlink dev eswitch set pci/0000:af:00.1 mode switchdev
   ```

8. Running the above command creates a VF representor interface corresponding to each PF interface. The VF representor interface name will be the PF interface name along with `_0`.

   ***Note*:** Here, the number of VF representor created is based on the sriov_numvfs value configured.

   ```
   ip link show | grep <PF_interface>
   ```

   For example, `ip link show | grep <u25eth0>`.

   ***Note*:** Here u25eth0 and u25eth1 are the PF interfaces, and u25eth0_0 and u25eth1_0 are the VF representor interfaces.

   Now make the VF representor interfaces up using the ifconfig command:

   ```
   ifconfig <vf_rep_interface> up
   ```

9. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.md#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, proceed to the next step.

10. Create VXLAN/GRE interfaces:

    ***Note*:** The following configuration is for the VXLAN. Similarly, the GRE tunnel could also be used.

    ```
    ovs-vsctl add-port br0 vxlan0 -- set interface vxlan0 type=vxlan
    options:local_ip=<ip_address> options:remote_ip=<ip_address>
    options:key=<key_id>
    ovs-vsctl add-port br1 vxlan1 -- set interface vxlan0 type=vxlan
    options:local_ip=<ip_address> options:remote_ip=<ip_address>
    options:key=<key_id>
    ```

    For example:

    ```
    ovs-vsctl add-port br0 vxlan0 -- set interface vxlan0 type=vxlan
    options:local_ip=10.16.0.1 options:remote_ip=10.16.0.2 options:key=123
    ovs-vsctl add-port br1 vxlan1 -- set interface vxlan type=vxlan
    options:local_ip=10.16.0.4 options:remote_ip=10.16.0.3 options:key=456
    ```

11. Adding VF representor of each PF interface to separate OVS bridge.

    ```
    ovs-vsctl add-port <bridge-name_0> <VF rep interface 1>
    ovs-vsctl add-port <bridge-name_1> <VF rep interface 2>
    ```

    For example:

    ```
    ovs-vsctl add-port br0 u25eth0_0
    ovs-vsctl add-port br1 u25eth1_0
    ```

12. Make the two bridges up:

    ```
    ifconfig <bridge_name> up
    ```

    For example:

    ```
    ifconfig br0 up
    ifconfig br1 up
    ```

13. Print a brief overview of the database contents:

    ```
    ovs-vsctl show
    ```

14. Refer to [VM Installation](./ug1534-vminstall.md) to make the virtual machine up. After the VM is up, do refer to [Functionality Check](link) check functionality.

*Figure 12:* **Tunneling/Detunneling**

![X25696-090721](./media/ash1631066523492_LowRes.png)

### OVS Configuration

1. Export the OVS path:

   ```
   export PATH=$PATH:/usr/local/share/openvswitch/scripts
   export PATH=$PATH:/usr/local/bin
   ```

2. Stop OVS and remove the database for removing old configurations:

   ```
   ovs-ctl stop
   rm /usr/local/etc/openvswitch/conf.db
   ```

3. Start OVS:

   ```
   ovs-ctl start
   ```

4. Enable hardware offload:

   ```
   ovs-vsctl set Open_vSwitch . other_config:hw-offload=true
   ovs-vsctl set Open_vSwitch . other_config:tc-policy=none
   ```

5. After adding the policy, restart OVS:

   ```
   ovs-ctl restart
   ```

6. Set OVS log levels (for debug purpose only, if needed):

   ```
   ovs-appctl vlog/set ANY:ANY:dbg
   ovs-appctl vlog/set poll_loop:ANY:OFF
   ovs-appctl vlog/set netlink_socket:ANY:OFF
   ```

7. Obtain the maximum time (in ms) that idle flows remain cached in the datapath:

   ```
   ovs-vsctl set open_vswitch $(ovs-vsctl list open_vswitch | grep _uuid |
   cut -f2 -d ":" | tr -d ' ') other_config:max-idle=30000000g
   ```

8. Print a brief overview of the database contents:

   ```
   ovs-vsctl show
   ```

   The output should be:

   ```
   <git_version>
   ovs_version: "<ovs_version>"
   ```

   ***Note*:** OVS versions 2.12 and 2.14 have been tested.

9. Add bridge to OVS:

   ```
   ovs-vsctl add-br <bridge-name>
   ovs-vsctl add-br br0
   ```

   ***Note*:** For VM to VM or VM to Port or Port to VM Tunnel alone create two OVS bridges. For example, `ovs-vsctl add-br br0` and `ovs-vsctl add-br br1`.

### Functionality Check

After adding the U25N network interfaces to the OVS bridge, the functionality can be verified using ping, iperf, and dpdk network performance tools.

#### Ping Test

1. Assign the IP address to the respective interface and do a ping using the following command:

   ```
   ping <remote_ip>
   ```

2. After the ping occurs, do an iperf:

   ***Note*:** For VXLAN and L2GRE, set the MTU size to 1400 before
running iperf or pktgen on a particular interface.

   ```
   ifconfig <interfacemtu 1400 [as root]
   ```

3. Run iperf3 -s on the host device [iperf server].

4. Run iperf3 -c <ip> on a remote device [iperf client].

***Note*:** Refer to [DPDK on U25N](link) to run dpdk-testpmd.

### Statistics

Refer to Statistics to get commands for statistics.

## IPsec

### Supported XFRM Parameters

IPsec tunnels are created between two servers. Because IPsec is in *transport mode*, L2GRE is used to create tunnels. The strongSwan application runs in userspace. The charon plugin of strongSwan is used to offload rules on the U25N. Packets reaching the IPsec module should be L2GRE encapsulated.

- Encryption algorithm: AES-GCM 256 encryption/decryption

- IPsec mode: Transport mode.

- Maximum IPsec tunnel supported: 32

### Classification Fields (Matches)

#### Encryption

Key

1. IPv4 source address

2. IPv4 destination address

3. IP4 protocol Action

Action

1. Action flag

2. SPI

3. Key

4. IV

#### Decryption

Key

1. IPv4 source address

2. IPv4 destination address

3. SPI Action

Action

1. Decryption key

2. IV

### strongSwan Installation

Do the following as a root user:

- apt-get install aptitude

- aptitude install opensc

- aptitude install libgmp10

- aptitude install libgmp-dev

- apt-get install libssl-dev

***Note*:** Before installing the Debian package for strongSwan, make sure all the dependencies are installed.

1. Refer to [Basic Requirements and Component Versions Supported](./ug1534-installation.md#basic-requirements-and-component-versions-supported) for the required OS/software version.

2. Check the version of the strongSwan Debian package. If it shows the version as strongswan_5.8.4-1, ignore this step.

   The version can be found using the command `sudo swanctl --version`.

   Remove the already installed package before installing the latest one:

   ```
   dpkg -r strongswan_5.8.4-1_amd64
   dpkg -i strongswan_5.8.4-1_amd64.deb
   ```

3. After installation of the strongSwan package, create a CA certificate. For this, create a CA certificate in one server and copy the same to another server.

#### Server 1 Configuration

1. Generate a self-sign CA certificate using the PKI utility of strongSwan:

   ```
   cd /etc/ipsec.d
   ipsec pki --gen --type rsa --size 4096 --outform pem > private/
   strongswanKey.pem
   ipsec pki --self --ca --lifetime 3650 --in private/strongswanKey.pem --
   type rsa --dn "C=CH, O=strongSwan, CN=Root CA" --outform pem > cacerts/
   strongswanCert.pem
   ```

2. After the key and certificate is generated in server 1, copy it to server 2 in the same path.

    a. Copy the file `strongswanKey.pem` in the path `/etc/ipsec.d/private/` in the first server to the second server in the same path.

    b. Copy the file `strongswanCert.pem` in the path `/etc/ipsec.d/cacerts/strongswanCert.pem` in the first server to the second server in the same path.

   After finishing the above, create a key pair and certificate for each server separately as root.

3. Generate the key pair and certificate in server 1 as root:

   ```
   cd /etc/ipsec.d
   ipsec pki --gen --type rsa --size 2048 --outform pem > private/
   client1Key.pem
   chmod 600 private/client1Key.pem
   ipsec pki --pub --in private/client1Key.pem --type rsa | ipsec pki --
   issue --lifetime 730 --cacert cacerts/strongswanCert.pem --cakey private/
   strongswanKey.pem --dn "C=CH, O=strongSwan, CN=device1" --san device1 --
   flag serverAuth --flag ikeIntermediate --outform pem > certs/
   client1Cert.pem
   ```

4. Configure the conf file and secret file in server 1:

   ```
   sudo vim /etc/ipsec.conf
   conn hw_offload #
   left=10.16.0.2
   right=10.16.0.1
   ike=aes256gcm16-sha256-modp2048
   esp=aes256gcm16-modp2048
   keyingtries=%forever
   ikelifetime=8h
   lifetime=8h
   dpddelay=1h
   dpdtimeout=1h
   dpdaction=restart
   auto=route
   keyexchange=ikev2
   type=transport
   leftcert=client1Cert.pem
   leftsendcert=always
   hw_offload=yes
   leftid="C=CH, O=strongSwan, CN=device1"
   rightid="C=CH, O=strongSwan, CN=device2"
   leftprotoport=gre
   rightprotoport=gre
   sudo vim /etc/ipsec.secrets
   : RSA client1Key.pem
   ```

   ***Note*:** White space is present between `:` and `RSA`.

#### Server 2 Configuration

1. Generate the key pair and certificate in server 2 as root:

   ```
   cd /etc/ipsec.d
   ipsec pki --gen --type rsa --size 2048 --outform pem > private/
   client2Key.pem
   chmod 600 private/client2Key.pem
   ipsec pki --pub --in private/client2Key.pem --type rsa | ipsec pki --
   issue --lifetime 730 --cacert cacerts/strongswanCert.pem --cakey private/
   strongswanKey.pem --dn "C=CH, O=strongSwan, CN=device2" --san device2 --
   flag serverAuth --flag ikeIntermediate --outform pem > certs/
   client2Cert.pem
   ```

2. Configure the conf file and secret file in server 2:

   ```
   sudo vim /etc/ipsec.conf
   conn hw_offload #
   left=10.16.0.1
   right=10.16.0.2
   ike=aes256gcm16-sha256-modp2048
   esp=aes256gcm16-modp2048
   keyingtries=%forever
   ikelifetime=8h
   lifetime=8h
   dpddelay=1h
   dpdtimeout=1h
   dpdaction=restart
   auto=route
   keyexchange=ikev2
   type=transport
   leftcert=client2Cert.pem
   leftsendcert=always
   hw_offload=yes
   leftid="C=CH, O=strongSwan, CN=device2"
   rightid="C=CH, O=strongSwan, CN=device1"
   leftprotoport=gre
   rightprotoport=gre
   sudo vim /etc/ipsec.secrets
   : RSA client2Key.pem
   ```

   ***Note*:** White space is present between `:` and `RSA`.

#### Server 1: Steps to Run IPsec

1. Check the driver version of the U25N interface using the following command:

   ```
   ethtool -i U25_eth0 | grep version
   ```

   ***Note*:** Ignore this step if the U25N driver version is 5.3.3.1003.

   This should give the output as version: 5.3.3.1003.

   ***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.md#basic-requirements-and-component-versions-supported).

2. Make the PF1 interface up:

   ```
   ifconfig <interface_2> up
   ```

   For example, `ifconfig U25eth1 up`.

3. Make the PF0 interface up and assign a tunnel endpoint IP to it:

   ```
   ifconfig <interface_1> <ip> up
   ```

   For example, `ifconfig U25eth0 10.16.0.2/24 up`.

4. Make the PF0 interface into switchdev mode.

   ***Note*:** Make sure the PF0 interface link is up before doing switchdev mode.

   The lspci | grep Sol command gives the PCIe device bus ID.

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example, `devlink dev eswitch set pci/0000:af:00.0 mode switchdev`.

5. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.md#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, proceed with the following steps.

6. Create GRE interfaces:

   ```
   ovs-vsctl add-port br0 gre0 -- set interface gre0 type=gre
   options:local_ip=<ip_address> options:remote_ip=<ip_address>
   ovs-vsctl add-port br0 gre0 -- set interface gre0 type=gre
   options:local_ip=10.16.0.2 options:remote_ip=10.16.0.1
   ```

7. Add the PF1 interface OVS bridge:

   ```
   ovs-vsctl add-port br0 <U25N interface_2>
   eg:ovs-vsctl add-port br0 U25eth1
   ```

8. Print a brief overview of the database contents:

   ```
   ovs-vsctl show
   ```

9. Make the bridge up:

   ```
   ifconfig <bridge_name> up
   ```

   For example, `ifconfig br0 up`.

10. Enable ipsec offload in the driver:

    ```
    echo 1 >> /sys/class/net/<U25N_interface_1>/device/ipsec_enable
    ```

    For example, `echo 1 >> /sys/class/net/U25eth0/device/ipsec_enable`.

11. Start IPsec:

    ```
    sudo ipsec restart
    ```

#### Server 2: Steps to Run IPsec

1. Check the driver version of the U25N interface using the following command:

   ```
   ethtool -i U25_eth0 | grep version
   ```

   ***Note*:** Ignore this step if the U25N driver version is 5.3.3.1003.

   This should give the output as version 5.3.3.1003.

   ***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.md#basic-requirements-and-component-versions-supported).

2. Make the PF1 interface up:

   ```
   ifconfig <interface_2> up
   ```

   For example, `ifconfig U25eth1 up`.

3. Make the PF0 interface up and assign a tunnel IP to it:

   ```
   ifconfig <interface_1> <ip> up
   ```

   For example, `ifconfig U25_eth0 10.16.0.1/24up`.

4. Make the PF0 interface into switchdev mode:

   ***Note*:** Make sure the PF0 interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives us the PCIe device bus ID.

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example, `devlink dev eswitch set pci/0000:af:00.0 mode switchdev`.

5. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.md#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, proceed with the following steps.

6. Create GRE interfaces:

   ```
   ovs-vsctl add-port br0 gre0 -- set interface gre0 type=gre
   options:local_ip=<ip_address> options:remote_ip=<ip_address>
   ovs-vsctl add-port br0 gre0 -- set interface gre0 type=gre
   options:local_ip=10.16.0.1 options:remote_ip=10.16.0.2
   ```

7. Add PF1 interface OVS bridge:

   ```
   ovs-vsctl add-port br0 <U25N interface_2>
   eg:ovs-vsctl add-port br0 U25eth1
   ```

8. Print a brief overview of the database contents:

   ```
   ovs-vsctl show
   ```

9. Make the bridge up.

   ```
   ifconfig <bridge_name> up
   ```

   For example, `ifconfig br0 up`.

10. Enable ipsec offload in the driver:

    ```
    echo 1 >> /sys/class/net/<U25N_interface_1>/device/ipsec_enable
    ```

    For example, `echo 1 >> /sys/class/net/U25eth0/device/ipsec_enable`.

11. Start IPsec:

    ```
    sudo ipsec restart
    ```

    Refer to [Functionality Check](link) to check OVS functionality.

### Statistics

Refer to [Statistics](link) to get IPsec statistics.

*Figure 13:* **IPsec + OVS**

![X25555-091321](media/jqy1631576791246_LowRes.png)

## Stateless Firewall

This section is about stateless firewall prerequisites, installation steps, and rules supported.

### Kernel Upgrade

Run the command `uname -r` to get the kernel version. If the kernel version is v5.5 or higher, ignore the following steps:

1. Download the Debian packages for the kernel upgrade.

2. Install the dpkg utility using the following command:

   ```
   sudo apt-get update -y
   sudo apt-get install -y dpkg
   ```

3. Unzip the folder using the following command:

   ```
   unzip <folder_name>.zip
   ```

4. Move inside the folder using the cd command:

   ```
   cd <folder_name>
   ```

5. Run the following command to install the Debian packages:

   ```
   sudo dpkg -i *.deb
   ```

6. After the command is executed with no error, do a reboot:

   ```
   sudo reboot
   ```

### nftables

***Note*:** Refer to [Deployment Image Flashing](link) for flashing images to check firewall functionality.

1. The nftables only work in legacy mode. Ensure the SmartNIC is in legacy mode using the following command:

   ```
   devlink dev eswitch show pci/0000:<pci_id>
   ```

   The output of the above command should be:

   ```
   pci/0000:<pci_id>: mode legacy
   ```

   If not in legacy mode, change to this mode using the following command:

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
   ```

2. Refer to [Basic Requirements and Component Versions Supported](./ug1534-installation.md#basic-requirements-and-component-versions-supported) for the required OS/software version.

   The nftables version can be found using the command `nft -v`.

   nftables version ≥ v0.9.6

   ***Note*:** Tested version 0.9.6 and 0.9.8.

### Classification Fields (Matches)

Maximum rules supported: 1K for each U25N PF interface.

Key:

1. IPv4/IPv6 source address

2. IPv4/IPv6 destination address

3. Protocol (TCP/UDP)

4. Src_port

5. Dst_port

6. Chain

7. Interface Action

Action

1. drop

2. accept

#### Driver Installation

***Note*:** Log in as root user before proceeding with the following
steps.

The prerequisites for the installation are as follows:

- modprobe mtd (first time only)

- modprobe mdio (first time only)

1. If the debian package already exists, remove the existing package before installing the latest debian package:

   ```
   rmmod sfc
   dpkg -r sfc-dkms
   dpkg -i sfc-dkms_5.3.3.2000_all.deb
   modprobe sfc
   ```

   ***Note*:** Login as root user before proceeding with the following steps.

2. Create a table

   ```
   nft add table <family> <name>
   ```

   For example, `nft add table netdev filter`.

3. Create a chain:

   For example, `nft add chain netdev filter input1 { type filter hook ingress device ens7f1np1 priority 1; flags offload ;}`. Adding a chain without specifying the policy leads to the default policy Accept.

4. Add rules to the chain:

   ```
   nft add rule <family> <table name> <chain name> ip saddr <ip> drop
   ```

   For example, `nft add rule netdev filter input1 ip saddr 1.1.1.1 drop`.

5. Commands for listing tables, chain, and rules:

   a. Listing a table of a netdev family:

      ```
      nft list tables <family>
      ```

      For example, `nft list tables netdev`.

   b. Listing a particular chain from a table:

      ```
      nft list chain <family> <table name> <chain_name>
      ```

      For example, `nft list chain netdev filter input1`.

   c. Listing a chain along with a handle:

      ```
      nft -a list chain <family> <table name> <chain_name>
      ```

      For example, `nft -a list chain netdev filter input1`.

   d. Listing all tables, chains, and rules with handle:

      ```
      nft -a list ruleset
      ```

6. Commands for deleting tables, chains and rules:

   a. Deleting a table:

      ```
      nft delete table <family> <name>
      ```

      For example, `nft delete table netdev filter`.

   b. Deleting a chain:

      ```
      nft delete chain <family> <table name> <chain name>
      ```

      For example, `nft delete chain netdev filter input1`.

   c. Deleting a specific rule with a handle:

      ```
      nft delete rule <family> <table name> <chain_name> handle <handle_no>
      ```

      For example, `nft delete rule netdev filter input1 handle 3`.

      ***Note*:** Here the handle number for a specific rule could be found using the `nft -a list ruleset` command.

*Figure 14:* **Firewall**

![X25556-091321 this table might be messed up](./media/uhd1631577266564_LowRes.png)

## Statistics

This section outlines the commands used by different modules to check the statistics and packet counters.

### OVS Commands

- To print a brief overview of the database contents:

  ```
  ovs-vsctl show
  ```

- To show the datapath flow entries:

  ```
  ovs-ofctl dump-flows <bridge_name>
  ```

- To show the full OpenFlow flow table, including hidden flows, on the bridge:

  ```
  ovs-appctl dpctl/dump-flows type=offloaded
  ```

- To show the OVS datapath flows:

  ```
  ovs-dpctl dump-flows
  ovs-appctl dpctl/dump-flows
  ```

- To show which flows are offloaded or not:

  ```
  tc filter show dev <iface_name> ingress
  ```

### MAE Rules

- To show offloaded rules now present in the match-action engine (MAE):

  ```
  cat /sys/kernel/debug/sfc/<if_iface>/mae_rules
  ```

  ***Note*:** Here `if_iface` should be the corresponding PF interface.

  For example, `cat /sys/kernel/debug/sfc/if_u25eth0/mae_rules`.

- To show default rules now present in the MAE:

  ```
  cat /sys/kernel/debug/sfc/<iface/mae_default_rules
  ```

### IPsec Statistics

- `sudo swanctl --stats`

- Use the `ip xfrm show` command to display IPsec offload security association.

### External MAC counter

TBD

## Debug Commands

1. To get kernel logs, enter the following command:

   ```
   dmesg
   ```

2. Logs from the U25N hardware are saved to a file. Reading a file from the X86 host produces logs. The logs are collected from the internal processing subsystem and exported to the host at intervals. These logs are populated when switchdev mode is enabled. The path to read the logs in host is `/var/log/ps_dmesg.txt`.

3. sfreport: This is a command line utility that generates a diagnostic log file providing diagnostic data about the server and Solarflare adapters.
