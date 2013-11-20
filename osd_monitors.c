/* 
 *  osd_monitors by Tomas Dvorak, based on osd_clock code.
 *  ___
 *   |omas_dvorak@mailcan.com  (copy'n'paste won't work, use brain :-)
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define MIN(a,b) ((a) < (b) ? (a) : (b))

#define SECTOR_SIZE 512
#define PAGE_SIZE getpagesize()

#define warn(format, ...) fprintf (stderr, "WARN: " format, __VA_ARGS__)
#define user_warn(format, ...) fprintf (stderr, "USER_WARN: " format, __VA_ARGS__)

/* How often [ms] wake up and test if visibility has changed or timer has
 * expired ? */
#define WAKE_INTERVAL 500 

/* Maximum number of level-color pairs */
#define MAX_LEVEL_COLORS 100 

/* internal */
#define MAX_FORMATTED_NUMBER_SIZE 10

/* for getline() */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <xosd.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/statvfs.h>
#include <errno.h>

/* Structures ***************************************************************/

struct level_color {
	float level;
	char *color;
};

struct cfg {
	struct monitor *monitor;
	const char *device;
	const char *format;
	char *font;
	char *color;
	char *outline_color;
	int outline_width;
	int hoffset;
	int voffset;
	xosd_pos vpos;
	xosd_pos hpos;
	int shadow;
	int interval;
	struct level_color level_colors[MAX_LEVEL_COLORS];
	int n_level_colors;
};

struct io_stats {
	float in;
	float out;
};

struct usage_stats {
	float free;
	float total;
};
#define USED_PERCENTAGE(stats) (100.0 * ((stats).total - (stats).free) / (stats).total)

/* Misc. ********************************************************************/

const char*
color_for_level(float level, const struct cfg *cfg)
{
	int i;
	i = 0;
	if (!cfg->n_level_colors)
		return cfg->color;
	for (i = 0; i < cfg->n_level_colors - 1; i++) {
		if (cfg->level_colors[i+1].level > level)
			return cfg->level_colors[i].color;
	}
	return cfg->level_colors[cfg->n_level_colors - 1].color;
}

/* format_number will never write more than maxbufsize chars (including
 * trailing \0) in buf */
void
format_number(char *buf, float number)
{
	const struct format {
		float limit; float divider; const char *format;
	} formats[] = {
		{0.0, 1.0, "%.1f"},
		{10.0, 1.0, "%.0f"},
		{1024.0, 1024.0, "%.1fK"},
		{1024.0*10.0, 1024.0, "%.0fK"},
		{1024.0*1024.0, 1024.0*1024.0, "%.1fM"},
		{1024.0*1024.0*10.0, 1024.0*1024.0, "%.0fM"},
		{1024.0*1024.0*1024.0, 1024.0*1024.0*1024.0, "%.1fG"},
		{1024.0*1024.0*1024.0*10.0, 1024.0*1024.0*1024.0, "%.0fG"},
		{1024.0*1024.0*1024.0*1024.0, 1024.0*1024.0*1024.0*1024.0, "%.1fT"},
		{1024.0*1024.0*1024.0*1024.0*10.0, 1024.0*1024.0*1024.0*1024.0, "%.0fT"},
		{1024.0*1024.0*1024.0*1024.0*1024.0, 1024.0*1024.0*1024.0*1024.0*1024.0, "%.1fP"},
		{1024.0*1024.0*1024.0*1024.0*1024.0*10.0, 1024.0*1024.0*1024.0*1024.0*1024.0, "%.0fP"},
		{1024.0*1024.0*1024.0*1024.0*1024.0*1024.0*1024, 1.0, "%.2e"}
	};
	int i;
	const char n_formats = sizeof(formats)/sizeof(struct format);
	for (i = 0; i < n_formats; i++) {
		if (i == n_formats - 1 || formats[i+1].limit > number) {
			snprintf(buf, MAX_FORMATTED_NUMBER_SIZE, formats[i].format, number/formats[i].divider);
			return;
		}
	}
}

