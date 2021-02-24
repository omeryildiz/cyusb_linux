/************************************************************************************************
 * Program Name		:	09_cyusb_performance.cpp					*
 * Description		:	This is a CLI program which can be used to measure the		*
 *				data transfer rate for data (IN or OUT endpoint) transfers	*
 *				from a Cypress USB device. Endpoints of type Bulk, Interrupt 	*
 *				and Isochronous are supported.					*
 * Author		:	Karthik Sivaramakrishnan					*
 * License		:	LGPL Ver 2.1							*
 * Copyright		:	Cypress Semiconductors Inc.					*
 * Date written		:	May 5, 2015							*
 ***********************************************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>


#include "../include/cyusb.h"

// Variables storing the user provided application configuration.
unsigned int endpoint   = 0;	// Endpoint to be tested
unsigned int reqsize    = 16;	// Request size in number of packets
unsigned int queuedepth = 16;	// Number of requests to queue
unsigned int duration   = 100;	// Duration of the test in seconds

cyusb_handle		*dev_handle = NULL;	// Handle to the USB device
unsigned char		eptype;			// Type of endpoint (transfer type)
unsigned int		pktsize;		// Maximum packet size for the endpoint

unsigned int            success_count = 0;	// Number of successful transfers
unsigned int            failure_count = 0;	// Number of failed transfers
unsigned int 		transfer_size = 0;	// Size of data transfers performed so far
unsigned int		transfer_index = 0;	// Write index into the transfer_size array
volatile bool		stop_transfers = false;	// Request to stop data transfers
volatile int		rqts_in_flight = 0;	// Number of transfers that are in progress

struct timeval		start_ts;		// Data transfer start time stamp.
struct timeval		end_ts;			// Data transfer stop time stamp.

// Function: xfer_callback
// This is the call back function called by libusb upon completion of a queued data transfer.
static void
xfer_callback (
		struct libusb_transfer *transfer)
{
	unsigned int elapsed_time;
	int size = 0;

	// Reduce the number of requests in flight.
	rqts_in_flight--;

	// Check if the transfer has succeeded.
	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {

		failure_count++;

	} else {

		if (eptype == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS) {

			// Loop through all the packets and check the status of each packet transfer
			for (int i = 0; i < reqsize; ++i) {

				// Calculate the actual size of data transferred in each micro-frame.
				if (transfer->iso_packet_desc[i].status == LIBUSB_TRANSFER_COMPLETED) {
					size += transfer->iso_packet_desc[i].actual_length;
				}
			}

		} else {
			size = reqsize * pktsize;
		}

		success_count++;
	}

	// Update the actual transfer size for this request.
	transfer_size += size;

	// Print the transfer statistics when queuedepth transfers are completed.
	transfer_index++;
	if (transfer_index == queuedepth) {

		gettimeofday (&end_ts, NULL);
		elapsed_time = ((end_ts.tv_sec - start_ts.tv_sec) * 1000000 +
			(end_ts.tv_usec - start_ts.tv_usec));

		printf ("Transfer Counts: %d pass %d fail\n", success_count, failure_count);
		printf ("Data rate: %f KBps\n\n", (((double)transfer_size / 1024) / ((double)elapsed_time / 1000000)));

		transfer_index = 0;
		transfer_size  = 0;
		start_ts = end_ts;
	}

	// Prepare and re-submit the read request.
	if (!stop_transfers) {

		switch (eptype) {
			case LIBUSB_TRANSFER_TYPE_BULK:
				if (libusb_submit_transfer (transfer) == 0)
					rqts_in_flight++;
				break;

			case LIBUSB_TRANSFER_TYPE_INTERRUPT:
				if (libusb_submit_transfer (transfer) == 0)
					rqts_in_flight++;
				break;

			case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
				libusb_set_iso_packet_lengths (transfer, pktsize);
				if (libusb_submit_transfer (transfer) == 0)
					rqts_in_flight++;
				break;

			default:
				break;
		}
	}
}

// Function to free data buffers and transfer structures
static void
free_transfer_buffers (
		unsigned char          **databuffers,
		struct libusb_transfer **transfers)
{
	// Free up any allocated data buffers
	if (databuffers != NULL) {
		for (unsigned int i = 0; i < queuedepth; i++) {
			if (databuffers[i] != NULL) {
				free (databuffers[i]);
			}
			databuffers[i] = NULL;
		}
		free (databuffers);
	}

	// Free up any allocated transfer structures
	if (transfers != NULL) {
		for (unsigned int i = 0; i < queuedepth; i++) {
			if (transfers[i] != NULL) {
				libusb_free_transfer (transfers[i]);
			}
			transfers[i] = NULL;
		}
		free (transfers);
	}
}

// Prints application usage information.
static void
print_usage (
		const char *progname)
{
	printf ("%s: USB data transfer performance test\n", progname);
	printf ("\n");
	printf ("Usage: %s -e <epnum> -s <reqsize> -q <queuedepth> -d <duration>\n", progname);
	printf ("\twhere\n");
	printf ("\t\tepnum is the endpoint to be tested\n");
	printf ("\t\treqsize is the size of individual data transfer requests in packets or bursts\n");
	printf ("\t\tqueuedepth is the number of requests to be queued at a time\n");
	printf ("\t\tduration is the duration in seconds for which the test is to be run\n");
	printf ("\n");
}

int main (
		int argc,
		char **argv)

{
	extern char *optarg;
	char         c;

	libusb_device_descriptor deviceDesc;
	libusb_config_descriptor *configDesc;
	libusb_interface_descriptor *interfaceDesc;
	libusb_endpoint_descriptor *endpointDesc;
	libusb_ss_endpoint_companion_descriptor *companionDesc;

	int  rStatus;
	int  if_numsettings;
	bool found_ep = false;

	struct libusb_transfer **transfers = NULL;		// List of transfer structures.
	unsigned char **databuffers = NULL;			// List of data buffers.

	struct timeval t1, t2;					// Timestamps used for test duration control

	// Parse command line parameters
	while ((c = getopt (argc, argv, "e:s:q:d:h")) != -1) {
		switch (c) {
			case 'e':
				// Get the endpoint number.
				if (sscanf ((const char *)optarg, "%d", &endpoint) != 1) {
					printf ("%s: Failed to parse endpoint number\n", argv[0]);
					print_usage (argv[0]);
					return (-EINVAL);
				}

				// Check for validity of the endpoint
				if (((endpoint & 0x70) != 0) || ((endpoint & 0x0F) == 0)) {
					printf ("%s: Invalid endpoint 0x%x specified\n", argv[0], endpoint);
					print_usage (argv[0]);
					return (-EINVAL);
				}
				break;

			case 's':
				// Get the request size value.
				if (sscanf ((const char *)optarg, "%d", &reqsize) != 1) {
					printf ("%s: Failed to parse request size\n", argv[0]);
					print_usage (argv[0]);
					return (-EINVAL);
				}
				break;

			case 'q':
				// Get the request size value.
				if (sscanf ((const char *)optarg, "%d", &queuedepth) != 1) {
					printf ("%s: Failed to parse queue depth\n", argv[0]);
					print_usage (argv[0]);
					return (-EINVAL);
				}
				break;

			case 'd':
				// Get the request size value.
				if (sscanf ((const char *)optarg, "%d", &duration) != 1) {
					printf ("%s: Failed to parse test duration\n", argv[0]);
					print_usage (argv[0]);
					return (-EINVAL);
				}
				break;

			case 'h':
				// Print the usage information and quit.
				print_usage (argv[0]);
				return (0);
				break;

			default:
				// Unknown option.
				printf ("%s: Unsupported switch -%c\n", argv[0], c);
				print_usage (argv[0]);
				return (-EINVAL);
				break;
		}
	}

	// Find the USB device and locate the endpoint to be tested.

	// Step 1: Initialize the cyusb library and check if any devices are detected.
	rStatus = cyusb_open ();
	if (rStatus < 0) {
		printf ("%s: Failed to initialize cyusb library\n", argv[0]);
		return -EACCES;
	}
	else {
		if (rStatus == 0) {
			printf ("%s: No USB device found\n", argv[0]);
			return -ENODEV;
		}
	}

	// Step 2: Get a handle to the first CyUSB device.
	dev_handle = cyusb_gethandle (0);
	if (dev_handle == NULL) {
		printf ("%s: Failed to get CyUSB device handle\n", argv[0]);
		return -EACCES;
	}

	// Step 3: Read the configuration descriptor.
	rStatus = cyusb_get_config_descriptor (dev_handle, 0, &configDesc);
	if (rStatus != 0) {
		printf ("%s: Failed to get USB Configuration descriptor\n", argv[0]);
		cyusb_close ();
		return -EACCES;
	}

	// Step 4: Check each of the interfaces one by one and check if we can find the desired endpoint there.
	for (int i = 0; i < configDesc->bNumInterfaces; i++) {

		// Step 4.a: Claim the interface
		rStatus = cyusb_claim_interface (dev_handle, i);
		if (rStatus != 0) {
			printf ("%s: Failed to claim interface %d\n", argv[0], i);
			cyusb_free_config_descriptor (configDesc);
			cyusb_close ();
			return -EACCES;
		}

		// Step 4.b: Get each of the interface descriptors and check if the endpoint is present.
		if_numsettings = configDesc->interface[i].num_altsetting;
		for (int j = 0; j < if_numsettings; j++) {

			interfaceDesc = (libusb_interface_descriptor *)&(configDesc->interface[i].altsetting[j]);

			// Step 4.b.1: Check if the desired endpoint is present.
			for (int k = 0; k < interfaceDesc->bNumEndpoints; k++) {

				endpointDesc = (libusb_endpoint_descriptor *)&(interfaceDesc->endpoint[k]);
				if (endpointDesc->bEndpointAddress == endpoint) {
					printf ("%s: Found endpoint 0x%x in interface %d, setting %d\n",
							argv[0], endpoint, i, j);

					// If the alt setting is not 0, select it
					cyusb_set_interface_alt_setting (dev_handle, i, j);
					found_ep = true;
					break;
				}
			}

			if (found_ep)
				break;
		}

		if (found_ep)
			break;

		// Step 4.c: Release the interface as the endpoint was not found.
		cyusb_release_interface (dev_handle, i);
	}

	if (!found_ep) {
		printf ("%s: Failed to find endpoint 0x%x on device\n", argv[0], endpoint);
		cyusb_free_config_descriptor (configDesc);
		cyusb_close ();
		return (-ENOENT);
	}

	// Store the endpoint type and maximum packet size
	eptype  = endpointDesc->bmAttributes;

	cyusb_get_device_descriptor (dev_handle, &deviceDesc);
	if (deviceDesc.bcdUSB >= 0x0300) {

		// If this is a USB 3.0 connection, get the endpoint companion descriptor
		// The packet size for the endpoint is calculated as the product of the max
		// packet size and the burst size. For Isochronous endpoints, this value is
		// multiplied by the mult value as well.

		libusb_get_ss_endpoint_companion_descriptor (NULL, endpointDesc, &companionDesc);

		if (eptype == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS)
			pktsize = endpointDesc->wMaxPacketSize * (companionDesc->bMaxBurst + 1) *
				(companionDesc->bmAttributes + 1);
		else
			pktsize = endpointDesc->wMaxPacketSize * (companionDesc->bMaxBurst + 1);

		libusb_free_ss_endpoint_companion_descriptor (companionDesc);

	} else {

		// Not USB 3.0. For Isochronous endpoints, get the packet size as computed by
		// the library. For other endpoints, use the max packet size as it is.
		if (eptype == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS)
			pktsize = cyusb_get_max_iso_packet_size (dev_handle, endpoint);
		else
			pktsize = endpointDesc->wMaxPacketSize;

	}

	// Print the test parameters.
	printf ("%s: Starting test with the following parameters\n", argv[0]);
	printf ("\tRequest size     : 0x%x\n", reqsize);
	printf ("\tQueue depth      : 0x%x\n", queuedepth);
	printf ("\tTest duration    : 0x%x\n", duration);
	printf ("\tEndpoint to test : 0x%x\n", endpoint);
	printf ("\n");
	printf ("\tEndpoint type    : 0x%x\n", eptype);
	printf ("\tMax packet size  : 0x%x\n", pktsize);

	// Allocate buffers and transfer structures
	bool allocfail = false;

	databuffers = (unsigned char **)calloc (queuedepth, sizeof (unsigned char *));
	transfers   = (struct libusb_transfer **)calloc (queuedepth, sizeof (struct libusb_transfer *));

	if ((databuffers != NULL) && (transfers != NULL)) {

		for (unsigned int i = 0; i < queuedepth; i++) {

			databuffers[i] = (unsigned char *)malloc (reqsize * pktsize);
			transfers[i]   = libusb_alloc_transfer (
					(eptype == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS) ? reqsize : 0);

			if ((databuffers[i] == NULL) || (transfers[i] == NULL)) {
				allocfail = true;
				break;
			}
		}

	} else {
		allocfail = true;
	}

	// Check if all memory allocations have succeeded
	if (allocfail) {
		printf ("%s: Failed to allocate buffers and transfer structures\n", argv[0]);
		free_transfer_buffers (databuffers, transfers);

		cyusb_free_config_descriptor (configDesc);
		cyusb_close ();
		return (-ENOMEM);
	}

	// Take the transfer start timestamp
	gettimeofday (&start_ts, NULL);

	// Launch all the transfers till queue depth is complete
	for (unsigned int i = 0; i < queuedepth; i++) {
		switch (eptype) {
			case LIBUSB_TRANSFER_TYPE_BULK:
				libusb_fill_bulk_transfer (transfers[i], dev_handle, endpoint,
						databuffers[i], reqsize * pktsize, xfer_callback, NULL, 5000);
				rStatus = libusb_submit_transfer (transfers[i]);
				if (rStatus == 0)
					rqts_in_flight++;
				break;

			case LIBUSB_TRANSFER_TYPE_INTERRUPT:
				libusb_fill_interrupt_transfer (transfers[i], dev_handle, endpoint,
						databuffers[i], reqsize * pktsize, xfer_callback, NULL, 5000);
				rStatus = libusb_submit_transfer (transfers[i]);
				if (rStatus == 0)
					rqts_in_flight++;
				break;

			case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
				libusb_fill_iso_transfer (transfers[i], dev_handle, endpoint, databuffers[i],
						reqsize * pktsize, reqsize, xfer_callback, NULL, 5000);
				libusb_set_iso_packet_lengths (transfers[i], pktsize);
				rStatus = libusb_submit_transfer (transfers[i]);
				if (rStatus == 0)
					rqts_in_flight++;
				break;

			default:
				break;
		}
	}

	gettimeofday (&t1, NULL);
	do {
		libusb_handle_events (NULL);
		gettimeofday (&t2, NULL);
	} while (t2.tv_sec < (t1.tv_sec + duration));

	// Test duration elapsed. Set the stop_transfers flag and wait until all transfers are complete.
	printf ("%s: Test duration is complete. Stopping transfers\n", argv[0]);
	stop_transfers = true;
	while (rqts_in_flight != 0) {
		printf ("%d requests are pending\n", rqts_in_flight);
		libusb_handle_events (NULL);
		sleep (1);
	}

	// All transfers are complete. We can now free up all structures.
	printf ("%s: Transfers completed\n", argv[0]);

	free_transfer_buffers (databuffers, transfers);
	cyusb_free_config_descriptor (configDesc);
	cyusb_close();

	printf ("%s: Test completed\n", argv[0]);
	return 0;
}

/*[]*/

