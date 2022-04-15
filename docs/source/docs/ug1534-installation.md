# 3. U25N Installation

## 3.1 Basic Requirements and Component Versions Supported

- OS requirement: Ubuntu 18.04 or 20.04.

  ***Note*:** We recommend using the LTS version of Ubuntu.

- Kernel requirements:

  - OVS Functionality ≥ 4.15.

  - Stateless Firewall Functionality ≥ 5.5.

  ***Note*:** We recommends the default kernel 5.8 that comes with Ubuntu 20.04.02 LTS to support all features of the U25N hardware.

- PCIe® Gen3 x16 slot.

- Requires passive airflow. More details can be found in *Alveo U25N SmartNIC Data Sheet* (DS1005).

- U25N driver version: 5.3.3.1008.3 (minimum).

- X2 firmware version: v7.8.17.1011 (minimum).

  ***Note*:** To install the latest firmware, refer to the Updating U25N Firmware section.

- OVS version: 2.12 and above. For more information about OVS and its installation, refer to [https://docs.openvswitch.org/en/latest/intro/install/general](https://docs.openvswitch.org/en/latest/intro/install/general).

## 3.2 U25N Driver

***Note*:** Root user privileges are required to execute following steps and commands.

Once the server is successfully booted with U25N Smart NIC, validate that the U25N Smart NIC is detected using the lspci command:

```text
lspci | grep Solarflare
```

It will list two PCIe id’s corresponding to U25N, similar to the following sample output:

```bash  
af:00.0 Ethernet controller: Solarflare Communications XtremeScale SFC9250
10/25/40/50/100G Ethernet Controller (rev 01)
af:00.1 Ethernet controller: Solarflare Communications XtremeScale SFC9250
10/25/40/50/100G Ethernet Controller (rev 01)
```

### Solarflare Linux Utilities Installation

The Solarflare Linux Utilities package can be found from the U25N software package downloaded from the lounge.

1. Download and unzip the package on the target server.

   ***Note*:** Alien package must be downloaded using the command `sudo apt install alien`.

2. Create the `.deb` file:

   ```bash
   sudo alien sfutils‐<version>.x86_64.rpm
   ```

   This command generates the `sfutils_<version>_amd64.deb` file.

3. Install the `.deb` file:

   ```bash
   sudo dpkg -i sfutils_<version>_amd64.deb
   ```

The server should have sfupdate, sfkey, sfctool and sfboot utilities now.

### 3.2.1 U25N Driver Installation

***Note*:** Install the dkms package with the following command if not available:

```bash
sudo apt-get install dkms
```

***Note*:** If the driver version is 5.3.3.1008.3, please ignore Step 1.
Check the driver version of U25N interface using the following command:

```bash
ethtool -i U25N_eth0 | grep version #U25N_eth0 is the first port of U25N.
```

Step 1 : If the debian package already exists, before installing the latest debian package, remove the already existing package.

```bash
modprobe mtd  [for first time alone]

modprobe mdio  [for first time alone]

rmmod sfc

dpkg -r sfc-dkms

dpkg -i sfc-dkms_x.x.x.x_all.deb

modprobe sfc
```

For Eg:

```bash
dpkg -i sfc-dkms_5.3.3.1008.3_all.deb
```

### 3.2.2 Updating U25N Firmware

Step 1: Make sure U25N driver is loaded using `lsmod` command

```bash
lsmod | grep sfc
```

For Eg :

```bash
lsmod | grep sfc
sfc                   626688  0
```

Step 2: Execute the following step to update firmware

```bash
sfboot --list # This should output U25N interfaces
sfupdate #This should work or do the below step
sfupdate -i <U25N_eth0> --write --force --yes # U25N_eth0 is the PF0 interface of U25N
```

Note: Please use the PF0 interface. No reboot is required.

Step 3: Confirm the firmware version using the command `sfupdate`. Version should be 1011.

Eg:

```bash
sfupdate | grep Bundle
```

Bundle type:        U25N
Bundle version:     v7.8.17.1011

### 3.2.3 sfboot Configuration

***Note*:** Ignore the following steps if the U25N is operating in SR-IOV mode and the required VF are already assigned. Available VFs and SR-IOV mode can be verified by running the sfboot command as a root user.

For more information on enabling VFs and SR-IOV, refer to the [solarflare user guide](https://support-nic.xilinx.com/wp/drivers)

X2 switch mode must be configured in SR-IOV mode for enabling VFs. Each U25N PF can have up to 120 VFs. The following command demonstrates how to switch to the SR-IOV mode and enable VFs.

```bash
sudo sfboot switch-mode=sriov vf-count=<No. of VF required>
```

***Note*:**  Perform a power cycle to update the configuration. Updated configuration will be retained post cold boot.

## 3.3 U25N Shell

The U25N shell is programmed on the U25N with a known good image which is called "golden image" out of the factory. The Following sections demonstrate how to verify the shell. If the validation fails, refer to [U25N Shell Programming](./ug1534-shellprogramming.md).

### 3.3.1 Shell Version Check

Step 1 : Check the driver version of U25N interface using the following command:

***Note*:** Ignore Step 1, if the U25N driver version is 5.3.3.1008.3.

```bash
ethtool -i U25N_eth0 | grep version
```

***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.html#u25n-driver).

Step 2 : Use the u25n_update utility (found inside RELEASE_DIR/utils directory) to find out the U25N shell version. Execute the following command to read the U25N shell version.

```bash
./u25n_update get-version <U25N_eth0>
```

Eg:

```bash
./u25n_update get-version u25eth0

Sample output:
[u25n_update] - Image Upgrade/Erase Utility V3.1

Reading version from the hardware

X2 Firmware Version : 7.8.17.1011
PS Firmware Version : 5.00
Deployment shell Version : 0x0812210D # Timestamp of Shell
Bitstream Version : 0xA00E60D3 # Timestamp of offload 
Features supported:  MAE IPSEC # Represent NIC by default
Timestamp : 08-12-2021 16:30:45 


STATUS: SUCCESSFUL

```

You should get following prompt post successful U25N version read.

```bash
STATUS: SUCCESSFUL
```

***Note*:** If the u25n_update command returns status as failed, this implies that the shell is not programmed with the Golden image. Please contact support. Refer to [U25N Shell Programming](./ug1534-shellprogramming.md) to flash the Golden image to the U25N shell.

The following section demonstrates how to validate the Golden Image Functionality. For flashing deployment image to the U25N shell refer to [Deployment Image Flashing](./ug1534-installation.html#deployment-image-flashing.md).

***Note*:** If the Golden image version is same as the Deployment image version, flashing is not required.

### 3.3.2 Testing Golden Image Functionality

1. The U25N shell with Golden image includes Image Upgrade and Basic NIC functions.

2. U25N hardware in legacy mode where the acceleration logic is simple passthrough from ethernet ports to host driver.

#### 3.3.2.1 Checking Basic NIC Functionality

Connect the U25N physical port to a traffic generator or any peer NIC device.

```text
Run ping after assigning IP addresses on source and remote ports.
Run iperf server and client on any ports to verify traffic.
Run DPDK testpmd at the X2 PF interface. Packets sent to the physical port will be received at the testpmd application corresponding to the bound PF.
```

***Note*:** Refer to [DPDK on U25N](./ug1534-supportedservices.html#dpdk-on-u25n) to run dpdk-testpmd.

### 3.3.3 Deployment Image Flashing

***Note*:** U25N must have the Golden Image before performing any of the following steps. Refer to [Shell Version Check](./ug1534-installation.html#shell-version-check) to check the Shell version. Ignore the flashing if Golden image version is same as deployment version in the release.

***Note*:** Before flashing the image ensure the U25N card is in legacy mode with no OVS software application running. Also, remove all associated OVS databases( if any)

```bash
To check the mode of operation:(Legacy/Switchdev modes)
devlink dev eswitch show pci/0000:<pci_id> #pci_id is the BDF of U25N Device
```

Step 1 : In case U25N driver version is 5.3.3.1008.3, this step can be ignored.  The following command can be used to check the driver version:

```bash
ethtool -i U25N_eth0 | grep version
```

***Note*:** To install the latest sfc driver, please refer to [U25N Driver](./ug1534-installation.html#u25n-driver).

Step 2 : Ensure that the U25N card is in legacy mode. Below is the command to validate:

```bash
devlink dev eswitch show pci/0000:<pci_id>
```

The command mentioned above should return the following sample output:

```bash
pci/0000:<pci_id>: mode legacy
```

Use the following command to switch mode in case the card is not already in the legacy mode:

```bash
devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
```

Step 3 : The u25n_update utility uses U25N PF0 network interface to flash the image. Execute the following command to commence image flash.
***Note*:** The interface must be the PF0 interface of the target U25N card. The file must be a `.bin` file. Ensure the image path to be flashed is correct.

```bash
./u25n_update upgrade <path_to_bin_or_ub_file_with_extension> <PF0_interface> # The bin file should be in the shell directory of the release.
```

Eg:

```bash
#From Release_dir/utils:
./u25n_update upgrade ../shell/BOOT.bin u25eth0 
#The name of the bin file may change to represent month and day like U25N_Shell_Dec13.bin
```

Post successful image flash you will the following prompt:

```bash
STATUS: SUCCESSFUL
```

Step 4 : Reset the U25N hardware to boot from the updated deployment image.

```bash
./u25n_update reset <U25N_eth0>
```

Eg:

```bash
./u25n_update reset u25eth0
```

Post a successful reset the following prompt will be shown:

```bash
STATUS: SUCCESSFUL # The above command takes a few seconds to complete to ensure proper reset sequence is validated. 
```

Step 5 : To retrieve U25N Deployment image version details, use the following commands:

```bash
./u25n_update get-version <U25N_eth0>
```

Eg:

```bash
./u25n_update get-version u25eth0
#Sample Output:
[u25n_update] - Image Upgrade/Erase Utility V3.1

Reading version from the hardware

X2 Firmware Version : 7.8.17.1011
PS Firmware Version : 5.00
Deployment shell Version : 0x0812210D
Bitstream Version : 0xA00E60D3
Features supported:  No Features supported
Timestamp : 08-12-2021 16:30:45


STATUS: SUCCESSFUL

```

The following prompt will be shown, post a successful retrieval

```text
STATUS: SUCCESSFUL
```

### 3.3.3.1 Loading Partial Bitstream into Deployment Shell

***Note*:** The U25N must be flashed with the Deployment image before running the following commands. Refer to the [[Deployment Image Flashing](ug1534-installation.html#deployment-image-flashing)](./ug1534-installation.html#deployment-image-flashing) to flash the deployment image to the U25N shell.

***Note*:** In case partial Bitstream already exists in the U25N card and/or if a warm reboot has already been done, ensure a fpga reset is performed using u25n_update application before following the steps mentioned below. Use the following command to reset the FPGA.

```bash
./u25n_update reset <U25N_eth0>
```

Eg:

```bash
./u25n_update reset u25eth0
```

Use the PF0 interface of U25N card to initiate the FPGA reset.

Step 1. In case U25N driver version is 5.3.3.1008, this step can be ignored. Driver version of the U25N can be validated using the following command:

   ```bash
   ethtool -i U25_eth0 | grep version
   ```

***Note*:** To install the latest sfc driver, refer to [U25N Driver](./ug1534-installation.html#basic-requirements-and-component-versions-supported).

***Note*:** Before flashing the image, make sure the U25N Smart NIC is in legacy mode, with  OVS software application running, and all OVS databases are removed.

Step 2. The U25N Smart NIC should be in legacy mode. Use the following command to verify this:

   ```bash
   devlink dev eswitch show pci/0000:<pci_id>
   ```

   Sample output of the above command:

   ```bash
   pci/0000:<pci_id>: mode legacy
   ```

   If the U25N hardware is not in legacy mode, change it to legacy mode using the following command:

   ```bash
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
   ```

   ***Note*:** The interface used in the above command should be the PF0 interface of the target U25N SmartNIC. The file must be a `.bit` file. Ensure the path to the image to be flashed is correct.

Step 3. Image flashing happens through the U25N PF0 network interface using the u25n_update utility.

   1. The image is flashed using the following CLI command:
  
***Note*:** The interface used in the above command should be the PF0 interface of the target U25N SmartNIC. The file must be a `.bit` file. Ensure the path to the image to be flashed is correct.

```bash
./u25n_update upgrade <path_to_bit_file_with_extension> <PF0_interface>
# the path to bit files is Release_dir/bits 
```

 E.g:

```bash
./u25n_update upgrade ../bits/ovs_*.bit u25eth0
```

 2. After the image is flashed successfully, the following message is displayed:

      ```bash
      STATUS: Successful
      ```

 3. The u25n_update utility can be used to retrieve the bitstream version. Version retrieval happens at the U25N PF0 network interface using the u25n_update utility. Use the following command to retrieve the image version:

```bash
./u25n_update get-version <U25N_eth0>
```

E.g:

```bash
./u25n_update get-version u25eth0
```

Bitstream version: 0xA00D10D1

Post a successful image retrieval, the following message is displayed:

```bash
STATUS: SUCCESSFUL
```

### 3.4 Steps to change MPSoC Linux kernel image (For advanced users)

Follow this Section only when the file system needs to be updated for the existing Deployment image.

Deployment image must be flashed to the U25N shell before following the below mentioned steps. Refer to [[Deployment Image Flashing](ug1534-installation.html#deployment-image-flashing)](./ug1534-installation.html#deployment-image-flashing) to flash the Deployment image.

***Note*:** Before flashing the image to the shell ensure the card is in legacy mode with no OVS software application  running and all OVS databases have been removed.

Step 1 : If the U25N driver version is 5.3.3.1008.3, this step can be ignored. Driver version of the U25N can be validated by using the following command:

```bash
ethtool -i U25_eth0 | grep version
```

***Note*:** To install the latest sfc driver, refer  Section 3.2.

Step 2 : U25N Cards should be in legacy mode. Use the following command to validate:

```bash
devlink dev eswitch show pci/0000:<pci_id>
```

Sample output of above command:

```bash
pci/0000:<pci_id>: mode legacy
```

In case U25N hardware is not in legacy mode, change it to legacy mode using the following command:

```bash
devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
```

Step 3 : Image flashing is performed through the U25N PF0 network interface using the u25n_update utility.

Image is flashed using a CLI command

***Note*:** The interface should be the PF0 interface of the target U25N card. The file must be a `.ub` file. Make sure the path to the image to be flashed is given correctly.

```bash
./u25n_update upgrade <path_to_bin_or_ub_file_with_extension> <PF0_interface>
```

Eg:

```bash
./u25n_update upgrade image.ub <u25eth0>
```

Post successful image flash, the following message will be displayed:

```bash
STATUS: SUCCESSFUL
```

Step 4 : Reset the U25N hardware to boot with the new kernel image using the following command:

```bash
./u25n_update reset <PF0_interface>
```

Eg:

```bash
./u25n_update reset <u25eth0>
```

After successful image flash, the following message will be displayed:

```bash
STATUS: SUCCESSFUL
```

## 3.5 Reverting the U25N SmartNIC to Golden Image

Reverting to the factory (or golden) image is recommended in the following cases:

- Preparing to flash a different shell onto the SmartNIC.

- The SmartNIC no longer appears on lspci after programming a custom image onto the SmartNIC.

- To recover from the state when the deployment image has become unresponsive even after performing multiple resets using the u25n_update utility.

***Note*:** After the factory reset is performed, the U25N loaded with the golden image (A known good image). It has the essential provision to upgrade the deployment image.

Step 1. In case the U25N driver version is 5.3.3.1008, this step can be ignored. Driver version of the U25N interface can be validated using the following command:

   ```bash
   ethtool -i U25_eth0 | grep version
   ```

***Note*:** To install the latest sfc driver, refer to the [U25N Driver](./ug1534-installation.html#u25n-driver).

***Note*:** Before flashing the image, make sure the SmartNIC is in legacy mode, no OVS software application is running, and OVS databases are removed.

Step 2. The U25N SmartNIC should be in legacy mode. Use the following command to verify:

   ```bash
   devlink dev eswitch show pci/0000:<pci_id>
   ```

Sample output of the above command:

   ```bash
   pci/0000:<pci_id>: mode legacy
   ```

If the U25N hardware is not in the legacy mode, change it to legacy mode using the following command:

   ```bash
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
   ```

Step 3. Perform a factory reset via PF0 network interface of U25N

   1. Factory reset is performed using the following CLI command:

   ```bash
   ./u25n_update factory-reset <PF0_interface>
   ```

  E.g: `./u25n_update factory-reset <u25eth0>`

   2. After a successful factory reset, the following message is displayed:

   ```bash
   STATUS: SUCCESSFUL
   ```