/* maxsize includes trailing \0, valid values are > 0 */
void 
format_io_stats(char *buf, int maxsize, const char *format, struct io_stats *stats)
{
	char formatted_number[MAX_FORMATTED_NUMBER_SIZE];
	char *s;

	maxsize--; /* for trailing \0 */
	for (; *format; format++) {
		if (!maxsize)
			break;
		if (*format != '%') {
			maxsize--;
			*buf++ = *format;
			continue;
		}
		format++;
		switch (*format) {
			case 'i': format_number(formatted_number, stats->in); break;
			case 'o': format_number(formatted_number, stats->out); break;
			case 't': format_number(formatted_number, stats->in + stats->out); break;
			default: *buf++ = *format; maxsize--; continue;
		}
		for (s = formatted_number; *s && --maxsize;)
			*buf++ = *s++;
	}
	*buf = '\0';
}

/* maxsize includes trailing \0, valid values are > 0 */
void 
format_usage_stats(char *buf, int maxsize, const char *format, struct usage_stats *stats)
{
	char formatted_number[MAX_FORMATTED_NUMBER_SIZE];
	char *s;
	float used = stats->total - stats->free;

	maxsize--; /* for trailing \0 */
	for (; *format; format++) {
		if (!maxsize)
			break;
		if (*format != '%') {
			maxsize--;
			*buf++ = *format;
			continue;
		}
		format++;
		switch (*format) {
			case 'f': format_number(formatted_number, stats->free); break;
			case 'F': snprintf(formatted_number, sizeof(formatted_number), "%.1f", 100.0 * stats->free/stats->total); break;
			case 'u': format_number(formatted_number, used); break;
			case 'U': snprintf(formatted_number, sizeof(formatted_number), "%.1f", 100.0 * used/stats->total); break;
			case 't': format_number(formatted_number, stats->total); break;
			default: *buf++ = *format; maxsize--; continue;
		}
		for (s = formatted_number; *s && --maxsize;)
			*buf++ = *s++;
	}
	*buf = '\0';
}

/* From glibc documentation
 * Subtract the `struct timeval' values X and Y, storing the result in RESULT.
 * Return 1 if the difference is negative, otherwise 0.  */ 
int
timeval_subtract(struct timeval *result, const struct timeval *_x, const struct timeval *_y) 
{
	struct timeval x, y;
	memcpy(&x, _x, sizeof(struct timeval));
	memcpy(&y, _y, sizeof(struct timeval));

	if (x.tv_usec < y.tv_usec) {     
		int nsec = (y.tv_usec - x.tv_usec) / 1000000 + 1;
		y.tv_usec -= 1000000 * nsec;
		y.tv_sec += nsec;
	}   
	if (x.tv_usec - y.tv_usec > 1000000) {
		int nsec = (x.tv_usec - y.tv_usec) / 1000000;
		y.tv_usec += 1000000 * nsec;
		y.tv_sec -= nsec;   
	}   
	result->tv_sec = x.tv_sec - y.tv_sec;
	result->tv_usec = x.tv_usec - y.tv_usec;   
	return x.tv_sec < y.tv_sec; 
}

float
speed(float s1, float s2, const struct timeval *t1, const struct timeval *t2)
{
	struct timeval diff;
	timeval_subtract(&diff, t1, t2);
	float speed;
	speed = (s1 - s2) / ((float)diff.tv_usec / 1000000.0 + (float)diff.tv_sec);
	return speed;
}

/* Reads longs from multiple columns of one line in file.
 * ... is a list of (int colnumber, float *result) pairs, sorted by colnumber
 */
