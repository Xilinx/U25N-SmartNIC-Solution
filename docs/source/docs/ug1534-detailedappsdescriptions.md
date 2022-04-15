# 4 Detailed Applications Description

## 4.1 Legacy and Switchdev Modes

### 4.1.1 Legacy NIC (Default)

In legacy mode packets from the external MAC0 are forwarded to the internal MAC2 without any modifications on flow entry miss, and vice versa. Similarly, packets from the external MAC1 are forwarded to the internal MAC3 without any modifications on flow entry miss, and vice versa. OVS is not supported in this configuration.

*Figure 5:* **Legacy Mode**

![X25712-091321](media/cgl1631062304607_LowRes.png)

### 4.1.2 Switchdev Mode

When changed to the switchdev mode, the U25N can support OVS switching. Devlink features are added to the PF0 interface in each adapter to support the switch mode. Switchdev mode can be added for a single adapter or both. A new representor network interface comes up for each VF when a VM is connected to the VF via SR-IOV virtual ports provided by X2 VNICs.

*Figure 6:* **Switchdev Mode**

![X25549-091321](media/lpe1631063727073_LowRes.png)

## 4.2 OVS

### 4.2.1 Installing OVS

OVS is a multilayer software switch licensed under the open source Apache 2 license. It implements a production quality switch platform that supports standard management interfaces and opens the forwarding functions to programmatic extension and control. OVS is well suited to function as a virtual switch in virtualized environments.
Follow the below-mentioned steps to install OVS.

