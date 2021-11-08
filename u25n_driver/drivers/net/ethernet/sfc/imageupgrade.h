/****************************************************************************
 * Image upgrade IOCTLS for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef IMAGEUPGRADE_H
#define IMAGEUPGRADE_H
/*BUFFER SIZE*/
#define SIZE (992)

enum {
        OPERATION_ERASE = 0,
        OPERATION_WRITE,
        OPERATION_VERIFY
};

struct file_details {
	unsigned int status;
	size_t fsize;
};

struct imgdata {
	unsigned int status;
	size_t size;
	char buffer[];
}__attribute__((packed));

struct fini_details {
	unsigned int status;
	unsigned long crc;
};

struct flash_upgrade_status {
        unsigned int overall_status;
	unsigned int curr_state;
        unsigned int operation;
        unsigned int bytes_performed;
};

union info
{
        struct file_details init_details;
        struct imgdata image;
        struct fini_details fini_details;
        unsigned int flash_index;
	struct flash_upgrade_status flash_status;
	char version[5];
        char fw_version[32];
	struct {
		unsigned int status;
		unsigned int bit_version;
		unsigned int app_list;
		unsigned int time_stamp;
	};
};

struct command_format
{
        uint8_t subcommand;
        union info cmd_info;
};

#endif