void
read_columns_from_file(const char *fname, const char *match_pattern, int n_vals, ...)
{
	FILE *f;
	char *line = NULL;
	char *line_start;
	size_t len = 0;
	char *saveptr = NULL, *token;
	int i, result_idx;
	va_list ap;
	int cur_col, next_col;
	float *result;

	va_start(ap, n_vals);

	if (NULL == (f = fopen(fname, "r"))) {
			perror("fopen");
			exit(EXIT_FAILURE);
	}
	cur_col = 0;
	while ((getline(&line, &len, f)) != -1) {
		for (line_start = line; *line_start == ' '; line_start++);
		if (!strstr(line_start, match_pattern))
			continue;
		/* colon in strtok_r is neccessary for parsing /proc/net/dev */
		token = strtok_r(line_start, " :", &saveptr);
		for (result_idx = 0; result_idx < n_vals; result_idx++) {
			next_col = va_arg(ap, int);
			result = va_arg(ap, float *);
			for (i = 0; i < next_col - cur_col; i++)
				token = strtok_r(NULL, " :", &saveptr);
			if (!token) {
				warn("read_columns_from_file: not enough columns in '%s'('%s')\n", 
						match_pattern, fname);
				goto end;
			}
			*result = atof(token);
			cur_col = next_col;
		}
		goto end;
	}
	warn("read_columns_from_file: '%s' line not found in '%s'\n", match_pattern, fname);

end:
	fclose(f);
	if (line)
		free(line);
	va_end(ap);
}

/* Reads floats from multiple lines in form "string floatnumber" in file.
 * ... is a list of (const char *string, float *result) pairs, in order of
 * appearance in file.
 */
void
read_lines_from_file(const char *fname, int n_vals, ...)
{
	FILE *f;
	char *line = NULL;
	size_t len = 0;
	char *saveptr = NULL, *token;
	va_list ap;
	const char *match_pattern;
	float *result;
	ssize_t read;


	va_start(ap, n_vals);

	if (NULL == (f = fopen(fname, "r"))) {
			perror("fopen");
			exit(EXIT_FAILURE);
	}
	while(n_vals--) {
		match_pattern = va_arg(ap, const char *);
		result = va_arg(ap, float *);
		while (-1 != (read = (getline(&line, &len, f)))) {
			if (strstr(line, match_pattern))
				break;
		}
		if (-1 == read) {
			warn("read_lines_from_file: Not all lines found in '%s'\n", fname);
			break;
		}
		token = strtok_r(line, " ", &saveptr);
		token = strtok_r(NULL, " ", &saveptr);
		*result = atof(token);
	}

	fclose(f);
	if (line)
		free(line);
	va_end(ap);
}

/* Signal handling **********************************************************/

static int visibility = 1;
static int visibility_changed = 0;

void 
signal_handler(int signum)
{
	switch(signum) {
		case SIGUSR1:
			visibility = 0;
			visibility_changed = 1;
			break;
		case SIGUSR2:
			visibility = 1;
			visibility_changed = 1;
			break;
		case SIGHUP:
			visibility = 1 - visibility;
			visibility_changed = 1;
			break;
	}
}

void 
setup_signal_handlers(void)
{
	struct sigaction sigaction_description;
	sigset_t block_mask;

	sigaction_description.sa_handler = &signal_handler;
	sigemptyset(&block_mask);
	sigaction_description.sa_mask = block_mask;
	sigaction_description.sa_flags = 0;
	if (
			sigaction(SIGUSR1, &sigaction_description, NULL) ||
			sigaction(SIGUSR2, &sigaction_description, NULL) ||
			sigaction(SIGHUP, &sigaction_description, NULL)
	   ) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}
}

/* Monitors *****************************************************************/

/* shared */

struct monitor {
	const char *name;
	const char *description;
	const char *default_device;
	const char *default_format;

	void *(*create_stats_data)(const struct cfg *cfg);
	void (*retrieve_stats)(void *stats, const struct cfg *cfg);
	void (*render)(xosd *osd, const struct cfg *cfg,
			const struct timeval *t_now, const struct timeval *t_before,
			const void *stats_now, const void *stats_before); 
};

void *
monitor_create_io_stats_data(const struct cfg *cfg)
{
	return malloc(sizeof(struct io_stats));
}

