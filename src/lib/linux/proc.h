#ifndef LINUX_PROC_H
#define LINUX_PROC_H

#include <stdio.h>
#include <sys/types.h>

struct proctal_linux_mem_region {
	void *start_addr;
	void *end_addr;

	int read;
	int write;
	int execute;

	// This is not perfect.
	char path[255];
};

const char *proctal_linux_proc_path(pid_t pid, const char *file);

int proctal_linux_read_mem_region(struct proctal_linux_mem_region *region, FILE *maps);

#endif /* LINUX_PROC_H */
