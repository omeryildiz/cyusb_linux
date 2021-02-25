/*
 * Filename             : usbmethods.h
 * Description          : Declarations of USB utility functions used by the cyusb_linux application.
 */

#ifndef INCLUDED_USBMETHODS_H
#define INCLUDED_USBMETHODS_H

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

extern int
fx2_ram_download (
		const char *filename,
	       	int extension);

extern int
fx2_eeprom_download (
		const char *filename,
	       	int large);

extern int
fx3_usbboot_download (
		const char *filename);

extern int
fx3_i2cboot_download (
		const char *filename);

extern int
fx3_spiboot_download (
		const char *filename);

extern void
streamer_set_params (
		unsigned int ep,
		unsigned int type,
		unsigned int pktsize,
		unsigned int numpkts,
		unsigned int numrqts);

extern void
streamer_stop_xfer (
		void);

extern bool
streamer_is_running (
		void);

extern int
streamer_start_xfer (
		void);

#endif /* INCLUDED_USBMETHODS_H */

/*[]*/