void 
monitor_type_iospeed_render(xosd *osd, const struct cfg *cfg,
		const struct timeval *t_now, const struct timeval *t_before,
		const void *_io_stats_now, const void *_io_stats_before)
{
	char output[256];
	const struct io_stats *io_stats_now = _io_stats_now;
	const struct io_stats *io_stats_before = _io_stats_before;
	struct io_stats stats_speed;

	stats_speed.in = speed(io_stats_now->in, io_stats_before->in, t_now, t_before);
	stats_speed.out = speed(io_stats_now->out, io_stats_before->out, t_now, t_before);
	format_io_stats(output, sizeof(output), cfg->format, &stats_speed);
	xosd_set_colour(osd, color_for_level(stats_speed.in + stats_speed.out, cfg));
	xosd_display(osd, 1, XOSD_string, output);
}

/* monitor clock */

void 
monitor_type_clock_render(xosd *osd, const struct cfg *cfg,
		const struct timeval *t_now, const struct timeval *t_before,
		const void *_stats_now, const void *_stats_before)
{
	char output[256];
	time_t now = time(NULL);

	strftime(output, sizeof(output) - 1, cfg->format, localtime(&now));
	xosd_display(osd, 1, XOSD_string, output);
}

/* monitor cpu */

struct cpu_stats {
	float user;
	float nice;
	float kernel;
	float idle;
};

void *
monitor_type_cpu_create_stats_data(const struct cfg *cfg)
{
	return malloc(sizeof(struct cpu_stats));
}

void
monitor_type_cpu_retrieve_stats(void *_stats, const struct cfg *cfg)
{
	struct cpu_stats *stats = _stats;
	float user, nice, kernel, idle;

	read_columns_from_file("/proc/stat", cfg->device, 4,
			1, &user, 2, &nice, 3, &kernel, 4, &idle);
	stats->user = user;
	stats->nice = nice;
	stats->kernel = kernel;
	stats->idle = idle;
}

void 
monitor_type_cpu_render(xosd *osd, const struct cfg *cfg,
		const struct timeval *t_now, const struct timeval *t_before,
		const void *_stats_now, const void *_stats_before)
{
	char output[256];
	const struct cpu_stats *stats_now = _stats_now, *stats_before = _stats_before;
	float user = stats_now->user - stats_before->user;
	float nice = stats_now->nice - stats_before->nice;
	float kernel = stats_now->kernel - stats_before->kernel;
	float idle = stats_now->idle - stats_before->idle;
	float f = 100.0 * (1.0 - idle/(user + nice + kernel + idle));

	snprintf(output, sizeof(output) - 1, cfg->format, f);
	xosd_set_colour(osd, color_for_level(f, cfg));
	xosd_display(osd, 1, XOSD_string, output);
}

/* monitor ctxt */

void
monitor_type_ctxt_retrieve_stats(void *_io_stats, const struct cfg *cfg)
{
	struct io_stats *io_stats = _io_stats;
	float ctxt;

	read_columns_from_file("/proc/stat", "ctxt", 1, 1, &ctxt);
	io_stats->in = ctxt;
	io_stats->out = 0;
}

/* monitor running processes */

void 
monitor_type_runps_render(xosd *osd, const struct cfg *cfg,
		const struct timeval *t_now, const struct timeval *t_before,
		const void *_stats_now, const void *_stats_before)
{
	char output[256];
	float running_processes;

	read_columns_from_file("/proc/stat", "procs_running", 1, 1, &running_processes);
	snprintf(output, sizeof(output) - 1, cfg->format, running_processes);
	xosd_display(osd, 1, XOSD_string, output);
}

/* monitor mem */

