/************************************************************************************************
 * Program Name		:	03_getconfig.cpp						*
 * Author		:	V. Radhakrishnan ( rk@atr-labs.com )				*
 * License		:	LGPL Ver 2.1							*
 * Copyright		:	Cypress Semiconductors Inc. / ATR-LABS				*
 * Date written		:	March 22, 2012							*
 * Modification Notes	:									*
 * 												*
 * This program is a CLI program to extract the current configuration of a device.		*
\***********************************************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#include "../include/cyusb.h"

/********** Cut and paste the following & modify as required  **********/
static const char * program_name;
static const char *out_filename = "stdout";
static const char *const short_options = "hv";
static const struct option long_options[] = {
		{ "help",	0,	NULL,	'h'	},
		{ "version",	0,	NULL,	'v'	},
		{ NULL,		0,	NULL,	 0	}
};

static int next_option;

static void print_usage(FILE *stream, int exit_code)
{
	fprintf(stream, "Usage: %s options\n", program_name);
	fprintf(stream, 
		"  -h  --help           Display this usage information.\n"
		"  -v  --version        Print version.\n");

	exit(exit_code);
}
/***********************************************************************/

int main(int argc, char **argv)
{
	int r;
	int config = 0;
	struct libusb_config_descriptor *desc = NULL;
	char tbuf[64];

	program_name = argv[0];

	while ( (next_option = getopt_long(argc, argv, short_options, 
					long_options, NULL) ) != -1 ) {
		switch ( next_option ) {
			case 'h': /* -h or --help  */
				print_usage(stdout, 0);
			case 'v': /* -v or --version */
				printf("%s (Ver 1.0)\n",program_name);
				printf("Copyright (C) 2012 Cypress Semiconductors Inc. / ATR-LABS\n");
				exit(0);
			case '?': /* Invalid option */
				print_usage(stdout, 1);
			default : /* Something else, unexpected */
				abort();
		}
	} 

	r = cyusb_open();
	if ( r < 0 ) {
		printf("Error opening library\n");
		return -1;
	}
	else if ( r == 0 ) {
		printf("No device found\n");
		return 0;
	}
	r = cyusb_get_configuration(cyusb_gethandle(0),&config); 
	if ( r ) {
		cyusb_error(r);
		cyusb_close();
		return r;
	}

	if ( config == 0 ) 
		printf("The device is currently unconfigured\n");
	else
		printf("Device configured. Current configuration = %d\n", config);

	r = cyusb_get_active_config_descriptor(cyusb_gethandle(0), &desc);
	if ( r ) {
		printf("Error retrieving config descriptor\n");
		return r;
	}

	sprintf(tbuf,"bLength             = %d\n",   desc->bLength);
	printf("%s",tbuf);
	sprintf(tbuf,"bDescriptorType     = %d\n",   desc->bDescriptorType);
	printf("%s",tbuf);
	sprintf(tbuf,"TotalLength         = %d\n",   desc->wTotalLength);
	printf("%s",tbuf);
	sprintf(tbuf,"Num. of interfaces  = %d\n",   desc->bNumInterfaces);
	printf("%s",tbuf);
	sprintf(tbuf,"bConfigurationValue = %d\n",   desc->bConfigurationValue);
	printf("%s",tbuf);
	sprintf(tbuf,"iConfiguration      = %d\n",   desc->iConfiguration);
	printf("%s",tbuf);
	sprintf(tbuf,"bmAttributes        = %d\n",   desc->bmAttributes);
	printf("%s",tbuf);
	sprintf(tbuf,"Max Power           = %04d\n", desc->MaxPower);
	printf("%s",tbuf);

	cyusb_free_config_descriptor (desc);
	cyusb_close();

	return 0;
}