1. The OVS source code is available in its Git repository, which  can be cloned into a directory named "ovs" using the git clone command (see [https://github.com/openvswitch/ovs.git](https://github.com/openvswitch/ovs.git)).

2. After cloning, the ovs directory will be in the current directory path. "cd" to the ovs directory as mentioned below:

   ```bash
   cd ovs
   ```

3. Execute the following commands sequentially as the root user:

   ```bash
   ./boot.sh
   ./configure
   make -j8
   make install
   ```

4. Export the OVS path:

   ```bash
   export PATH=$PATH:/usr/local/share/openvswitch/scripts
   ```

5. Perform a version check:

   ```bash
   ovs-vswitchd --version
   ```

   ***Note*:** Version 2.12 and 2.14 have been tested.

Maximum flows supported and tested: 8k

### 4.2.2 Classification Fields (Matches)

Supported Keys and Actions

```bash
Keys

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

Actions

1. do_decap

2. do_decr_ip_ttl

3. do_src_mac

4. do_dst_mac

5. do_vlan_pop

6. do_vlan_push

7. do_encap

8. do_deliver
```

### 4.2.3 Port to Port

In this configuration the U25N PF is added to the OVS bridge as an interface. Packets are sent to an external MAC, and OVS performs the switching based on the packets received.

*Figure 7:* **Port to Port setup**

![X25550-090721 this seems messed up and missing a portion](media/cfd1631064119641_LowRes.png)

Step 1. Refer to [Basic Requirements and Component Versions Supported](./ug1534-installation.html#basic-requirements-and-component-versions-supported) for the required OS/software version.

Step 2. In case the U25N driver version is 5.3.3.1008.3,this step can be ignored. Driver version of the U25N interface can be validated using the following command:

   ```bash
   ethtool -i U25_eth0 | grep version
   ```

   ***Note*:** To install the latest sfc driver, refer to U25N Driver installation section.

Step 3. Put both U25N PF interfaces in ready state by executing the following command:

   1. List the interfaces using the `ifconfig -a` command.

   2. Search for U25N interfaces using the `ethtool -i <interface_name>` command:

      ```bash
      sfboot --list # shows the U25n interfaces
      ifconfig <u25n_interface> up
      ```

      Example Output:

      ```bash
      ifconfig U25_eth0 up
      ifconfig U25_eth1 up
      ```

Step 4. Put the U25N PF interfaces into switchdev mode by executing the following command:

   ***Note*:** Ensure that the PF interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives us the PCIe® device bus ID required to execute the following command.

   ```bash
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   Example Output:

   ```bash
   devlink dev eswitch set pci/0000:af:00.0 mode switchdev
   devlink dev eswitch set pci/0000:af:00.1 mode switchdev
   ```

Step 5. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.html#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, continue with the next step.

Step 6. Add external ports to the OVS bridge by executing the following commands:

   ```bash
   ovs-vsctl add-port br0 <PF interface>
   ```

   For example:

   ```bash
   ovs-vsctl add-port br0 U25_eth0
   ovs-vsctl add-port br0 U25_eth1
   ```

Step 7. Prints a brief overview of the database contents using the following command:

   ```bash
   ovs-vsctl show
   ```

   Refer to [Functionality Check](./ug1534-detailedappsdescriptions.html#functionality-check) to check the OVS functionality.

### 4.2.4 Port to VM or VM to Port

***Note*:** To have this configuration SR-IOV must be enabled in BIOS. For the Port to VM or VM to Port configuration, a tunnel L2GRE or VXLAN could be created with two server setups.

*Figure 8:* **Port to VM or VM to Port setup**
![seems messed up a little bit X25551-090721](media/cgm1631064433686_LowRes.png)

Step 1. Refer to [Basic Requirements and Component Versions Supported](./ug1534-installation.html#basic-requirements-and-component-versions-supported) for the required OS/software version. For VM use cases, VFs need to be created for the corresponding PF for binding to the VM. The number of VF counts should be configured in the sfboot command and in sriov_numvfs.

   ***Note*:** For more information, refer to [sfboot Configuration](./ug1534-installation.html#sfboot-configuration).

Step 2. Check the driver version of the U25N interface using the following command:

   ***Note*:** Ignore this step if the U25N driver version is 5.3.3.1008.3

   ```bash
   ethtool -i U25_eth0 | grep version
   ```

   ***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.html#u25n-driver).

   ***Note*:** Refer to [Deployment Image Flashing](./ug1534-installation.html#deployment-image-flashing) for flashing images to check OVS functionality.

Step 3. Ensure that U25N PF interfaces are up by running the following commands:

   a. List the PF interface using the `ifconfig -a` command.

   b. Find the U25N PF interface using the `ethtool -i <interface_name>` command.

   For example:

   ```bash
   ethtool -i <U25_eth0>
   ```

   driver: sfc

   version: 5.3.3.1008.3

   ```bash
   ifconfig <U25_interface> up
   ```

   For example:

   ```bash
   ifconfig U25eth0 up
   ```

Step 4. Enable desired VFs to the U25N PF. In the following command, a single VF is enabled on the PF0 interface:

   ***Note*:** The VF could also be created on the PF1 interface based on the use case. The sriov_numvfs count should be less than or equal to the VF count specified in the sfboot command. The sriov_numvfs should be enabled only in legacy mode. To check the U25N mode, excecute the following steps.

   ```bash
   devlink dev eswitch show pci/0000:<pci_id>
   ```

   Example Output:

   ```bash
   pci/0000:af:00.0 mode legacy
   ```

   If not in legacy mode, change to legacy mode using the following command:

   ```bash
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
   echo 1 > /sys/class/net/<interface>/device/sriov_numvfs
   ```

   For example:

   ```bash
   echo 1 > sys/class/net/U25_eth0/device/sriov_numvfs
   ```

   ***Note*:** After executing above mentioned command, a VF PCIe ID and VF interface will get created. The VF PCIe device ID can be listed with the command `lspci -d 1924:1b03`.

An example of the device ID is
```bash
af:00.2 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (Virtual Function) (rev 01). 
This VF PCIe ID is used for binding the VF to a VM.
```
Step 5. The VF interface can be found using the `ifconfig -a` command. To differentiate VF from PF, use the ip link show command. This gives the VF interface ID and VF interface mac address under the PF interface.

Step 6. Ensure that the VF interface is up by executing the following command:

```bash
   ifconfig <vf_interface> up
```

Step 7. Ensure that the PF interface is in switchdev mode by executing the following command:

   ***Note*:** Ensure the PF interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives the PCIe device bus ID.

   ```bash
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example, `devlink dev eswitch set pci/0000:af:00.0 mode switchdev`.

Step 8. Running the above command creates a VF representor interface. The VF representor interface name will be the PF interface name followed by `_0` for the first VF representor and `_1` for the second V representor, and so on.

   ***Note*:**The total number of VF representor interfaces created are based on the sriov_numvfs value configured.

   ```bash
   ip link show | grep <PF_interface>
   ```

   For example, `ip link show | grep <u25eth0>`.

   ***Note*:** Here u25eth0 is the PF interface and u25eth0_0 is the VF representor interface.

   Now make the VF representor interface up using the ifconfig command:

   ```bash
   ifconfig <vf_rep_interface> up
   ```

Step 9. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.html#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, proceed to the next step.

Step 10. Add PF interfaces as ports to the OVS bridge:

    ```bash
    ovs-vsctl add-port br0 <x2 interface>
    ```

    For example, `ovs-vsctl add-port br0 U25eth0`.

Step 11. Add a VF representor interface as a port to the OVS bridge:

    ```bash
    ovs-vsctl add-port <bridge-name> <VF rep interface>
    ```

    For example, `ovs-vsctl add-port br0 U25eth0_0`.

Step 12. Ensure that the the OVS bridge is up by running the following command:

    ```bash
    ifconfig <bridge-name> up
    ```

Step 13. Print a brief overview of the database contents:

    ```bash
    ovs-vsctl show
    ```

Step 14. Refer [VM Installation](./ug1534-vminstall.html#vm-installation) to instantiate the VM. Post successful instantiation follow the Step 15 to validate its functionality.

Step 15. Refer to [Functionality Check](./ug1534-detailedappsdescriptions.html#functionality-check) to check OVS functionality.

### VM to VM

***Note*:** To have this configuration SR-IOV must be enabled in BIOS. For a VM to VM configuration, a tunnel- VXLAN or L2GRE could be created with two server setups.

*Figure 9:* **VM to VM setup**

![seems messed up a little bit X25551-090721](media/cxa1631064658045_LowRes.png)

Step 1. Refer to [U25N Driver](./ug1534-installation.html#u25n-driver) for the required OS/software version. For VM use cases, VFs need to be enabled on the corresponding PF and should be bound to the VM. The desired number VFs counts should be configured using sfboot command and in sriov_numvfs. Offload will happen between VMs created using same the PF's VF.

   ***Note*:** For more information, refer to sfboot Configuration.

Step 2. Validate the driver version of the U25N interface using the following command:

   ***Note*:** Ignore this step if the U25N driver version is 5.3.3.1008.3

   ```bash
   ethtool -i U25_eth0 | grep version
   ```

   ***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.html#u25n-driver).

   ***Note*:** Refer  [Deployment Image Flashing](./ug1534-installation.html#deployment-image-flashing) for flashing images to check OVS functionality.

Step 3. Make the U25N X2 PF interface up:

   1. List the PF interface using the ifconfig -a command.

   2. Find the U25N PF interface using the `ethtool -i <interface_name>` command.

      For example:

      ```bash
      ethtool -i <U25_eth0>
      ```

      driver: sfc

      version: 5.3.3.1008.3

      `ifconfig <U25_interface> up`

      For example:

      ```bash
      ifconfig U25eth0 up
      ```

Step 4. Enable the desired VFs on the corresponding PF. In this following commands, two VFs are enabled the PF0 interface:

   ***Note*:** The VF could also be enabled in the PF1 interface based on the use case. The sriov_numvfs count should be less than or equal to the VF count specified in the sfboot command. The sriov_numvfs should be enabled only in legacy mode. To validate the U25N mode, execute the following steps.

   ```bash
   devlink dev eswitch show pci/0000:<pci_id>
   ```

   The output of the above command would be:

   ```bash
   pci/0000:af:00.0 mode legacy
   ```

   In case it is not in legacy mode, change it to legacy mode using the following command:

   ```bash
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
   echo 2 > /sys/class/net/<interface>/device/sriov_numvfs
   ```

   For example:

   ```bash
   echo 1 /sys/class/net/U25_eth0/device/sriov_numvfs
   ```

   ***Note*:** Post executing the above mentioned command, two VF PCIe IDs and two VF interfaces are created. The VF PCIe device ID can be listed with the `lspci -d 1924:1b03` command. An example of the device ID is 
```bash
   af:00.2 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (Virtual Function) (rev 01)* and *af:00.3 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (Virtual Function) (rev 01)*.
These VF PCIe IDs will be used for binding VFs to VMs.
```

Step 5. The two VF interfaces can be found using the `ifconfig -a` command. To differentiate VF from PF, use the `ip link show` command. This gives the VF interface ID and VF interface mac address under the PF interface.

Step 6. Ensure that the two VF interfaces are up by executing the following command:

   ```bash
   ifconfig <vf_interfaceup>
   ```

Step 7. Ensure that the PF interfaces are in switchdev mode by executing the following command.

   ***Note*:** Ensure the PF interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives the PCIe device bus ID:

   ```bash
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example, `devlink dev eswitch set pci/0000:af:00.0 mode switchdev`.

Step 8. Execution of above mentioned command results in the creation of two VF representor interfaces. The VF representor interface name will have PF interface name followed by `_0` for the first VF representor and `_1` for the second VF representor and so on and so forth.

   ***Note*:** The number of VF representor interfaces created are based on the sriov_numvfs value configured.

   ip link show | grep <PF_interface>

   For example, `<ip link show | grep <u25eth0>`.

   ```bash
   u25eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP
   mode DEFAULT group default qlen 1000
   u25eth0_0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel
   master ovs-system state UP mode DEFAULT group default qlen 1000
   u25eth0_1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel
   master ovs-system state UP mode DEFAULT group default qlen 1000
   ```

   ***Note*:** Here u25eth0 is the PF interface, and u25eth0_0 and u25eth0_1 are the VF representor interfaces.

   Now make the VF representor interfaces up using the ifconfig command:

   ```bash
   ifconfig <vf_rep_interface> up
   ```

Step 9. Follow the steps mentioned in OVS Configuration to create an OVS bridge. After creating the OVS bridge, proceed to the next step.

Step 10. Add two VF representor interfaces to the OVS bridge:

    ```bash
    ovs-vsctl add-port <bridge-name> <VF rep interface 1>
    ovs-vsctl add-port <bridge-name> <VF rep interface 2>
    ```

    For example:

    ```
    ovs-vsctl add-port br0 u25eth0_0
    ovs-vsctl add-port br0 u25eth0_1
    ```

Step 11. Make the OVS bridge up:

    ```bash
    ifconfig <bridge-name> up
    ```

Step 12. Print the brief overview of the database contents:

    ```bash
    ovs-vsctl show
    ```

Step 13. Refer [VM Installation](./ug1534-vminstall.md) to instantiate the VM. Post VM instantiation, proceed to the next step to validate the functionality.

Step 14. Refer [Functionality Check(link)] to validate OVS functionality.

### 4.2.6 Tunnels (Encapsulation/Decapsulation)

U25N Smart NIC supports offloading of tunnels using encapsulation and decapsulation actions.

- **Encapsulation:** Pushing tunnel header is supported on TX

- **Decapsulation:** Stripping tunnel header is supported on RX

Supported tunnels:

- VXLAN

- L2GRE

#### L2GRE

***Note*:** For Port to VM or VM to Port or VM to VM case, a tunnel could be created with two server setup. The following section demonstrates creating a L2GRE based tunnel.

- Maximum tunnel support = 1K

- Maximum supported flows = 8K

- Maximum MTU size = 1400

Refer [Basic Requirements and Component Versions Supported](./ug1534-installation.html#basic-requirements-and-component-versions-supported) for the required OS/software version. An L2GRE tunnel can be formed between two servers. Tunnel endpoint IP should be added to the PF interface which acts as a origin of the tunnel.

***Note*:** Two tunnels could be formed between two PFs of U25N SmartNICs for two different servers for the VM to VM case.

*Figure 10:* **L2GRE End to End setup with OVS functionality**

![X25553-091321](media/idz1631575855541_LowRes.png)

##### **Server 1 Configuration**

Step 1. Validate the driver version of the U25N interface by executing the following command:

   ***Note*:** Ignore this step if the U25N driver version is 5.3.3.1008.3.

   ```bash
   ethtool -i U25_eth0 | grep version
   ```

   ***Note*:** To install the latest sfc driver, refer to U25N Driver Installation section.

   ***Note*:** Refer Deployment Image Flashing for flashing images and to validate OVS functionality.

Step 2. Ensure that the U25N PF interfaces are up:

   List the PF interfaces using the `ifconfig -a` command. Find the U25N PF interface using the `ethtool -i <interface_name>` command.

   For example:

   ```bash
   ethtool -i <U25_eth0>
   ```

   driver: sfc

   version: 5.3.3.1008.3

   ```bash
   ifconfig <U25_interface> up
   ```

   For example:

   ```bash
   ifconfig U25eth0 up
   ifconfig U25eth1 up
   ```

Step 3. Assign tunnel IP to PF0 interface by executing following command:

   ```bash
   ifconfig <interface_1> <ip> up
   ```

   For example, ifconfig U25eth0 10.16.0.2/24 up.

Step 4. Ensure that the PF0 interface is in switchdev mode.

   ***Note*:** Make sure the PF0 interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives the PCIe device bus ID.

   ```bash
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example:

   ```bash
   devlink dev eswitch set pci/0000:af:00.0 mode switchdev
   ```

Step 5. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.html#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, proceed to the next step.

Step 6. Create GRE interfaces bby executing following commands:

   ```bash
   ovs-vsctl add-port <bridge_name> gre0 -- set interface gre0 type=gre
   options:local_ip=<ip_address> options:remote_ip=<ip_address>
   ```

   For example:

   ```bash
   ovs-vsctl add-port br0 gre0 -- set interface gre0 type=gre
   options:local_ip=10.16.0.2 options:remote_ip=10.16.0.1
   ```

Step 7. Add a PF1 interface as a port to the OVS bridge:

   ```bash
   ovs-vsctl add-port <bridge-name> <U25N interface_2>
   ```

   For example:

   ```bash
   ovs-vsctl add-port br0 U25eth1
   ```

Step 8. Ensue the OVS bridge is up:

   ```bash
   ifconfig <bridge_name> up
   ```

   For example:

   ```bash
   ifconfig br0 up
   ```

Step 9. Print a brief overview of the database contents:

   ```bash
   ovs-vsctl show
   ```

##### Server 2 Configuration

Step 1. Check the driver version of the U25N interface using this command:

   ***Note*:** Ignore this step if the U25N X2 driver version is 5.3.3.1008.3

   ```bash
   ethtool -i U25_eth0 | grep version
   ```

   ***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.html#u25n-driver) installation section.

Step 2. Ensure that U25N PF interfaces are up:

   List the PF interfaces using the `ifconfig -a` command. Find the U25N PF interface using the `ethtool -i <interface_name>` command.

   For example:

   ```bash
   ethtool -i <U25_eth0>
   ```

   driver: sfc

   version: 5.3.3.1000

   ```bash
   ifconfig <U25_interface> up
   ```

   For example:

Step 3. Assign tunnel IP to PF0 interface by executing the following command:

   ```bash
   ifconfig <interface_1> <ip> up
   ```

   For example:

   ```bash
   ifconfig U25eth0 10.16.0.1/24 up
   ```

Step 4. Ensure that PF0 interface is in switchdev mode.

   ***Note*:** Make sure the PF0 interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives the PCIe device bus ID.

   ```bash
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example:

   ```bash
   devlink dev eswitch set pci/0000:af:00.0 mode switchdev
   ```

Step 5. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.html#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, proceed to the next step.

Step 6. Create GRE interfaces:

   ```bash
   ovs-vsctl add-port <bridge_name> gre0 -- set interface gre0 type=gre
   options:local_ip=<ip_address> options:remote_ip=<ip_address>
   ```

   For example:

   ```bash
   ovs-vsctl add-port br0 gre0 -- set interface gre0 type=gre
   options:local_ip=10.16.0.1 options:remote_ip=10.16.0.2
   ```

Step 7. Add a PF1 interface as a port to the OVS bridge:

   ```bash
   ovs-vsctl add-port <bridge_name> <U25N interface_2>
   ```

   For example:

   ```bash
   ovs-vsctl add-port br0 U25eth1
   ```

Step 8. Ensure that the bridge up:

   ```bash
   ifconfig <bridge_name> up
   ```

   For example:

   ```bash
   ifconfig br0 up
   ```

STep 9. Print a brief overview of the database contents:

   ```bash
   ovs-vsctl show
   ```

Step 10. Refer [Functionality Check](./ug1534-detailedappsdescriptions.html#functionality-check) to check OVS functionality.

#### 4.2.6.2 VXLAN

***Note*:** For Port to VM or VM to Port or VM to VM configuration, a tunnel could be created with two server setups. The following section demonstrates creating a  VXLAN based tunnel.

- Maximum tunnel support = 1K

- Maximum supported flows = 8K

- Maximum MTU size = 1400

Refer  [Basic Requirements and Component Versions Supported](./ug1534-installation.html#basic-requirements-and-component-versions-supported) for the required OS/software version. A VXLAN tunnel can be formed between two servers. Tunnel endpoint IP should be added to the PF interface where the tunnel needs to be created.

*Figure 11:* **VXLAN**

![X25554-091321](media/rcv1631576075856_LowRes.png)

##### Server 1 Configuration

Step 1. Validate the driver version of the U25N interface using this command:

   ***Note*:** Ignore this step if the U25N X2 driver version is 5.3.3.1008.3

   ```bash
   ethtool -i U25_eth0 | grep version
   ```

   ***Note*:** To install the latest sfc driver, refer [U25N Driver](./ug1534-installation.html#u25n-driver).

   ***Note*:** Refer to Deployment Image Flashing for flashing images to check OVS functionality.

Step 2. Ensure that the the U25N PF interfaces are up:

   List the PF interfaces using the ifconfig -a command. Find the U25N PF interface using the `ethtool -i <interface_name>` command.

   For example:

   ```bash
   ethtool -i <U25_eth0>
   ```

   driver: sfc

   version: 5.3.3.1008.3

   ```bash
   ifconfig <U25_interface> up
   ```

   For example:

   ```bash
   ifconfig U25eth0 up
   ifconfig U25eth1 up
   ```

Step 3. Assign tunnel IP to PF0 interface:

   ```bash
   ifconfig <interface_1> <ip> up
   ```

   For example:

   ```bash
   ifconfig U25eth0 10.16.0.2/24 up
   ```

Step 4. Ensure that PF0 interface is in switchdev mode.

   ***Note*:** Make sure the PF0 interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives the PCIe device bus ID.

   ```bash
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example, `devlink dev eswitch set pci/0000:af:00.0 mode switchdev`.

Step 5. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.html#ovs-configuration) to create an OVS bridge. Post OVS bridge creation, proceed to the next step.

Step 6. Create VXLAN interfaces:

   ```bash
   ovs-vsctl add-port br0 vxlan0 -- set interface vxlan0 type=vxlan
   options:local_ip=<ip_address> options:remote_ip=<ip_address>
   options:key=<key_id>
   ovs-vsctl add-port br0 vxlan0 -- set interface vxlan0 type=vxlan
   options:local_ip=10.16.0.2 options:remote_ip=10.16.0.1 options:key=123
   ```

Step 7. Add a PF1 interface as a port to the OVS bridge:

   ```bash
   ovs-vsctl add-port <bridge-name> <U25N interface_2>
   ```

   For example, `ovs-vsctl add-port br0 U25eth1`.

Step 8. Ensure that the bridge is up:

   ```bash
   ifconfig <bridge_name> up
   ```

   For example:

   ```bash
   ifconfig br0 up
   ```

Step 9. Print a brief overview of the database contents:

   ```bash
   ovs-vsctl show
   ```

##### Server 2 Configuration

Step 1. Check the driver version of the U25N X2 interface using this command:

   ***Note*:** Ignore this step if the U25N driver version is 5.3.3.1008.3

   ```bash
   ethtool -i U25_eth0 | grep version
   ```

   This should give the output as version 5.3.3.1008.3

   ***Note*:** To install the latest sfc driver, refer to U25N Driver.

Step 2. Ensure that the U25N PF interfaces are up:

   List the PF interfaces using the `ifconfig -a` command. Find the U25N PF interface using the `ethtool -i <interface_name>` command.

   For example:

   ```bash
   ethtool -i <U25_eth0>
   ```

   driver: sfc

   version: 5.3.3.1008.3

   ```bash
   ifconfig <U25_interface> up
   ```

   For example:

   ```bash
   ifconfig U25eth0 up
   ifconfig U25eth1 up
   ```

Step 3. Assign tunnel IP to PF0 interface:

   ```bash
   ifconfig <interface_1> <ip> up
   ```

   For example:

   ```bash
   ifconfig U25eth0 10.16.0.1/24 up
   ```

Step 4. Ensure that PF0 interface is in switchdev mode.

   ***Note*:** Make sure the PF0 interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives the PCIe device bus ID.

   ```bash
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example:

   ```bash
   devlink dev eswitch set pci/0000:af:00.0 mode switchdev
   ```

Step 5. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.html#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, proceed to the next step.

Step 6. Create VXLAN interfaces:

   ```bash
   ovs-vsctl add-port br0 vxlan0 -- set interface vxlan0 type=vxlan
   options:local_ip=<ip_address> options:remote_ip=<ip_address>
   options:key=<key_id>
   ovs-vsctl add-port br0 vxlan0 -- set interface vxlan0 type=vxlan
   options:local_ip=10.16.0.1 options:remote_ip=10.16.0.2 options:key=123
   ```

Step 7. Add a PF1 interface as a port to the OVS bridge:

   ```bash
   ovs-vsctl add-port <bridge_name> <U25N interface_2>
   ```

   For example:

   ```bash
   ovs-vsctl add-port br0 U25eth1
   ```

Step 8. Ensure the bridge is up:

   ```bash
   ifconfig <bridge_name> up
   ```

   For example:

   ```bash
   ifconfig br0 up
   ```

Step 9. Print a brief overview of the database contents:

   ```bash
   ovs-vsctl show
   ```

STep 10. Refer to [Functionality Check](./ug1534-detailedappsdescriptions.html#functionality-check) to check OVS functionality.

#### 4.2.6.3 VM to VM or VM to Port or Port to VM Tunnel

- Maximum tunnel support = 1K

- Maximum supported flows = 8K

- Maximum MTU size = 1400

Refer to [Basic Requirements and Component Versions Supported](./ug1534-installation.html#basic-requirements-and-component-versions-supported). A VXLAN tunnel can be created between two servers. Tunnel endpoint IP should be added to the PF interface whcih acts as the starting point of the tunnel.

*Figure 12:* **Tunneling/Detunneling setup**

![X25696-090721](./media/ash1631066523492_LowRes.png)

##### Server 1 Configuration

Step 1. Check the driver version of the U25N interface using this command:

   ***Note*:** Ignore this step if the U25N X2 driver version is 5.3.3.1008.3

   ```
   ethtool -i U25_eth0 | grep version
   ```

   ***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.html#u25n-driver).

   ***Note*:** Refer to [Deployment Image Flashing](./ug1534-installation.html#deployment-image-flashing) for flashing images to check OVS functionality.

Step 2. Ensure that the U25N PF interfaces are up:

   List the PF interfaces using the `ifconfig -a` command. Find the U25N PF interface using the `ethtool -i <interface_name>` command.

   For example:

   ```
   ethtool -i <U25_eth0>
   ```

   Driver: sfc

   Version: 5.3.3.1008.3

   ```
   ifconfig <U25_interface> up
   ```

   For example:

   ```
   ifconfig U25eth0 up
   ifconfig U25eth1 up
   ```

Step 3. Assign tunnel IP to PF0 and PF1 interfaces:

   ```
   ifconfig <interface_1> <ip> up
   ifconfig <interface_2> <ip> up
   ```
   
   For example:

   ```
   ifconfig <interface_1> <ip> up
   ifconfig <interface_2> <ip> up
   ```

Step 4. Enable the desired the number of VF on the corresponding PF. Here, a single VF is enabled on each PF0 and PF1 interfaces:

   ***Note*:** The sriov_numvfs count should be less than or equal to VF count specified in sfboot command. The sriov_numvfs should be enabled only in legacy mode. To validate U25N mode, please follow the below steps.

   ```bash
   devlink dev eswitch show pci/0000:<pci_id>
   ```

   The output of the above command would be:

   ```bash
   pci/0000:af:00.0 mode legacy
   ```

   If not in legacy mode, change it to legacy mode using the following command:

   ```bash
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
   echo 1 > /sys/class/net/<interface>/device/sriov_numvfs
   ```

   For example:

   ```bash
   echo 1 > /sys/class/net/U25_eth0/device/sriov_numvfs
   echo 1 > /sys/class/net/U25_eth1/device/sriov_numvfs
   ```

   ***Note*:** Post executing the above mentioned command, a VF PCIe ID and a VF interface will get created corresponding to each PF0 and PF1.
    The VF PCIe device ID can be listed with the `lspci -d 1924:1b03` command. An example of the device ID is
    ```bash
     af:00.2 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (Virtual Function) (rev 01) and af:00.6 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (Virtual Function) (rev 01). These VF PCIe IDs would be used for binding VFs to VMs.
     ```bash

Step 5. The two VF interfaces can be found using the `ifconfig -a` command. To differentiate VF from PF, use the `ip link show` command. This gives the VF interface ID and VF interface mac address under the PF interface.

Step 6. Ensure that the two VF interfaces up:

   ```bash
   ifconfig <vf_interface> up
   ```

Step 7. Ensure that the PF0 and PF1 interfaces are in switchdev mode.

   ***Note*:** Make sure the PF0 and PF1 interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives the PCIe device bus ID:

   ```bash
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example:

   ```bash
   devlink dev eswitch set pci/0000:af:00.0 mode switchdev
   devlink dev eswitch set pci/0000:af:00.1 mode switchdev
   ```

Step 8. Post execution of the above mentioned command a VF representor interface corresponding to each PF interface will get created. The VF representor interface name will have PF interface name followed by `_0`.

   ***Note*:** Here, the number of VF representors created are based on the sriov_numvfs value configured.

   ```bash
   ip link show | grep <PF_interface>
   ```

   For example, `ip link show | grep <u25eth0>`.

   ```bash
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

   ```bash
   ifconfig <vf_rep_interface> up
   ```

Step 9. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.html#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, proceed to the next step.

Step 10. Creating VXLAN/GRE interfaces:

    ***Note*:** The following configuration is for the VXLAN. Similarly, the GRE tunnel could also be used.

    ```bash
    ovs-vsctl add-port br0 vxlan0 -- set interface vxlan0 type=vxlan
    options:local_ip=<ip_address> options:remote_ip=<ip_address>
    options:key=<key_id>
    ovs-vsctl add-port br1 vxlan1 -- set interface vxlan0 type=vxlan
    options:local_ip=<ip_address> options:remote_ip=<ip_address>
    options:key=<key_id>
    ```

    For example:

    ```bash
    ovs-vsctl add-port br0 vxlan0 -- set interface vxlan0 type=vxlan
    options:local_ip=10.16.0.2 options:remote_ip=10.16.0.1 options:key=123
    ovs-vsctl add-port br1 vxlan1 -- set interface vxlan type=vxlan
    options:local_ip=10.16.0.3 options:remote_ip=10.16.0.4 options:key=456
    ```

Step 11. Execute the following command to add VF representor enabled on each PF interface to a separate OVS bridge.

    ```bash
    ovs-vsctl add-port <bridge-name_0> <VF rep interface 1>
    ovs-vsctl add-port <bridge-name_1> <VF rep interface 2>
    ```

    For example:

    ```bash
    ovs-vsctl add-port br0 u25eth0_0
    ovs-vsctl add-port br1 u25eth1_0
    ```

Step 12. Ensure the the two bridges are up:

    ```bash
    ifconfig <bridge_name> up
    ```

    For example:

    ```bash
    ifconfig br0 up
    ifconfig br1 up
    ```

Step 13. Print a brief overview of the database contents:

    ```bash
    ovs-vsctl show
    ```

Step 14. Refer [VM Installation](./ug1534-vminstall.md) to instantiate the VM.

##### Server 2 Configuration

Step 1. Check the driver version of the U25N interface using this command:

   ***Note*:** Ignore this step if the U25N X2 driver version is 5.3.3.1008.3

   ```bash
   ethtool -i U25_eth0 | grep version
   ```

   This should give the output as version 5.3.3.1008.3

   ***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.html#u25n-driver).

   ***Note*:** Refer [Deployment Image Flashing](./ug1534-installation.html#deployment-image-flashing) for flashing images to check OVS functionality.

Step 2. Ensure that U25N PF interfaces are up:

   List the PF interfaces using the `ifconfig -a` command. Find the U25N PF interface using the `ethtool -i <interface_name>`command.

   For example:

   ```bash
   ethtool -i <U25_eth0>
   ```

   Driver: sfc

   Version: 5.3.3.1008.3

   ```bash
   ifconfig <U25_interface> up
   ```

   For example:

   ```bash
   ifconfig U25eth0 up
   ifconfig U25eth1 up
   ```

Step 3. Assign tunnel IP to PF0 and PF1 interfaces:

   ```bash
   ifconfig <interface_1> <ip> up
   ifconfig <interface_2> <ip> up
   ```

   For example:

   ```bash
   ifconfig U25eth0 10.16.0.1/24 up
   ifconfig U25eth1 10.16.0.4/24 up
   ```

Step 4. Enable the desired number of VFs to the corresponding PF. Here, one VF is enabled on each PF0 and PF1 interface:

   ***Note*:** The sriov_numvfs count should be less than or equal to VF count assigned in sfboot command. The sriov_numvfs should be enabled only in legacy mode. To validate mode, please follow the below steps.

   ```bash
   devlink dev eswitch show pci/0000:<pci_id>
   ```

   The output of the above command would be:

   ```bash
   pci/0000:af:00.0 mode legacy
   ```

   In case not in legacy mode, change it to legacy mode using the following command:

   ```bash
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
   echo 1 > /sys/class/net/<interface>/device/sriov_numvfs
   ```

   For example:

   ```bash
   echo 1 > /sys/class/net/U25_eth0/device/sriov_numvfs
   echo 1 > /sys/class/net/U25_eth1/device/sriov_numvfs
   ```

   ***Note*:** Post execution of the above mentioned command, a VF PCIe ID and a VF interface will get created corresponding to each PF0 and PF1. The VF PCIe device ID can be listed with the `lspci -d 1924:1b03` command. An example of the device ID is af:00.2 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (Virtual Function) (rev 01) and af:00.6 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (Virtual Function) (rev 01). These VF PCIe IDs would be used for binding VFs to VMs.

Step 5. The two VF interfaces can be found using the `ifconfig -a` command. To differentiate VF from PF, use the `ip link show` command. This gives the VF interface ID and VF interface mac address under the PF interface.

Step 6. Ensure that two VF interfaces are up:

   ```bash
   ifconfig <vf_interface> up
   ```

Step 7. Ensure that PF0 and PF1 interfaces are in switchdev mode.

   ***Note*:** Make sure the PF0 and PF1 interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives the PCIe device bus ID:

   ```bash
   devlink dev eswitch set pci/0000:<PCIe device bus idm ode switchdev
   ```

   For example:

   ```bash
   devlink dev eswitch set pci/0000:af:00.0 mode switchdev
   devlink dev eswitch set pci/0000:af:00.1 mode switchdev
   ```

Step 8. Running the above command creates a VF representor interface corresponding to each PF interface. The VF representor interface name will have the PF interface name followed by `_0`.

   ***Note*:** The total number of VF representor created is based on the sriov_numvfs value configured.

   ```bash
   ip link show | grep <PF_interface>
   ```

   For example, `ip link show | grep <u25eth0>`.

   ***Note*:** Here u25eth0 and u25eth1 are the PF interfaces, and u25eth0_0 and u25eth1_0 are the VF representor interfaces.

   Ensure the VF representor interfaces are up using the ifconfig command:

   ```bash
   ifconfig <vf_rep_interface> up
   ```

Step 9. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.html#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, proceed to the next step.

Step 10. Create VXLAN/GRE interfaces:

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

Step 11. Adding VF representor of each PF interface to separate OVS bridges.

    ```bash
    ovs-vsctl add-port <bridge-name_0> <VF rep interface 1>
    ovs-vsctl add-port <bridge-name_1> <VF rep interface 2>
    ```

    For example:

    ```
    ovs-vsctl add-port br0 u25eth0_0
    ovs-vsctl add-port br1 u25eth1_0
    ```

Step 12. Ensure that the two bridges are up:

    ```bash
    ifconfig <bridge_name> up
    ```

    For example:

    ```
    ifconfig br0 up
    ifconfig br1 up
    ```

Step 13. Print a brief overview of the database contents:

    ```bash
    ovs-vsctl show
    ```

Step 14. Refer to [VM Installation](./ug1534-vminstall.md) to instantiate the VM. Post successful instantiation refer [Functionality Check](./ug1534-detailedappsdescriptions.html#functionality-check) to validate  functionality.

### 4.2.7 OVS Configuration

Step 1. Export the OVS path:

   ```
   export PATH=$PATH:/usr/local/share/openvswitch/scripts
   export PATH=$PATH:/usr/local/bin
   ```

Step 2. Stop OVS and remove any database to get rid of old configurations:

   ```
   ovs-ctl stop
   rm /usr/local/etc/openvswitch/conf.db
   ```

Step 3. Start OVS:

   ```
   ovs-ctl start
   ```

Step 4. Enable hardware offload:

   ```
   ovs-vsctl set Open_vSwitch . other_config:hw-offload=true
   ovs-vsctl set Open_vSwitch . other_config:tc-policy=none
   ```

Step 5. After adding the hardware offload policy, restart OVS:

   ```
   ovs-ctl restart
   ```

Step 6. Set OVS log levels (for debug purpose only, if needed):

   ```
   ovs-appctl vlog/set ANY:ANY:dbg
   ovs-appctl vlog/set poll_loop:ANY:OFF
   ovs-appctl vlog/set netlink_socket:ANY:OFF
   ```

Step 7. Set the maximum time (in ms) that idle flows remain cached in the datapath:

   ```
   ovs-vsctl set open_vswitch $(ovs-vsctl list open_vswitch | grep _uuid |
   cut -f2 -d ":" | tr -d ' ') other_config:max-idle=30000000g
   ```

Step 8. Print a brief overview of the database contents:

   ```
   ovs-vsctl show
   ```

   The output should be:

   ```
   <git_version>
   ovs_version: "<ovs_version>"
   ```

   ***Note*:** OVS versions 2.12 and 2.14 have been tested.

Step 9. Adding bridge to OVS:

   ```
   ovs-vsctl add-br <bridge-name>

   For Example
   ovs-vsctl add-br br0
   ```

   ***Note*:** For VM to VM or VM to Port or Port to VM Tunnel alone create two OVS bridges. For example, `ovs-vsctl add-br br0` and `ovs-vsctl add-br br1`.

### 4.2.8 Functionality Check

After adding the U25N network interfaces to the OVS bridge, the functionality can be verified using ping, iperf, and dpdk network performance tools.

#### Ping Test

Step 1. Assign the IP address to the respective interface and do a ping using the following command:

   ```
   ping <remote_ip>
   ```

Step 2. After a successful ping test, iperf can be used:

   ***Note*:** For VXLAN and L2GRE, set the MTU size to 1400 before
running iperf3 or pktgen on a particular interface.

   ```
   ifconfig <interfacemtu 1400 [as root]
   ```

Step 3. Run iperf3 -s on the host device [iperf server].

Step 4. Run iperf3 -c <ip> on a remote device [iperf client].

***Note*:** Refer to [DPDK on U25N](./ug1534-supportedservices.html#dpdk-on-u25n) to run dpdk-testpmd.

## IPsec

### 4.3.1 Supported XFRM Parameters

IPsec tunnels are created between two servers. Because IPsec is in *transport mode*, L2GRE is used to create tunnels. The strongSwan application runs in user space. The charon plugin of strongSwan is used to offload rules on the U25N. Packets reaching the IPsec module should be L2GRE encapsulated.

- Encryption algorithm: AES-GCM 256 encryption/decryption

- IPsec mode: Transport mode.

- Maximum IPsec tunnel supported: 32

### 4.3.2 Classification Fields (Matches)

#### 4.3.2.1 Encryption

```bash
Key

1. IPv4 source address

2. IPv4 destination address

3. IP4 protocol Action

Actions

1. Action flag

2. SPI

3. Key

4. IV
```

#### 4.3.2.2 Decryption

```bash
Keys

1. IPv4 source address

2. IPv4 destination address

3. SPI (Security Parameter Index)

Actions

1. Decryption key

2. IV
```

### 4.3.3 strongSwan Installation

Execute the following commands as a root user:

```bash

- apt-get install aptitude

- aptitude install opensc

- aptitude install libgmp10

- aptitude install libgmp-dev

- apt-get install libssl-dev
```

***Note*:** Before installing the Debian package for strongSwan, make sure all the dependencies are installed.

Step 1. Refer [Basic Requirements and Component Versions Supported](./ug1534-installation.html#basic-requirements-and-component-versions-supported) for the required OS/software version.

Step 2. Use the following command to validate the version of the strongSwan Debian package. Ignore this step if it shows the version as strongswan_5.8.4

   The version can be found using the command `sudo swanctl --version`.

   Remove the already installed package before installing the latest one:

   ```bash
   dpkg -r strongswan_5.8.4-1_amd64
   dpkg -i strongswan_5.8.4-1_amd64.deb
   ```

Step 3. After installing the strongSwan package, create a CA certificate. CA certificate can be created in one server and can be copied to the other server.

#### 4.3.3.1 Server 1 Configuration

Step 1. Generating a self-signed CA certificate using the PKI utility of strongSwan:

   ```text
   cd /etc/ipsec.d
   ipsec pki --gen --type rsa --size 4096 --outform pem > private/
   strongswanKey.pem
   ipsec pki --self --ca --lifetime 3650 --in private/strongswanKey.pem --
   type rsa --dn "C=CH, O=strongSwan, CN=Root CA" --outform pem > cacerts/
   strongswanCert.pem
   ```

Step 2. After the key and certificate are generated in server 1, copy them to server 2 in the same directory.
```text
    a. Copy the file `strongswanKey.pem` present in `/etc/ipsec.d/private/` from the first server to the second server at the same location.

    b. Copy the file `strongswanCert.pem` presenti n `/etc/ipsec.d/cacerts/strongswanCert.pem` from the first server to the second server at the same location.

   After finishing the above, create a key pair and certificate for each server separately as root.
```
Step 3. Generate the key pair and certificate in server 1 as a root user:

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

Step 4. Configure the conf file and secret file in server 1:

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

   ***Note*:** There is a white space, present between `:` and `RSA`.

#### 4.3.3.2 Server 2 Configuration

Step 1. Generate the key pair and certificate in server 2 as root:

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

Step 2. Configure the conf file and secret file in server 2:

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

   ***Note*:**  There is a white space, present between `:` and `RSA`.

#### 4.3.3.3 Server 1: Steps to Run IPsec

Step 1. Validate the driver version of the U25N interface using the following command:

   ```
   ethtool -i U25_eth0 | grep version
   ```

   ***Note*:** Ignore this step if the U25N driver version is 5.3.3.1008.3


  ***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.html#u25n-driver).
  **Note*:** Either PF0 or PF1 interface could act as a Tunnel endpoint . In the following example we have assumed PF0 as a tunnel endpoint.

Step 2. Ensure that the PF1 interface is up:

   ```
   ifconfig <interface_2> up
   ```

   For example, `ifconfig U25eth1 up`.

Step 3. Esnure that the PF0 interface is up and assign a tunnel endpoint IP to it:

   ```
   ifconfig <interface_1> <ip> up
   ```

   For example, `ifconfig U25eth0 10.16.0.2/24 up`.

Step 4. Esnure that  PF0 interface is in switchdev mode.

   ***Note*:** Make sure the PF0 interface link is up before doing switchdev mode.

   The lspci | grep Sol command gives the PCIe device bus ID.

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example, `devlink dev eswitch set pci/0000:af:00.0 mode switchdev`.

Step 5. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.html#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, proceed with the following steps.

Step 6. Create GRE interfaces:

   ```
   ovs-vsctl add-port br0 gre0 -- set interface gre0 type=gre
   options:local_ip=<ip_address> options:remote_ip=<ip_address>
   ovs-vsctl add-port br0 gre0 -- set interface gre0 type=gre
   options:local_ip=10.16.0.2 options:remote_ip=10.16.0.1
   ```

Step 7. Add the PF1 interface OVS bridge:

   ```
   ovs-vsctl add-port br0 <U25N interface_2>
   eg:ovs-vsctl add-port br0 U25eth1
   ```

Step 8. Print a brief overview of the database contents:

   ```
   ovs-vsctl show
   ```

Step 9. Ensure that the bridge is up:

   ```
   ifconfig <bridge_name> up
   ```

   For example, `ifconfig br0 up`.

Step 10. Enable ipsec offload in the driver:

    ```
    echo 1 >> /sys/class/net/<U25N_interface_1>/device/ipsec_enable
    ```

    For example, `echo 1 >> /sys/class/net/U25eth0/device/ipsec_enable`.

Step 11. Start IPsec:

    ```
    sudo ipsec restart
    ```

#### 4.3.3.4 Server 2: Steps to Run IPsec

Step 1. Validate the driver version of the U25N interface using the following command:

   ```
   ethtool -i U25_eth0 | grep version
   ```

   ***Note*:** Ignore this step if the U25N driver version is 5.3.3.1008.3


   ***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.html#u25n-driver).

Step 2. Ensure that the PF1 interface up:

   ```
   ifconfig <interface_2> up
   ```

   For example, `ifconfig U25eth1 up`.

Step 3. Ensure that the PF0 interface is up and assign a tunnel IP to it:

   ```
   ifconfig <interface_1> <ip> up
   ```

   For example, `ifconfig U25_eth0 10.16.0.1/24up`.

Step 4. Ensure that the PF0 interface is into switchdev mode:

   ***Note*:** Make sure the PF0 interface link is up before doing switchdev mode.

   The `lspci | grep Sol` command gives us the PCIe device bus ID.

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode switchdev
   ```

   For example, `devlink dev eswitch set pci/0000:af:00.0 mode switchdev`.

Step 5. Follow the steps mentioned in [OVS Configuration](./ug1534-detailedappsdescriptions.html#ovs-configuration) to create an OVS bridge. After creating the OVS bridge, proceed with the following steps.

Steo 6. Create GRE interfaces:

   ```
   ovs-vsctl add-port br0 gre0 -- set interface gre0 type=gre
   options:local_ip=<ip_address> options:remote_ip=<ip_address>
   ovs-vsctl add-port br0 gre0 -- set interface gre0 type=gre
   options:local_ip=10.16.0.1 options:remote_ip=10.16.0.2
   ```

Step 7. Add PF1 interface OVS bridge:

   ```
   ovs-vsctl add-port br0 <U25N interface_2>
   eg:ovs-vsctl add-port br0 U25eth1
   ```

Step 8. Print a brief overview of the database contents:

   ```
   ovs-vsctl show
   ```

Step 9. Ensure that the bridge is up.

   ```
   ifconfig <bridge_name> up
   ```

   For example, `ifconfig br0 up`.

Step 10. Enable ipsec offload in the driver:

    ```
    echo 1 >> /sys/class/net/<U25N_interface_1>/device/ipsec_enable
    ```

    For example, `echo 1 >> /sys/class/net/U25eth0/device/ipsec_enable`.

Step 11. Start IPsec:

    ```
    sudo ipsec restart
    ```

    Refer [Functionality Check](./ug1534-detailedappsdescriptions.html#functionality-check) to validate OVS functionality.

*Figure 13:* **IPsec + OVS End to End setup Diagram**

![X25555-091321](media/jqy1631576791246_LowRes.png)

### 4.3.5 Changing IPsec Tunnel Endpoints

***Note*:** In the below case, we assumed PF0 interface had previously acted as tunnel endpoint and now we are configuring PF1 to act as tunnel endpoint.

***Note*:** Login as root user before proceeding below steps.

Step 1: Stop the incoming and outgoing traffic.

Step 2: Stop IPsec running on both servers using the below mentioned command.

```
ipsec stop
```

Step 3: Disable ipsec offload in PF interface using the below command.

```
echo 0 > /sys/class/net/<U25N_interface_1>/device/ipsec_enable
```

Eg: `echo 0 > /sys/class/net/U25eth0/device/ipsec_enable`

Step 4: Now enable the IPsec offload in the PF which needs to act as Tunnel Endpoint. 

```
echo 1 > /sys/class/net/<U25N_interface_2>/device/ipsec_enable
```

Eg: `echo 1 > /sys/class/net/U25eth1/device/ipsec_enable`

Step 5: Start the IPsec on both servers.

```
ipsec start
```


## 4.4 Stateless Firewall

This section is about stateless firewall prerequisites, installation steps, and supported rules.

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

***Note*:** Refer [Deployment Image Flashing](./ug1534-installation.html#deployment-image-flashing) for flashing images to check firewall functionality.

Step 1. The nftables only work in legacy mode. Ensure the SmartNIC is in legacy mode using the following command:

   ```
   devlink dev eswitch show pci/0000:<pci_id>
   ```

   The output of the above command should be:

   ```
   pci/0000:<pci_id>: mode legacy
   ```

  In case not in legacy mode, change it to legacy mode using the following command:

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
   ```

Step 2. Refer [Basic Requirements and Component Versions Supported](./ug1534-installation.html#basic-requirements-and-component-versions-supported) for the required OS/software version.

   The nftables version can be found using the command `nft -v`.

   nftables version ≥ v0.9.6

   ***Note*:** Tested version 0.9.6 and 0.9.8.

### 4.4.3 Classification Fields (Matches)

Maximum rules supported: 1K for each U25N PF interface.

```bash
Keys:

1. IPv4/IPv6 source address

2. IPv4/IPv6 destination address

3. Protocol (TCP/UDP)

4. Src_port

5. Dst_port

6. Chain

7. Interface Action

Actions

1. drop

2. accept
```
*Figure 14:* **Firewall**

![X25556-091321 this table might be messed up](./media/uhd1631577266564_LowRes.png)

#### Driver Installation

***Note*:** Log in as a root user before proceeding with the following
steps.

The prerequisites for the driver installation are as follows:

- modprobe mtd (first time only)

- modprobe mdio (first time only)

Step 1. If the debian package already exists, remove the existing package before installing the latest debian package:

   ```
   rmmod sfc
   dpkg -r sfc-dkms
   dpkg -i sfc-dkms_5.3.3.2000_all.deb
   modprobe sfc
   ```

   ***Note*:** Login as root user before proceeding with the following steps.

Step 2. Creating a table

   ```
   nft add table <family> <name>
   ```

   For example, `nft add table netdev filter`.

Step 3. Creating a chain:

   For example, `nft add chain netdev filter input1 { type filter hook ingress device ens7f1np1 priority 1; flags offload ;}`. Adding a chain without specifying the policy leads to the default policy Accept.

Step 4. Adding rules to the chain:

   ```
   nft add rule <family> <table name> <chain name> ip saddr <ip> drop
   ```

   For example, `nft add rule netdev filter input1 ip saddr 1.1.1.1 drop`.

Step 5. Commands for listing tables, chain, and rules:

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

Step 6. Commands for deleting tables, chains and rules:

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

## 4.5 Statistics

This section outlines the commands used by different modules to check the statistics and packet counters.

### OVS Commands

1. To print a brief overview of the database contents:

  ```
  ovs-vsctl show
  ```

2. To show the datapath flow entries:

  ```
  ovs-ofctl dump-flows <bridge_name>
  ```

3. To show the full OpenFlow flow table, including hidden flows, on the bridge:

  ```
  ovs-appctl dpctl/dump-flows type=offloaded
  ```

4. To show the OVS datapath flows:

  ```
  ovs-dpctl dump-flows
  ovs-appctl dpctl/dump-flows
  ```

5. To show which flows are offloaded or not:

  ```
  tc filter show dev <iface_name> ingress
  ```

### 4.5.2 MAE Rules

1. Use the following command to display the rules present in the match-action engine (MAE):

  ```
  cat /sys/kernel/debug/sfc/<if_iface>/mae_rules
  ```

  ***Note*:** Here `if_iface` should be the corresponding PF interface.

  For example, `cat /sys/kernel/debug/sfc/if_u25eth0/mae_rules`.

2. Use the following command to display the default rules present in the MAE:

  ```
  cat /sys/kernel/debug/sfc/<iface/mae_default_rules
  ```

### 4.5.3 IPsec Statistics

1. IPsec stats can be obtained by executing the following command:

- `sudo swanctl --stats`

2.  Use the `ip xfrm show` command to display IPsec offload security association.


## 4.6 Debug Commands

***Note*:** The output of the following commands should be saved for debug purposes.

1. lsmod - It displays which kernel modules are currently loaded. Whether the sfc driver is inserted or not can be verified by the below command. 

```
lsmod | grep sfc
```

```bash
Expected result:

```
sfc                   		704512  	0
sfc_driverlink         	16384  		1 	sfc
virtual_bus            	20480  		1 	sfc
mtd                    	65536  		14 	cmdlinepart,sfc
mdio                   	16384  		1 	sfc
```
```

2. To get kernel logs, enter the following command.

```
dmesg
```
	
This command will help to understand all the actions performed by the driver and if there are any crashes happening due to the driver.
 
```
lspci
```

To display information about all PCI buses and devices in the system. It will also show the network cards inserted in the system with details like driver in use, pci id etc.
For Example:

3. lspci | grep Solarflare - Lists the info regarding solarflare devices in the system
lspci -k | grep Solarflare - Lists the subsystem information also.  
lspci -vvv -s <BDF>

Expected result:
```bash

lspci | grep Solarflare:
3b:00.0 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (rev 01)

3b:00.1 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (rev 01)

lspci -k | grep Solarflare:

3b:00.0 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (rev 01)
Subsystem: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller

3b:00.1 Ethernet controller: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller (rev 01)
Subsystem: Solarflare Communications XtremeScale SFC9250 10/25/40/50/100G Ethernet Controller
```

4. Logs generated by U25N hardware are saved to a file.The logs are collected from the internal processing subsystem and exported to the host at frequent intervals. Currently these logs are populated when switchdev mode is enabled.

Path to read the logs in host : `/var/log/ps_dmesg.txt`

5. `sfreport` - A command line utility that generates a diagnostic log file providing diagnostic data about the server and Solarflare adapters. 
Please refer to SF-103837-CD Solarflare Server Adapter User Guide chapter 5.20 for more details.


6. `top` - This command can be used to show the linux processes or threads. It provides a real time view of the running system. It can be used to detect memory leaks. 
 The file in the linux path /proc/meminfo can also be used for detecting memory leaks. 

Watch out for the total memory already in use. Memory leak can be identified by running the command multiple times and checking whether the memory usage keeps on increasing. 


7. `ps` - This command displays relevant information about active processes.  
Example: ps -aux | grep sfc 

 
8. `ethtool` - This command can be used to understand the driver related information such as version, firmware info and the enabled features. 
ethtool -i <interface_name>  
Example Usage:	ethtool -i enp59s0f0np0

```bash
Expected result: 
driver: sfc
version: 5.3.3.2000.1
firmware-version: 7.8.7.1005 rx0 tx0
expansion-rom-version: 
bus-info: 0000:3b:00.0
supports-statistics: yes
supports-test: yes
supports-eeprom-access: no
supports-register-dump: yes
supports-priv-flags: yes
```

To get the information of the state of protocol offload and other features
ethtool -k <interface_name>
Example Usage:	ethtool -k enp59s0f1np1

```bash
Expected result: 
Features for enp59s0f1np1:
rx-checksumming: on
tx-checksumming: on
	tx-checksum-ipv4: on
	tx-checksum-ip-generic: off [fixed]
	tx-checksum-ipv6: on
	tx-checksum-fcoe-crc: off [fixed]
	tx-checksum-sctp: off [fixed]
scatter-gather: on
	tx-scatter-gather: on
	tx-scatter-gather-fraglist: off [fixed]
tcp-segmentation-offload: on
	tx-tcp-segmentation: on
	tx-tcp-ecn-segmentation: on
	tx-tcp-mangleid-segmentation: off
	tx-tcp6-segmentation: on
generic-segmentation-offload: on
generic-receive-offload: on
large-receive-offload: off
—-------------------------------------and so on.
```

To change the offload parameters and other features of the network device
sudo ethtool -K <interface_name> <feature> <on/off>

To get information about NIC Statistics
sudo ethtool -S <interface_name>
Example usage: sudo ethtool -S enp59s0f1np1

```bash
Expected result:
NIC statistics:
     rx_noskb_drops: 0
     rx_nodesc_trunc: 0
     port_tx_bytes: 59052
     port_tx_packets: 353
     port_tx_pause: 0
     port_tx_control: 0
     port_tx_unicast: 0
     port_tx_multicast: 273
     port_tx_broadcast: 80
     port_tx_lt64: 0
     port_tx_64: 0
     port_tx_65_to_127: 229
     port_tx_128_to_255: 44
     port_tx_256_to_511: 80
     port_tx_512_to_1023: 0
     port_tx_1024_to_15xx: 0
     port_tx_15xx_to_jumbo: 0
     port_rx_bytes: 0
     port_rx_good_bytes: 0
     port_rx_bad_bytes: 0
     port_rx_packets: 0
—-------------------------------------- and so on.
```

NOTE: ethtool functionalities for sfc driver can also be realised using the sfctool utility also.
Eg:   sudo sfctool -S <interface_name>
Example Usage: sudo sfctool -S enp59s0f1np1


9. u25n_update Application: u25n_update utility can be used to read the U25N shell version.
Eg:

```bash
./utils/u25n_update get-version <PF0_interface> 
```

10.MCDI Logging - Mcdi request and response data will be visible in dmesg if we activate mcdi logs. To activate the logs in dmesg:  
echo 1 >> /sys/class/net/<interface_name>/device/mcdi_logging 


11. OVS log levels - It can be turned on/off using the ovs-appctl commands.
 The ovs-vswitchd accepts the option --log-file[=file] to enable logging to a specific file. The file argument is actually optional, so if it is specified, it is used as the exact name for the log file. The default is used if the file is not specified. Usually the default is /usr/local/var/log/openvswitch/ovs-vswitchd.log 
Setting OVS Log levels:
```bash
ovs-appctl vlog/set ANY:ANY:dbg
ovs-appctl vlog/set poll_loop:ANY:OFF
ovs-appctl vlog/set netlink_socket:ANY:OFF
```

12. Mae Rules - To see the offloaded rules available in the MAE, check the below file:

```bash
cat /sys/kernel/debug/sfc/if_<interface_name>/mae_rules
```
To check the default rules in MAE, check the below file:
```bash
cat /sys/kernel/debug/sfc/if_<interface_name>/mae_default_rules
```

13. iperf3 - Iperf is a tool for network performance measurement and tuning. It is a cross-platform tool that can produce standardized performance measurements for any network. It has client and server functionality, and can create data streams to measure the throughput between the interfaces. After setting proper ip addresses: 
Server: iperf3 -s <options>
Client : iperf -c <ip_addr_interface> <options>


14. tcpdump -  Tcpdump is a packet sniffing and packet analyzing tool meant for System Administrators to troubleshoot connectivity issues in Linux. For example it can capture the packets coming to the network interface using the below command. 

```bash 
tcpdump -i <interface_name>
```