void 
monitor_type_memory_render(xosd *osd, const struct cfg *cfg,
		const struct timeval *t_now, const struct timeval *t_before,
		const void *_stats_now, const void *_stats_before)
{
	char output[256];
	struct usage_stats usage_stats;
	float total, free, buffers, cached;

	read_lines_from_file("/proc/meminfo", 4, 
			"MemTotal:", &total, 
			"MemFree:", &free, 
			"Buffers:", &buffers, 
			"Cached:", &cached);

	/* This is the way htop computes used memory, and it makes sense. 
	   See http://ubuntuforums.org/showthread.php?t=393176 */
	usage_stats.free = (free + buffers + cached) * 1024;
	usage_stats.total = total * 1024;

	format_usage_stats(output, sizeof(output), cfg->format, &usage_stats);
	xosd_set_colour(osd, color_for_level(USED_PERCENTAGE(usage_stats), cfg));
	xosd_display(osd, 1, XOSD_string, output);
}

/* monitor swap */

void 
monitor_type_swap_render(xosd *osd, const struct cfg *cfg,
		const struct timeval *t_now, const struct timeval *t_before,
		const void *_stats_now, const void *_stats_before)
{
	char output[256];
	struct usage_stats usage_stats;
	float total, free;

	read_lines_from_file("/proc/meminfo", 2, 
			"SwapTotal:", &total, 
			"SwapFree:", &free);

	usage_stats.free = free * 1024.0;
	usage_stats.total = total * 1024.0;
	format_usage_stats(output, sizeof(output), cfg->format, &usage_stats);
	xosd_set_colour(osd, color_for_level(USED_PERCENTAGE(usage_stats), cfg));
	xosd_display(osd, 1, XOSD_string, output);
}

/* monitor swapping activity */

void
monitor_type_swapact_retrieve_stats(void *_io_stats, const struct cfg *cfg)
{
	struct io_stats *io_stats = _io_stats;
	float in, out;

	read_lines_from_file("/proc/vmstat", 2, "pswpin", &in, "pswpout", &out);
	io_stats->in = in * PAGE_SIZE;
	io_stats->out = out * PAGE_SIZE;
}

/* monitor disk usage */

void 
monitor_type_disk_render(xosd *osd, const struct cfg *cfg,
		const struct timeval *t_now, const struct timeval *t_before,
		const void *_stats_now, const void *_stats_before)
{
	struct statvfs stat_struct;
	char output[256];
	struct usage_stats usage_stats;

	if (statvfs(cfg->device, &stat_struct)) {
		user_warn("Unable to statvfs %s: %s\n", cfg->device, strerror(errno));
		return;
	}
	usage_stats.total = (float)stat_struct.f_blocks * (float)stat_struct.f_frsize;
	usage_stats.free = (float)stat_struct.f_bavail * (float)stat_struct.f_bsize;

	format_usage_stats(output, sizeof(output), cfg->format, &usage_stats);
	xosd_set_colour(osd, color_for_level(USED_PERCENTAGE(usage_stats), cfg));
	xosd_display(osd, 1, XOSD_string, output);
}

/* monitor disk activity */

void
monitor_type_diskact_retrieve_stats(void *_io_stats, const struct cfg *cfg)
{
	struct io_stats *io_stats = _io_stats;
	float in, out;

	read_columns_from_file("/proc/diskstats", cfg->device, 2, 5, &in, 9, &out);
	io_stats->in = in * SECTOR_SIZE;
	io_stats->out = out * SECTOR_SIZE;
}

/* monitor net */

void
monitor_type_net_retrieve_stats(void *_io_stats, const struct cfg *cfg)
{
	struct io_stats *io_stats = _io_stats;
	float in, out;

	read_columns_from_file("/proc/net/dev", cfg->device, 2, 1, &in, 9, &out);
	io_stats->in = in;
	io_stats->out = out;
}


/* Monitor specs */

