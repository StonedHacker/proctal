#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <proctal.h>

#include "cmd.h"

static int request_quit = 0;

static void quit(int signum)
{
	request_quit = 1;
}

static int register_signal_handler()
{
	struct sigaction sa = {
		.sa_handler = quit,
		.sa_flags = 0,
	};

	sigemptyset(&sa.sa_mask);

	return sigaction(SIGINT, &sa, NULL) != -1
		&& sigaction(SIGTERM, &sa, NULL) != -1;
}

static void unregister_signal_handler()
{
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
}

static void wait_signal_handler()
{
	sigset_t mask, oldmask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	sigprocmask(SIG_BLOCK, &mask, &oldmask);

	while (!request_quit) {
		sigsuspend(&oldmask);
	}

	sigprocmask(SIG_UNBLOCK, &mask, NULL);
}

static void wait_input_or_signal_handler()
{
	// The file descriptor of standard input.
	const int ifd = 0;

	int flags = fcntl(ifd, F_GETFL, 0);
	if (fcntl(ifd, F_SETFL, flags | O_NONBLOCK) == -1) {
		// TODO: Handle possible failure.
	}

	fd_set rfds;

	FD_ZERO(&rfds);
	FD_SET(ifd, &rfds);

	sigset_t interesting, original;

	sigemptyset(&interesting);
	sigaddset(&interesting, SIGINT);
	sigaddset(&interesting, SIGTERM);

	char buf[2048];

	sigprocmask(SIG_BLOCK, &interesting, &original);

	// There's a chance a signal was delivered before the block was set,
	// hence why the first statement in the following infinite loop is
	// checking if the signal handler was run.

	for (;;) {
		if (request_quit) {
			break;
		}

		pselect(1, &rfds, NULL, NULL, NULL, &original);

		sigprocmask(SIG_SETMASK, &original, NULL);

		if (request_quit) {
			break;
		}

		for (;;) {
			ssize_t r = read(0, buf, sizeof buf);

			if (request_quit) {
				// No point in reading any further when the
				// signal to quit was delivered.
				break;
			}

			if (r == 0) {
				request_quit = 1;
				break;
			} else if (r == -1) {
				if (errno == EAGAIN) {
					// Would have blocked reading. Let
					// pselect block.
					break;
				} else {
					// This is unexpected though we can
					// probably ignore.
					break;
				}
			}
		}

		sigprocmask(SIG_BLOCK, &interesting, NULL);
	}

	sigprocmask(SIG_SETMASK, &original, NULL);
}

int proctal_cmd_freeze(struct proctal_cmd_freeze_arg *arg)
{
	if (!register_signal_handler()) {
		fprintf(stderr, "Failed to set up signal handler.\n");
		return 1;
	}

	proctal p = proctal_create();

	switch (proctal_error(p)) {
	case 0:
		break;

	case PROCTAL_ERROR_OUT_OF_MEMORY:
		fprintf(stderr, "Out of memory.\n");
		proctal_destroy(p);
		return 1;

	default:
		fprintf(stderr, "Unable to create an instance of Proctal.\n");
		proctal_destroy(p);
		return 1;
	}

	proctal_set_pid(p, arg->pid);

	if (!proctal_freeze(p)) {
		switch (proctal_error(p)) {
		case 0:
			break;

		case PROCTAL_ERROR_PERMISSION_DENIED:
			fprintf(stderr, "Permission denied.\n");
			proctal_destroy(p);
			return 1;

		default:
			fprintf(stderr, "Unable to freeze.\n");
			proctal_destroy(p);
			return 1;
		}
	}

	if (arg->input) {
		wait_input_or_signal_handler();
	} else {
		wait_signal_handler();
	}

	proctal_unfreeze(p);

	proctal_destroy(p);

	switch (proctal_error(p)) {
	case 0:
		break;

	default:
		fprintf(stderr, "Failed to cleanup properly.\n");
		proctal_destroy(p);
		return 1;
	}

	unregister_signal_handler();

	return 0;
}
