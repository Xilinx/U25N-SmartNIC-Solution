# U25N Installation

## Basic Requirements and Component Versions Supported

- OS requirement: Ubuntu 18.04 or 20.04.

  ***Note*:** Xilinx recommends using the LTS version of Ubuntu.

- Kernel requirements:

  - OVS Functionality ≥ 4.15.

  - Stateless Firewall Functionality ≥ 5.5.

  ***Note*:** Xilinx recommends the default kernel 5.8 that comes with Ubuntu 20.04.02 LTS to support all features of the U25N hardware.

- PCIe® Gen3 x16 slot.

- Requires passive airflow. More details can be found in *Alveo U25N SmartNIC Data Sheet* (DS1005).

- U25N driver version: 5.3.3.1003 (minimum).

  ***Note*:** To install the latest U25N driver, refer to [U25N Driver](linkbrokeninpdf).

- X2 firmware version: v7.8.17.1004 (minimum).

  ***Note*:** To install the latest firmware, refer to the Updating U25N Firmware section.

- OVS version: 2.12 and above. For more information about OVS and its installation, refer to [https://docs.openvswitch.org/en/latest/intro/install/general](https://docs.openvswitch.org/en/latest/intro/install/general).

## U25N Driver

***Note*:** Log in as root user before proceeding with the following steps.

After the server is booted with U25N hardware, check whether the U25N SmartNIC is detected using the lspci command:

```
lspci | grep Solarflare
```

Two PCI IDs corresponding to the U25N are displayed:

```
af:00.0 Ethernet controller: Solarflare Communications XtremeScale SFC9250
10/25/40/50/100G Ethernet Controller (rev 01)
af:00.1 Ethernet controller: Solarflare Communications XtremeScale SFC9250
10/25/40/50/100G Ethernet Controller (rev 01)
```

### Utility

The Solarflare Linux Utilities package (SF-107601-LS) is available from [https://support-nic.xilinx.com/wp/drivers?sd=SF-107601-LS-71&pe=1945](https://support-nic.xilinx.com/wp/drivers?sd=SF-107601-LS-71&pe=1945).

1. Download and unzip the package on the target server.

   ***Note*:** Alien package must be downloaded using the command `sudo apt install alien`.

2. Create the `.deb` file:

   ```
   sudo alien sfutils‐<version>.x86_64.rpm
   ```

   [//]: # (^is that file extension supposed to be .rpm?)

   This command generates the `sfutils_<version>_amd64.deb` file.

3. Install the `.deb` file:

   ```
   sudo dpkg -i sfutils_<version>_amd64.deb
   ```

The sfupdate, sfkey, sfctool, and sfboot utilities are available on the server.

### U25N Driver Installation

***Note*:** Install the dkms package with the following command if not available:

```
sudo apt-get install dkms
```

1. Check the driver version of the U25N interface using the following command:

   ```
   ethtool -i U25_eth0 | grep version
   ```

   This should give the output as version 5.3.3.1003.

   ***Note*:** If the driver version is at least 5.3.3.1003, ignore the following step.

2. If the Debian package already exists, remove the already existing one before installing the latest Debian package.

   ```
   modprobe mtd [for first time alone]
   modprobe mdio [for first time alone]
   rmmod sfc
   dpkg -r sfc-dkms
   dpkg -i sfc-dkms_x.x.x.x_all.deb
   modprobe sfc
   ```

   For example:

   ```
   dpkg -i sfc-dkms_5.3.3.1003_all.deb
   ```

### Updating U25N Firmware

1. Make sure the U25N driver is loaded using the `lsmod` command:

   ```
   lsmod | grep sfc
   ```

   For example:

   ```
   lsmod | grep sfc
   sfc 626688 0
   ```

2. Execute the following step to update the firmware:

   ```
   sfupdate -i <X2_interface> --image <firmware image> --write --force --yes
   ```

   ***Note*:** Use the PF0 interface. No reboot is required.

3. Confirm the firmware version using the sfupdate command. The version should be 1004.

   ```
   sfupdate | grep Bundle
   ```

   Bundle type: U25 (bundle type for production SmartNICs is U25N) Bundle
   version: v7.8.17.1004 (minimum)

### sfboot Configuration

***Note*:** Ignore this section if the U25N mode is SR-IOV and the required VF counts are already assigned. This can be verified by running the sfboot command as root.

Refer to the Solarflare server adapter user guide at [https://support-nic.xilinx.com/wp/drivers](https://support-nic.xilinx.com/wp/drivers). X2 switch mode should be in SR-IOV. Based on the number of VF required on each U25N PF interface, execute the following command. The maximum vf-count could be 120:

```
sudo sfboot switch-mode=sriov vf-count=<No. of VF required>
```

For example, here 120 VFs have been assigned for each U25N PF interface:

```
sudo sfboot switch-mode=sriov vf-count=120
```

***Note*:** Powercycle is required to update the configuration. The configuration is retained even after cold or warm reboot.

## U25N Shell

By default, the U25N shell is programmed with the golden image. Follow the steps in the following sections to verify the shell. If this fails, refer to [U25N Shell Programming](./ug1534-shellprogramming.md).

### Shell Version Check

***Note*:** If the U25N driver version is 5.3.3.1003, ignore step 1.

1. Check the driver version of the U25N interface using the following command:

   ```
   ethtool -i U25_eth0 | grep version
   ```

   This should give an output of version of 5.3.3.1003.

   ***Note*:** To install the latest sfc driver, refer to the [U25N Driver](link?) section.

2. The u25n_update utility can be used to read the U25N shell version. Version reading is done at the U25N PF0 network interface using the u25n_update utility.

   1. Reading of version is performed using a CLI command:

      ```
      ./u25n_update get-version <PF0_interface>
      ```

      For example:

      ```
      ./u25n_update get-version u25eth0
      ```

      Golden shell version: 0x21081600.

   2. After the versions are read successfully, the log would be:

      ```
      STATUS: SUCCESSFUL
      ```

***Note*:** If u25n_update status shows failed, the shell is not programmed with the golden image. Refer to [U25N Shell Programming](./ug1534-shellprogramming.md) to flash the golden image to the U25N shell.

Follow the sections below if needed to check the golden image functionality. For flashing deployment image to U25N shell, refer to the [Deployment Image Flashing](link) section.

### Golden Image Functionality

- U25N shell with Golden image includes Image Upgrade and Basic NIC functions. Offloaded features are not supported.

- U25N hardware in legacy mode [MAC0 TO MAC2 AND MAC1 TO MAC3].

#### Checking Basic NIC Functionality

1. Run DPDK testpmd at the X2 PF interface.

2. The packet sending to an external MAC is received at the testpmd application corresponding to PF bound.

3. Connect the external MAC to the traffic generator or any peer NIC device.

***Note*:** Refer to [DPDK on U25N](link) to run dpdk-testpmd.

### Deployment Image Flashing

The U25N SmartNICs are shipped with the golden image. Refer to [Shell Version Check](link) to check the shell version. If the shell has a golden image, continue with the following steps.

***Note*:** Before flashing the image, make sure the SmartNIC is in legacy mode, no OVS software application is running, and OVS databases are removed.

1. If the U25N driver version is 5.3.3.1003, ignore this step. Check the driver version of the U25N interface using the following command:

   ```
   ethtool -i U25_eth0 | grep version
   ```

   This should give an output of version 5.3.3.1003.

   ***Note*:** To install the latest sfc driver, refer to U25N Driver.

2. The U25N SmartNIC should be in legacy mode. Use the following
    command to verify this:

   ```
   devlink dev eswitch show pci/0000:<pci_id>
   ```

   The output of the above command is:

   ```
   pci/0000:<pci_id>: mode legacy
   ```

   If the U25N hardware is not in legacy mode, then change to legacy mode using the following command:

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
   ```

3. Image flashing is done via the U25N PF0 network interface using the u25n_update utility.

   1. The image is flashed using a CLI command.

      ***Note*:** The interface should be the PF0 interface of the target U25N SmartNIC. The file must be a .bin file. Make sure the path to the image to be flashed is given correctly, as follows:

      ```
      ./u25n_update upgrade <path_to_bin_or_ub_file_with_extension>
      <PF0_interface>
      ```

      For example:

      ```
      ./u25n_update upgrade BOOT.bin u25eth0
      ```

   2. Flash operations like ERASE, WRITE, and VERIFY are displayed in the CLI based on the procedures done by the u25n_update utility.

   3. After the image is flashed successfully, the following log is shown:

      ```
      STATUS: SUCCESSFUL
      ```

4. Reset the U25N hardware to boot from the updated deployment image:

   For example:

   After the reset is done successfully, the following log is shown:

5. The u25n_update utility can be used to get the U25N deployment image version:

   ```
   ./u25n_update get-version <PF0_interface>
   ```

   For example:

   ```
   ./u25n_update get-version u25eth0
   ```

   Deployment shell version: 0x21011800.

   After the versions are read successfully, the following log is shown:

   ```
   STATUS: SUCCESSFUL
   ```

### Loading Partial Bitstream into Deployment Shell

***Note*:** The deployment image must be flashed to the U25N shell before following the steps below. Refer to [Deployment Image Flashing](link) to flash the deployment image to the U25N shell.

1. If the U25N driver version is 5.3.3.1003, ignore this step. Check the driver version of the U25N interface using the following command:

   ```
   ethtool -i U25_eth0 | grep version
   ```

   This should give an output of version 5.3.3.1003.

   ***Note*:** To install the latest sfc driver, refer to [U25N Driver](link).

   ***Note*:** Before flashing the image, make sure the SmartNIC is in legacy mode, no OVS software application is running, and OVS databases are removed.

2. The U25N SmartNIC should be in legacy mode. Use the following command to verify this:

   ```
   devlink dev eswitch show pci/0000:<pci_id>
   ```

   The output of the above command is:

   ```
   pci/0000:<pci_id>: mode legacy
   ```

   If the U25N hardware is not in legacy mode, then change to legacy mode using the following command:

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
   ```

   ***Note*:** The interface should be the PF0 interface of the target U25N SmartNIC. The file must be a `.bit` file. Make sure the path to the image to be flashed is given correctly.

3. Image flashing is done via the U25N PF0 network interface using the u25n_update utility.

   1. The image is flashed using a CLI command:

      ```
      ./u25n_update upgrade <path_to_bit_file_with_extension>
      <PF0_interface>
      ```

      For example:

      ```
      ./u25n_update upgrade fpga.bit u25eth0
      ```

   2. After the image is flashed successfully, the following log is shown:

      ```
      STATUS: Successful
      ```

4. The u25n_update utility can be used to get the bitstream version. Version reading is done at the U25N PF0 network interface using the u25n_update utility:

   ```
   ./u25n_update get-version <PF0_interface>
   ```

   For example:

   ```
   ./u25n_update get-version u25eth0
   ```

   Bitstream version: 0xA00D10D1

   After the versions are read successfully, the following log is shown:

   ```
   STATUS: SUCCESSFUL
   ```

## Updating XCU25 Linux Kernel Image

This section is required when the file system needs to be updated for the existing deployment image. The deployment image must be flashed to the U25N shell before following the steps below. Refer to [Deployment Image Flashing](link) to flash the deployment image.

***Note*:** Before flashing the image, make sure the SmartNIC is in legacy mode, no OVS software application is running, and OVS databases are removed.

1. If the U25N driver version is 5.3.3.1003, ignore this step. Check the driver version of the U25N interface using the following command:

   ```
   ethtool -i U25_eth0 | grep version
   ```

   This should give an output of version 5.3.3.1003.

   ***Note*:** To install the latest sfc driver, refer to U25N Driver.

2. The U25N SmartNIC should be in legacy mode. Use the following command to verify this:

   ```
   devlink dev eswitch show pci/0000:<pci_id>
   ```

   The output of the above command is:

   ```
   pci/0000:<pci_id>: mode legacy
   ```

   If the U25N hardware is not in legacy mode, then change to legacy mode using the following command:

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
   ```

3. Image flashing is done via the U25N PF0 network interface using the u25n_update utility.

   ***Note*:** The interface should be the PF0 interface of the target U25N SmartNIC. The file must be a `.ub` file. Make sure the path to the image to be flashed is given correctly.

   1. The image is flashed using a CLI command:

      ```
      ./u25n_update upgrade    <path_to_ub_file_with_extension><PF0_interface>
      ```

      For example:

      ```
      ./u25n_update upgrade image.ub <u25eth0>
      ```

   2. Flash operations like ERASE, WRITE, and VERIFY are displayed in the CLI based on the procedures done by the u25n_update utility.

   3. After the image is flashed successfully, the following log is shown:

      ```
      STATUS: SUCCESSFUL
      ```

4. Reset the U25N hardware to boot with the new kernel image:

   ```
   ./u25n_update reset <PF0_interface>
   ```

   For example:

   ```
   ./u25n_update reset <u25eth0>
   ```

   After the image is flashed successfully, the following log is shown:

   ```
   STATUS: SUCCESSFUL
   ```

## Reverting U25N SmartNIC to Golden Image

Reverting to factory (or golden) image is recommended in the following cases:

- Preparing to flash a different shell onto the SmartNIC.

- The SmartNIC no longer appears on lspci after programming a custom image onto the SmartNIC.

- To recover from the state where the deployment image is unresponsive even after performing multiple resets using the u25n_update utility.

***Note*:** After the factory reset is performed, the U25N loaded is the golden image. It has the essential provision to upgrade the deployment image but no provision to upgrade bitstream.

1. If the U25N driver version is 5.3.3.1003, ignore this step. Check the driver version of the U25N interface using the following command:

   ```
   ethtool -i U25_eth0 | grep version
   ```

   This should give an output of version 5.3.3.1003.

   ***Note*:** To install the latest sfc driver, refer to U25N Driver.

   ***Note*:** Before flashing the image, make sure the SmartNIC is in legacy mode, no OVS software application is running, and OVS databases are removed.

2. The U25N SmartNIC should be in legacy mode. Use the following command to verify this:

   ```
   devlink dev eswitch show pci/0000:<pci_id>
   ```

   The output of the above command is:

   ```
   pci/0000:<pci_id>: mode legacy
   ```

   If the U25N hardware is not in legacy mode, then change to legacy mode
   using the following command:

   ```
   devlink dev eswitch set pci/0000:<PCIe device bus id> mode legacy
   ```

3. Factory reset is done via the U25N PF0 network interface.

   1. Factory reset is performed using a CLI command:

   ```
   ./u25n_update factory-reset <PF0_interface>
   ```

   For example:

   ```
   ./u25n_update factory-reset <u25eth0>
   ```

   2. After the factory reset is done successfully, the following log is shown:

   ```
   STATUS: SUCCESSFUL
   ```