static struct monitor monitors[] = {
	{
	name:  "clock",
	description: "Simple clock, strftime(3) format",
	default_device: NULL,
	default_format: "%a %b %e %H:%M:%S %G",
	create_stats_data:  NULL,
	retrieve_stats: NULL,
	render: monitor_type_clock_render,
	},
	{
	name:  "cpu",
	description:  "Cpu activity monitor",
	default_device: "cpu0",
	default_format: "CPU: %.0f%%",
	create_stats_data:  monitor_type_cpu_create_stats_data,
	retrieve_stats: monitor_type_cpu_retrieve_stats,
	render: monitor_type_cpu_render,
	},
	{
	name:  "ctxt",
	description:  "Context switches per second monitor",
	default_device: NULL,
	default_format: "ctxt: %i switches/s",
	create_stats_data:  monitor_create_io_stats_data,
	retrieve_stats: monitor_type_ctxt_retrieve_stats,
	render: monitor_type_iospeed_render,
	},
	{
	name:  "runps",
	description:  "Processes in the RUNNING state monitor",
	default_device: NULL,
	default_format: "procs: %.0f",
	create_stats_data:  NULL,
	retrieve_stats: NULL,
	render: monitor_type_runps_render,
	},
	{
	name:  "mem",
	description:  "Used memory monitor",
	default_device: NULL,
	default_format: "Mem: %U%%, %uB/%tB",
	create_stats_data:  NULL,
	retrieve_stats: NULL,
	render: monitor_type_memory_render,
	},
	{
	name:  "swap",
	description:  "Swap usage monitor",
	default_device: NULL,
	default_format: "Swap: %U%%, %uB/%tB",
	create_stats_data:  NULL,
	retrieve_stats: NULL,
	render: monitor_type_swap_render,
	},
	{
	name:  "swapact",
	description:  "Swapping activity monitor",
	default_device: NULL,
	default_format: "swapact: %tB (%iB in/%oB out)",
	create_stats_data:  monitor_create_io_stats_data,
	retrieve_stats: monitor_type_swapact_retrieve_stats,
	render: monitor_type_iospeed_render,
	},
	{
	name:  "disk",
	description:  "Disk usage monitor. Device is some file on the disk I display usage for!",
	default_device: "/",
	default_format: "Disk: %U%%, %uB/%tB",
	create_stats_data:  NULL,
	retrieve_stats: NULL,
	render: monitor_type_disk_render,
	},
	{
	name:  "diskact",
	description:  "Disk activity monitor",
	default_device: "hda",
	default_format: "diskact: %tB (%iB in/%oB out)",
	create_stats_data:  monitor_create_io_stats_data,
	retrieve_stats: monitor_type_diskact_retrieve_stats,
	render: monitor_type_iospeed_render,
	},
	{
	name:  "net",
	description:  "Network activity monitor",
	default_device: "eth0",
	default_format: "eth0: %tB (%iB in/%oB out)",
	create_stats_data:  monitor_create_io_stats_data,
	retrieve_stats: monitor_type_net_retrieve_stats,
	render: monitor_type_iospeed_render,
	},
};

#define N_MONITORS (sizeof(monitors)/sizeof(struct monitor))

/* Argument parsing and configuration ***************************************/

static struct option long_options[] = {
	{"type",     1, NULL, 'T'},
	{"device",   1, NULL, 'D'},
	{"format",   1, NULL, 'F'},

	{"font",     1, NULL, 'f'},
	{"color",    1, NULL, 'c'},
	{"shadow",   1, NULL, 's'},
	{"outline-width", 1, NULL, 'O'},
	{"outline-color", 1, NULL, 'C'},
	{"level-colors", 1, NULL, 'L'},

	{"top",      0, NULL, 't'},
	{"vcenter",  0, NULL, 'v'},
	{"bottom",   0, NULL, 'b'},
	{"right",    0, NULL, 'r'},
	{"middle",   0, NULL, 'm'},
	{"left",     0, NULL, 'l'},
	{"offset",   1, NULL, 'o'},
	{"hoffset",  1, NULL, 'H'},

	{"interval", 1, NULL, 'i'},

	{"help",     0, NULL, 'h'},
	{NULL,       0, NULL, 0}
};
#define N_LONG_OPTIONS (sizeof(long_options)/sizeof(struct option))

