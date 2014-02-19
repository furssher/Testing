/** 
 * stb_host_usb.c: for STB receiving power-off instructions
 *                 from the sensor kit via USB.
 * compile: gcc -lusb-1.0 stb_host_usb.c stb_host_usb
 **/
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "libusb-1.0/libusb.h" /* Remember to set up the environment */

/* The vendor and product ID of the sensor kit */
#define DEV_ID_VENDOR	 0x04d8
#define DEV_ID_PRODUCT	 0x000a  

/* Defined control messages */
#define MOTION_ON		'M'
#define MOTION_OFF		'm'
#define LIGHT_ON		'L'
#define LIGHT_OFF		'l'
#define SOUND_ON		'S'
#define SOUND_OFF		's'
#define BUTTON_PRESSED	'B'
#define POWCTRL_PRESSED	'P'
#define OTHCTRL_PRESSED	'O'

#define MSGBUF_SIZE		1

/* USB communication defines */
#define EP_INTR			(1 | LIBUSB_ENDPOINT_IN)
#define EP_DATA			(2 | LIBUSB_ENDPOINT_IN)
#define CTRL_IN			(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN)
#define CTRL_OUT		(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT)

/* Device status enumeration */
enum {
	DEV_DETACHED = 0, DEV_ATTACHED, DEV_OPENED, 
};
int device_status = DEV_DETACHED;

/* Power mode invariables */
enum {
	POWER_FULL = 0, POWER_SLEEP, POWER_HIBERNATE,
};
int power_mode;

/* Power mode transition timeout invariables */
#define FULL_TO_SLEEP_TIMEOUT			10
#define SLEEP_TO_HIBERNATE_TIMEOUT		10
#define HIBERNATE_TIMEOUT				0  /* Fake time slot */
int timeout[3] = {
	FULL_TO_SLEEP_TIMEOUT,
	SLEEP_TO_HIBERNATE_TIMEOUT,
	HIBERNATE_TIMEOUT
};

/* Only for demo use */
#define DEMO    1

/* Device handler */
libusb_device_handle *dev_handle;

/* Control message buffer */
unsigned char msgbuf[MSGBUF_SIZE];

/* Control message transfer */
struct libusb_transfer *msg_transfer;

/* Exit indicator */
int do_exit = 0;

/* Timer switch: when there is a wake-sensor-message,
 * the timer (for timeout) should be paused */
enum {
	TIMER_STOP, TIMER_RESET, TIMER_GO_ON,
};
int timer_status;

/*
 * Motion sensor flag.
 */
enum {
	M_OFF, M_ON,
};
int motion_status;

/*
 * GUI Demo interface
 */
enum {
	ECHO_HIBERNATE = 0, ECHO_SLEEP, ECHO_FULL,
};
void demo_echo(int status)
{
	if (status >= 3)
		return;
	printf("%d", status);
	fflush(stdout);
}

/**
 * TODO: DEMO: simulate three scenarios of CPU (hence power)
 * consumption.
 */
#define PI_NUM	4
pid_t pi_pid[PI_NUM];

int demo_produce(pid_t pid[], int n)
{
#if DEMO
	int i;
	int cnt = n;

	if (n > PI_NUM || n < 0)
		return -1;
	for (i = 0; i < PI_NUM && cnt; i++) {
		if (pid[i] != 0) {
			continue;
		}
		if ((pid[i] = fork()) < 0) {
			perror("fork");
		} else if (pid[i] == 0) {
			//system("cat /dev/zero > /dev/null");
			execlp("/bin/sh", "/bin/sh", "-c", 
				   "cat /dev/zero > /dev/null",
				   (char *) 0);
			perror("execlp");
		} else {
			cnt--;	
		}
	}
#endif
	return 0;
}

int demo_cleanup(pid_t pid[], int n)
{
#if DEMO
	int i;
	int clean_pid;
	int cnt = n;
	char *numbuf;

	if (n > PI_NUM || n < 0)
		return -1;
	for (i = 0; i < PI_NUM && cnt; i++) {
		if (pid[i] == 0) {
			continue;
		}
		if (kill(pid[i], SIGKILL)) {
			perror("Kill");
		}
		cnt--;
		pid[i] = 0;
	}
	for (i = 0; i < n; i++) {
		wait(NULL);
	}

	numbuf = malloc(3*sizeof(char)); // FIXME
	if (!numbuf) {
		return -ENOMEM;
	}
	sprintf(numbuf, "%d", n);
	if ((clean_pid = fork()) < 0) {
		perror("fork");
	} else if (clean_pid == 0) {
		execlp("/opt/project/multip/clean.sh",
				"/opt/project/multip/clean.sh",
				numbuf, (char *) 0);
		perror("execlp");
	} else {
		waitpid(clean_pid, NULL, 0);
	}
#endif
	return 0;
}

