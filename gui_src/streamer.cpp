/*
 * Filename             : streamer.cpp
 * Description          : Provides functions to test USB data transfer performance.
 */

#include <QtCore>
#include <QtGui>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <libusb-1.0/libusb.h>

#include "../include/cyusb.h"
#include "../include/controlcenter.h"

extern ControlCenter *mainwin;
extern cyusb_handle  *h;

// Variables storing the user provided application configuration.
static unsigned int	endpoint   = 0;		// Endpoint to be tested
static unsigned int	reqsize    = 16;	// Request size in number of packets
static unsigned int	queuedepth = 16;	// Number of requests to queue
static unsigned char	eptype;			// Type of endpoint (transfer type)
static unsigned int	pktsize;		// Maximum packet size for the endpoint

static unsigned int	success_count = 0;	// Number of successful transfers
static unsigned int	failure_count = 0;	// Number of failed transfers
static unsigned int	transfer_size = 0;	// Size of data transfers performed so far
static unsigned int	transfer_index = 0;	// Write index into the transfer_size array
static unsigned int	transfer_perf = 0;	// Performance in KBps
static volatile bool	stop_transfers = false;	// Request to stop data transfers
static volatile int	rqts_in_flight = 0;	// Number of transfers that are in progress
static volatile bool	app_running = false;	// Whether the streamer application is running
static pthread_t	strm_thread;		// Thread used for the streamer operation

static struct timeval	start_ts;		// Data transfer start time stamp.
static struct timeval	end_ts;			// Data transfer stop time stamp.

// Function: streamer_set_params
// Sets the streamer test parameters
void
streamer_set_params (
		unsigned int ep,
		unsigned int type,
		unsigned int maxpkt,
		unsigned int numpkts,
		unsigned int numrqts)
{
	endpoint   = ep;
	eptype     = type;
	pktsize    = maxpkt;
	reqsize    = numpkts;
	queuedepth = numrqts;
}

// Function: streamer_stop_xfer
// Requests the streamer operation to be stopped.
void
streamer_stop_xfer (
		void)
{
	stop_transfers = true;
}

// Function: streamer_is_running
// Checks whether the streamer operation is running.
bool
streamer_is_running (
		void)
{
	return app_running;
}

// Function: streamer_update_results
// Gets the streamer test results on an ongoing basis
static void
streamer_update_results (
		void)
{
	char buffer[64];

	// Print the transfer statistics into the character strings and update UI.
	sprintf (buffer, "%d", success_count);
	mainwin->streamer_out_passcnt->setText (buffer);

	sprintf (buffer, "%d", failure_count);
	mainwin->streamer_out_failcnt->setText (buffer);

	sprintf (buffer, "%d", transfer_perf);
	mainwin->streamer_out_perf->setText (buffer);
}

// Function: xfer_callback
// This is the call back function called by libusb upon completion of a queued data transfer.
static void
xfer_callback (
		struct libusb_transfer *transfer)
{
	unsigned int elapsed_time;
	double       performance;
	int          size = 0;

	// Check if the transfer has succeeded.
	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {

		failure_count++;

	} else {

		if (eptype == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS) {

			// Loop through all the packets and check the status of each packet transfer
			for (unsigned int i = 0; i < reqsize; ++i) {

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

		// Calculate the performance in KBps.
		performance    = (((double)transfer_size / 1024) / ((double)elapsed_time / 1000000));
		transfer_perf  = (unsigned int)performance;

		transfer_index = 0;
		transfer_size  = 0;
		start_ts = end_ts;
	}

	// Reduce the number of requests in flight.
	rqts_in_flight--;

	// Prepare and re-submit the read request.
	if (!stop_transfers) {

		// We do not expect a transfer queue attempt to fail in the general case.
		// However, if it does fail; we just ignore the error.
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
}

// Function: streamer_thread_func
// Function that implements the main streamer functionality. This will run on a dedicated thread
// created for the streamer operation.
static void *
streamer_thread_func (
		void *arg)
{
	cyusb_handle *dev_handle = (cyusb_handle *)arg;
	struct libusb_transfer **transfers = NULL;		// List of transfer structures.
	unsigned char **databuffers = NULL;			// List of data buffers.
	int  rStatus;

	// Check for validity of the device handle
	if (dev_handle == NULL) {
		printf ("Failed to get CyUSB device handle\n");
		pthread_exit (NULL);
	}

	// The endpoint is already found and its properties are known.
	printf ("Starting test with the following parameters\n");
	printf ("\tEndpoint to test : 0x%x\n", endpoint);
	printf ("\tEndpoint type    : 0x%x\n", eptype);
	printf ("\tMax packet size  : 0x%x\n", pktsize);
	printf ("\tRequest size     : 0x%x\n", reqsize);
	printf ("\tQueue depth      : 0x%x\n", queuedepth);
	printf ("\n");

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
		printf ("Failed to allocate buffers and transfer structures\n");
		free_transfer_buffers (databuffers, transfers);
		pthread_exit (NULL);
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

	printf ("Queued %d requests\n", rqts_in_flight);

	struct timeval t1, t2, tout;
	gettimeofday (&t1, NULL);

	// Use a 1 second timeout for the libusb_handle_events_timeout call
	tout.tv_sec  = 1;
	tout.tv_usec = 0;

	// Keep handling events until transfer stop is requested.
	do {
		libusb_handle_events_timeout (NULL, &tout);

		// Refresh the performance statistics about once in 0.5 seconds.
		gettimeofday (&t2, NULL);
		if (t2.tv_sec > t1.tv_sec) {
			streamer_update_results ();
			t1 = t2;
		}

	} while (!stop_transfers);

	printf ("Stopping streamer app\n");
	while (rqts_in_flight != 0) {
		printf ("%d requests are pending\n", rqts_in_flight);
		libusb_handle_events_timeout (NULL, &tout);
		sleep (1);
	}

	free_transfer_buffers (databuffers, transfers);
	app_running = false;

	printf ("Streamer test completed\n\n");
	pthread_exit (NULL);
}

// Function: streamer_start_xfer
// Function to start the streamer operation. This creates a new thread which runs the
// data transfers.
int
streamer_start_xfer (
		void)
{
	if (app_running)
		return -EBUSY;

	// Default initialization for variables
	success_count  = 0;
	failure_count  = 0;
	transfer_index = 0;
	transfer_size  = 0;
	transfer_perf  = 0;
	rqts_in_flight = 0;
	stop_transfers = false;

	// Mark application running
	app_running    = true;
	if (pthread_create (&strm_thread, NULL, streamer_thread_func, (void *)h) != 0) {
		app_running = false;
		return -ENOMEM;
	}

	return 0;
}

/*[]*/