static struct option_description
{
	const char *option; 
	const char *description;
} option_descriptions[] = {
	{"type", "type of the monitor: clock(default), cpu, ..."},
	{"device", "device (monitor type dependent). Must come after monitor type!"},
	{"format", "format string (monitor dependent). Must come after monitor type!"},

	{"font", "fully qualified font."},
	{"color", "color. (default: green)"},
	{"shadow", "drop shadow offset. (default: 0)"},
	{"outline-width", "outline width. (default: 0)"},
	{"outline-color", "outline color. (default: black)"},
	{"level-colors", "list of value:color pairs, for monitors where color can change with value"},

	{"top", "locate at top of screen (default: bottom)"},
	{"vcenter", "locate in vertical center of screen (default: bottom)"},
	{"bottom", "locate at bottom of screen (default)"},
	{"right", "locate at right of screen (default: left)"},
	{"middle", "locate at middle of screen (default: left)"},
	{"left", "locate at left of screen (default)"},
	{"offset", "vertical offset in pixels. (default: 0)"},
	{"hoffset", "horizontal offset in pixels. (default: 0)"},

	{"interval", "interval (time between updates) in seconds"},
	{"help", "this help message"},
	{NULL, NULL}
};

void print_usage(const char *progname)
{
	struct option *o;
	struct option_description *od;
	const char *description;
	struct monitor *m;
	int i;

	printf("USAGE: %s [-flag args]\n", progname);
	for (o = long_options; o->name; o++) {
		printf("\t-%c, --%s\t", o->val, o->name);
		description = "DESCRIPTION MISSING!";
		for (od = option_descriptions; od->option; od++)
			if (!strcmp(od->option, o->name)) {
				description = od->description;
				break;
			}
		printf("%s\n", description);
	}
	printf("Available monitors (for the --type option):\n");
	for (i = 0; i < N_MONITORS; i++) {
		m = monitors + i;
		printf("\t%s\t%s.", m->name, m->description);
		if (m->default_device)
			printf(" Default device=\"%s\".", m->default_device);
		else 
			printf(" Device not used.");
		if (m->default_format)
			printf(" Default format=\"%s\".", m->default_format);
		else 
			printf(" Format not used.");
		printf("\n");
	}
}

void
parse_level_colors(const char *_level_colors_string, struct cfg *cfg)
{
	char *level_colors_string = strdup(_level_colors_string);
	char *saveptr = NULL, *token;

	token = strtok_r(level_colors_string, " :,;", &saveptr);
	while(1) {
		if (cfg->n_level_colors == MAX_LEVEL_COLORS)
			break;
		if (!token)
			break;
		cfg->level_colors[cfg->n_level_colors].level = atof(token);
		token = strtok_r(NULL, " :,;", &saveptr);
		if (!token)
			break;
		cfg->level_colors[cfg->n_level_colors].color = strdup(token);
		cfg->n_level_colors++;
		token = strtok_r(NULL, " :,;", &saveptr);
	}
	free(level_colors_string);
}


void 
parse_options(int argc, char *argv[], struct cfg *cfg)
{
	char shortops[2 * N_LONG_OPTIONS];
	struct option *o;
	int i;
	char c;

	cfg->monitor = monitors;
	cfg->format = monitors[0].default_format;
	cfg->device = monitors[0].default_device;

	cfg->font = "";
	cfg->color = "green";
	cfg->outline_color = "black";
	cfg->outline_width = 1;
	cfg->hoffset = 0;
	cfg->voffset = 0;
	cfg->shadow = 0;
	cfg->interval = 1;
	cfg->n_level_colors = 0;
	cfg->vpos = XOSD_bottom;
	cfg->hpos = XOSD_left;

	/* build short options for getopt_long */
	i = 0;
	for (o = long_options; o->name; o++) {
		shortops[i++] = o->val;
		if (o->has_arg)
			shortops[i++] = ':';
	}
	shortops[i] = '\0';

	while ((c = getopt_long(argc ,argv, shortops, long_options, NULL)) != -1)
	{
		switch(c)
		{
			case 'T': for (i = 0; i < N_MONITORS; i++) {
						  if (!strcasecmp(optarg, monitors[i].name)) {
								  cfg->monitor = monitors + i;
								  cfg->format = monitors[i].default_format;
								  cfg->device = monitors[i].default_device;
								  break;
							  }
						  }
					  if (i == N_MONITORS)
						  user_warn("unknown monitor type: %s\n", optarg);
					  break;
			case 'D': cfg->device = optarg;
			case 'f': cfg->font = optarg; break;
			case 'F': cfg->format = optarg; break;
			case 'c': cfg->color = optarg; break;
			case 'i': cfg->interval = atoi(optarg); break;
			case 's': cfg->shadow = atoi(optarg); break;
			case 'o': cfg->voffset = atoi(optarg); break;
			case 'H': cfg->hoffset = atoi(optarg); break;
			case 'r': cfg->hpos = XOSD_right; break;
			case 'm': cfg->hpos = XOSD_center; break;
			case 'l': cfg->hpos = XOSD_left; break;
			case 't': cfg->vpos = XOSD_top; break;
			case 'v': cfg->vpos = XOSD_middle; break;
			case 'b': cfg->vpos = XOSD_bottom; break;
			case 'O': cfg->outline_width = atoi(optarg); break;
			case 'C': cfg->outline_color = optarg; break;
			case 'L': parse_level_colors(optarg, cfg); break;
			case 'h': print_usage(argv[0]); exit(EXIT_SUCCESS);
		}
	}
};

/* Main *********************************************************************/

int 
main(int argc, char *argv[])
{
	struct cfg cfg;

	struct timeval t_now, t_before, t_elapsed;
	void *stats_now = NULL, *stats_before = NULL;
	struct monitor *monitor;

	xosd *osd;

	parse_options(argc, argv, &cfg);
	monitor = cfg.monitor;

	if (!(osd = xosd_create (2))) {
		fprintf (stderr, "Error initializing osd\n");
		return EXIT_FAILURE;
	}

	if (cfg.font != NULL && strlen(cfg.font) > 0) 
		xosd_set_font (osd, cfg.font);
	xosd_set_colour (osd, cfg.color);
	xosd_set_shadow_offset (osd, cfg.shadow);
	xosd_set_pos (osd, cfg.vpos);
	xosd_set_align (osd, cfg.hpos);
	xosd_set_vertical_offset(osd, cfg.voffset);
	xosd_set_horizontal_offset(osd, cfg.hoffset);
	xosd_set_outline_offset(osd, cfg.outline_width);
	xosd_set_outline_colour(osd, cfg.outline_color);

	setup_signal_handlers();

	gettimeofday(&t_before, NULL);

	if (monitor->create_stats_data) {
		stats_now = monitor->create_stats_data(&cfg);
		stats_before = monitor->create_stats_data(&cfg);
	}
	if (monitor->retrieve_stats)
		monitor->retrieve_stats(stats_before, &cfg);

	while (1)
	{
		usleep(WAKE_INTERVAL * 1000);
		gettimeofday(&t_now, NULL);
		timeval_subtract(&t_elapsed, &t_now, &t_before);
		if (t_elapsed.tv_sec < cfg.interval && !visibility_changed)
			continue;
		visibility_changed = 0;

		if (monitor->retrieve_stats)
			monitor->retrieve_stats(stats_now, &cfg);

		if (visibility) {
			if (monitor->render)
				monitor->render(osd, &cfg, &t_now, &t_before, stats_now, stats_before);
		} else 
			xosd_display(osd, 1, XOSD_string, "");

		{ void *swap = stats_now; stats_now = stats_before; stats_before = swap; };
		memcpy(&t_before, &t_now, sizeof(struct timeval));
	}

	xosd_destroy (osd);
	if (stats_now)
		free(stats_now);
	if (stats_before)
		free(stats_before);

	return EXIT_SUCCESS;
}