/**
 * USB utility functions
 **/
static int dev_open(libusb_device *dev, 
		            libusb_device_handle **handle)
{
	int rc;

	if (DEV_OPENED == device_status)
		return 0;

	rc = libusb_open(dev, handle);
	switch (rc) {
	case 0:	/* On success */
		device_status = DEV_OPENED;
		//fprintf(stderr, "open successfully!\n");
		break;
	case LIBUSB_ERROR_NO_MEM:
		fprintf(stderr, "ERROR: no memory!\n");
		break;
	case LIBUSB_ERROR_ACCESS:
		fprintf(stderr, "ERROR: cannot access!\n");
		break;
	case LIBUSB_ERROR_NO_DEVICE:
		fprintf(stderr, "ERROR: device detached again!\n");
		break;
	default:
		fprintf(stderr, "Unknown ERROR!\n");
		break;
	}

	return rc;
}

static int LIBUSB_CALL hotplug_callback(libusb_context *ctx, 
			libusb_device *dev, libusb_hotplug_event event, 
			void *user_data)
{
	struct libusb_device_descriptor desc;
	int rc;

	rc = libusb_get_device_descriptor(dev, &desc);
	if (LIBUSB_SUCCESS != rc) {
		fprintf(stderr, "Cannot get device descriptor!\n");
		return -1;
	}

	device_status = DEV_ATTACHED;
	fprintf(stderr, "Device attached: %04x:%04x...", desc.idVendor,
										             desc.idProduct);
	rc = dev_open(dev, &dev_handle);

	if (0 == rc) {
		fprintf(stderr, "Open successfully...(from cb func)\n");
		return 0;
	}

	return -1;
}

static int LIBUSB_CALL hotplug_callback_detach(libusb_context *ctx, 
			libusb_device *dev, libusb_hotplug_event event, 
			void *user_data)
{
	fprintf(stderr, "Device detached...\n");
	device_status = DEV_DETACHED;

	libusb_close(dev_handle);
	return 0;
}

/* Alarm signal handler for timeout use */
void alarm_handler(int signum)
{
	signal(SIGALRM, SIG_IGN);
	fprintf(stderr, "In alarm_handler(): ");

	if (motion_status == M_ON) {
		fprintf(stderr, "Motion sensor still active\n");
		goto reset;
	}

	if (power_mode == POWER_FULL) {
		fprintf(stderr, "Timeout: %d. Go to sleep.\n",
				                  FULL_TO_SLEEP_TIMEOUT);
		power_mode = POWER_SLEEP;
		demo_cleanup(pi_pid, 2);
		alarm(timeout[POWER_SLEEP]);
	} else if (power_mode == POWER_SLEEP) {
		fprintf(stderr, "Timeout: %d, Go to hibernate.\n",
				                  SLEEP_TO_HIBERNATE_TIMEOUT);
		power_mode = POWER_HIBERNATE;
		demo_cleanup(pi_pid, 2);
	} else if (power_mode == POWER_HIBERNATE) {
		fprintf(stderr, "Timeout: hibernating now...nobody wakes me up.\n");
	}

reset:	
	signal(SIGALRM, alarm_handler);
}

/* The state machine */
void next_power_mode(unsigned char msg)
{
	switch (power_mode) {
	case POWER_FULL: 
		switch (msg) {
		case BUTTON_PRESSED:
			fprintf(stderr, "(FULL): Button pressed\n");
            power_mode = POWER_SLEEP;   
			demo_echo(ECHO_SLEEP);
			demo_cleanup(pi_pid, 2);    
            timer_status = TIMER_RESET;  
			return;
		case MOTION_ON:
			/* Turn on this flag until *MOTION_OFF* received */
            timer_status = TIMER_STOP; 
			motion_status = M_ON;
			fprintf(stderr, "(FULL): Motion detected: timer paused\n");
			return;
		case MOTION_OFF:
			/* Resume the timer */
            timer_status = TIMER_RESET; 
			motion_status = M_OFF;
			fprintf(stderr, "(FULL): Motion disappears: timer resumed\n");
			return;
		case POWCTRL_PRESSED:
			/* Return to sleep directly */
			fprintf(stderr, "(FULL) Go to sleep\n");
			power_mode = POWER_SLEEP;
			demo_echo(ECHO_SLEEP);
			demo_cleanup(pi_pid, 2);
            timer_status = TIMER_RESET;  
			return;
		case SOUND_OFF: 
			return;
		case SOUND_ON:
		case LIGHT_ON: 
		case LIGHT_OFF: 
			fprintf(stderr, "(FULL): Light/Sound events occurred\n");
            timer_status = TIMER_RESET; 
			return;
		case OTHCTRL_PRESSED:
			fprintf(stderr, "(FULL): Other button pressed\n");
            timer_status = TIMER_RESET; 
			return;
		default:
			fprintf(stderr, "(Full): Instruction '%c' not supported\n", msg);
			return;
		}
		break;
	case POWER_SLEEP: 
		switch (msg) {
		case BUTTON_PRESSED:
			fprintf(stderr, "SLEEP --> FULL: button pressed!\n");
			power_mode = POWER_FULL;
			demo_echo(ECHO_FULL);
			demo_produce(pi_pid, 2);
            timer_status = TIMER_RESET;
			return;
		case MOTION_ON:
			/* Turn on this flag until *MOTION_OFF* received */
            timer_status = TIMER_STOP; 
			motion_status = M_ON;
			fprintf(stderr, "(SLEEP-->SLEEP): Motion detected: timer paused\n");
			return;
		case MOTION_OFF:
            timer_status = TIMER_RESET; 
			motion_status = M_OFF;
			fprintf(stderr, "(!SLEEP!): Motion disappears: timer resumed\n");
			return;
		case POWCTRL_PRESSED:
			/* Go back to FULL */
			fprintf(stderr, "(SLEEP): Back to FULL\n");
			power_mode = POWER_FULL;
			demo_echo(ECHO_FULL);
			demo_produce(pi_pid, 2);
            timer_status = TIMER_RESET; 
			return;
		case LIGHT_ON:
		case LIGHT_OFF:
		case SOUND_ON:
			fprintf(stderr, "(SLEEP): Light/Sound events occurred\n");
            timer_status = TIMER_RESET; 
			return;
		case SOUND_OFF:
			//fprintf(stderr, "(SLEEP): Light/Sound events occurred\n");
			return;
		case OTHCTRL_PRESSED:
			fprintf(stderr, "(SLEEP): Other button pressed\n");
            timer_status = TIMER_RESET; 
			return;
		default: 
			fprintf(stderr, "(SLEEP): Instruction '%c' not supported\n", msg);
			return;
		}
		break;
	case POWER_HIBERNATE:
		switch (msg) {
		case BUTTON_PRESSED:
			fprintf(stderr, "(HIBERNATE-->FULL): button pressed! \n");
			power_mode = POWER_FULL;  
			demo_echo(ECHO_FULL);
			demo_produce(pi_pid, 4);  
            timer_status = TIMER_RESET; 
			return;
		case MOTION_ON:
			/* Turn on this flag until *MOTION_OFF* received */
			power_mode = POWER_SLEEP;
			demo_echo(ECHO_SLEEP);
            timer_status = TIMER_STOP; 
			motion_status = M_ON;
			demo_produce(pi_pid, 2);
			fprintf(stderr, "(HIBERNATE-->SLEEP): Motion detected: timer paused\n");
			return;
		case MOTION_OFF:
			/* Resume the timer */ 
			power_mode = POWER_SLEEP;
			demo_echo(ECHO_SLEEP);
            timer_status = TIMER_RESET; 
			motion_status = M_OFF;
			fprintf(stderr, "(HIBERNATE-->SLEEP): Motion disappears: timer resumed\n");
			return;
		case POWCTRL_PRESSED:
			/* Go to FULL directly */
			power_mode = POWER_FULL;
			demo_echo(ECHO_FULL);
			demo_produce(pi_pid, 4);
			fprintf(stderr, "(HIBERNATE-->FULL): Power button pressed\n");
            timer_status = TIMER_RESET; 
			return;
		case LIGHT_ON:
		case LIGHT_OFF:
		case SOUND_ON:
			power_mode = POWER_SLEEP;
			demo_echo(ECHO_SLEEP);
			demo_produce(pi_pid, 2);
			fprintf(stderr, "(HIBERNATE): Light/Sound event occurred, go to SLEEP\n");
            timer_status = TIMER_RESET; 
			return;
		case SOUND_OFF:
			//fprintf(stderr, "(HIBERNATE): Sound events occurred\n");
			return;
		case OTHCTRL_PRESSED:
			fprintf(stderr, "(HIBERNATE): Other button pressed\n");
			power_mode = POWER_SLEEP;   
			demo_echo(ECHO_SLEEP);
			demo_produce(pi_pid, 2);    
            timer_status = TIMER_RESET; 
			return;
		default: 
			fprintf(stderr, "(HIBERNATE): Instruction '%c' not supported\n", msg);
			return;
		}
		break;
	default:
		fprintf(stderr, "Unknown power mode!\n");
		break;
	}
}

int main(int argc, char *argv[])
{
	int i;
	int rc, msg_received;
	int vendor_id, product_id, class_id;
	unsigned char *buf = malloc(sizeof(unsigned char));
	libusb_hotplug_callback_handle cb_attch_handle;
	libusb_hotplug_callback_handle cb_dttch_handle;
	libusb_device *dev;

	/* How to get that information automatically */
	vendor_id  = DEV_ID_VENDOR;
	product_id = DEV_ID_PRODUCT;
	class_id   = LIBUSB_HOTPLUG_MATCH_ANY;

	rc = libusb_init(NULL);

	/* Set the debug log */
	libusb_set_debug(NULL, 3);

	if (rc < 0) {
		fprintf(stderr, "failed to initialise libusb: %s\n", 
				           libusb_error_name(rc));
		goto out;
	}
	if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
		fprintf(stderr, "Hotplug capabilites are not supported "
				 "on this platform\n");
		goto out;
	}

	/* Register two hotplug callback functions */
	rc = libusb_hotplug_register_callback(NULL, /* context */
			        LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED,
					0, /* libusb_hotplug_flag */
					vendor_id, product_id, class_id, 
					hotplug_callback, NULL, /* user data */
					&cb_attch_handle);	
	if (LIBUSB_SUCCESS != rc) {
		fprintf(stderr, "Error registering hotplug callback!\n");
		goto out; 
	}
	rc = libusb_hotplug_register_callback(NULL, /* context */
			        LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
					0, /* libusb_hotplug_flag */
					vendor_id, product_id, class_id, 
					hotplug_callback_detach, NULL, /* user data */
					&cb_dttch_handle);	
	if (LIBUSB_SUCCESS != rc) {
		fprintf(stderr, "Error registering hotplug_detach callback\n");
		goto out;
	}

	/* Try to open the sensor kit */
	dev_handle = libusb_open_device_with_vid_pid(NULL, 
			            vendor_id, product_id);
	if (dev_handle) {
		device_status = DEV_ATTACHED;
		dev = libusb_get_device(dev_handle);
		if (!dev) {
			fprintf(stderr, "Cannot get the device from its handle\n");
			goto closeh;
		}
		rc = dev_open(dev, &dev_handle);
		if (rc != 0) {
			fprintf(stderr, "Cannot open, try again!\n");
		} else {
			fprintf(stderr, "Open successfully...\n");
			device_status = DEV_OPENED;
		}
	} else {
		/* Otherwise wait for hotplug events */
		fprintf(stderr, "Now waiting for sensor kit...\n");
	}

	while (DEV_OPENED != device_status) {
		rc = libusb_handle_events(NULL);
		if (rc < 0) {
			fprintf(stderr, "libusb_handle_events() failed: %s\n",
					libusb_error_name(rc));
		} 
	}

	/* Claim the interface, before that make sure the 
	 * interface is detached from the kernel */
	for (i = 0; i <= 1; i++) {
		if (libusb_kernel_driver_active(dev_handle, i) == 1) {
			fprintf(stderr, "Interface %d, detaching...\n", i);
			libusb_detach_kernel_driver(dev_handle, i);
		} else {
			fprintf(stderr, "Interface %d free from kernel..\n", i);
		}
		rc = libusb_claim_interface(dev_handle, i);
		if (rc < 0) {
			fprintf(stderr, "Interface %d claim failed: %d\n", i, rc);
		}
	}
	
	/* Set the alarm for state switching */
	power_mode = POWER_SLEEP;
	demo_echo(ECHO_SLEEP);
	signal(SIGALRM, alarm_handler);
	alarm(timeout[POWER_SLEEP]);
	timer_status = TIMER_GO_ON;
	motion_status = M_ON;

	for (i = 0; i < PI_NUM; i++) {
		pi_pid[i] = 0;
	}
	demo_produce(pi_pid, 2);

	/* Receive message synchronously */
	while (!do_exit) {
		rc = libusb_bulk_transfer(dev_handle, EP_DATA, buf, 
				                  1, &msg_received, 0);
		if (rc < 0) {
			perror("Bulk transfer failed");
			do_exit = 1;
		} else {
			fprintf(stderr, "Message received: %c\n", buf[0]);
			/* Reset the alarm as some event occurred */
			next_power_mode(buf[0]);
			if (timer_status != TIMER_GO_ON) {
				alarm(0);
				if (timer_status == TIMER_RESET) {
					signal(SIGALRM, alarm_handler);
					alarm(timeout[power_mode]);
				}
			} 
		}
	}

	libusb_release_interface(dev_handle, 0);
	libusb_release_interface(dev_handle, 1);
closeh:
	libusb_close(dev_handle);
out:
	libusb_exit(NULL);

	return do_exit;
}


