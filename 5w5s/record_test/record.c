#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <error.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#define UNIT_TEST 1
#define LOG_DIR "/tmp/stb_log"

/**
 * Get file path for today's log file.
 *
 * It's caller's responsibility to free the allocated
 * string.
 */
char *file_today(void)
{
	time_t cur_time = time(NULL);
	struct tm *tms = localtime(&cur_time);
	char *str = (char *)malloc(30 * sizeof(char));

	if (!str) {
		perror("malloc");
		return NULL;
	}

	/*
	 * File format: yyyy-ddd
	 * where "ddd" means the days of the year.
	 */
	sprintf(str, "%s/%d-%03d", LOG_DIR, tms->tm_year + 1900,
			tms->tm_yday);

	return str;
}

/**
 * Get the recent history file name. 
 *
 * @param prior - the days prior to today.
 * For example, when we want to get the file name of the day
 * before yesterday, prior = 2.
 */
char *get_file_name(int prior) {
	// TODO: implement me!
	return null;
}

/**
 * Open the log file of a certain date.
 */
FILE *open_log(char *path)
{
	if (!path)
		return NULL;

	return fopen(path, "a+");
}

/**
 * Open today's log file. 
 * If the file does not exist, create a new one.
 */
FILE *open_todays_log(void)
{
	FILE *fp;
	char *path = file_today();

	fp = open_log(path);
	free(path);

	return fp;
}

/**
 * Close a log file.
 */
int close_log(FILE *fp)
{
	return fclose(fp);
}

/**
 * Read all messages in today's log file and 
 * parse them for future use.
 */
int parse_todays_log(void) 
{
	FILE *fp = open_todays_log();
	int retval = 0;

	if (!fp) {
		perror("open log");
		return -1;
	}

	ssize_t readcnt = -1;
	size_t size = 0;
	char *line = NULL;

	while ((readcnt = getline(&line, &size, fp)) != -1) {
		retval += size;
		// now just print them out.
		printf("%s", line);
	}

	return retval;
}

/**
 * Count the number of sensor events from recent
 * @days files and return the total count number.
 */
int parse_recent_logs(int days) {
	// TODO: implement me!
}

/**
 * Write a log message into today's log file.
 */
int write_todays_log(char *str)
{
	FILE *fp = open_todays_log();
	int retval = -1;

	if (!fp) {
		perror("open log");
		return -1;
	}

	if (str) {
		retval = fprintf(fp, "%s\n", str);
		free(str);  // @str was allocated by asm_logmsg()
	}

	if (close_log(fp)) {
		perror("close log");
		return -1;
	}
	
	return retval;
}

/**
 * Assemble a log message.
 * The time will be the current local time.
 */
char *asm_logmsg(char *kind)
{
	char *retval;

	if (strlen(kind) > 100) {
		fprintf(stderr, "invalid parameter!\n");
	}

	int len = strlen(kind) + 10;
	time_t cur_time = time(NULL);
	struct tm *tms = localtime(&cur_time);

	// will be freed by write_todays_log()
	retval = (char *)malloc(len);  	
	if (!retval) {
		perror("malloc");
		exit(errno);
	}
	sprintf(retval, "%02d:%02d|%s", tms->tm_hour, tms->tm_min,
			kind);

	return retval;
}

/**
 * Initialize the recording procedure.
 */
int init_record(void)
{
	// make appropriate log directory.
	int status = 0;
	if (access(LOG_DIR, F_OK)) {
		status = mkdir(LOG_DIR, 
				S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	}

	return status;
}

#if UNIT_TEST
int main()
{
	if (init_record()) {
		perror("mkdir");
		exit(errno);
	}

	if (write_todays_log(asm_logmsg("An event occurs")) == -1) {
		perror("Write log");
		exit(errno);
	}
	sleep(2);
	if (write_todays_log(asm_logmsg("An event occurs")) == -1) {
		perror("Write log");
		exit(errno);
	}
	sleep(2);
	if (parse_log() == -1) {
		perror("Parse log");
		exit(errno);
	}
	
	return 0;
}
#endif
