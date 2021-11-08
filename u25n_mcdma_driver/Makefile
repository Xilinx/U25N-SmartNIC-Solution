obj-m := xilinx.o

#export CONFIG_XILINX_EMACLITE := y
#export CONFIG_XILINX_AXI_EMAC := y
#export CONFIG_AXIENET_HAS_MCDMA := y

ccflags-y := -DCONFIG_XILINX_AXI_EMAC -DCONFIG_AXIENET_HAS_MCDMA

xilinx-objs := xilinx_axienet_main.o xilinx_axienet_mdio.o xilinx_axienet_dma.o  \
                xilinx_axienet_mcdma.o xilinx_axienet_mcdi.o xilinx_axienet_nl.o 

SRC := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers

