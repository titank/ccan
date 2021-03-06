#include <ccan/daemon_with_notify/daemon_with_notify.h>
#include <ccan/daemon_with_notify/daemon_with_notify.c>
#include <ccan/tap/tap.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <string.h>

struct child_data {
	pid_t pid;
	pid_t ppid;
	int in_root_dir;
	int read_from_stdin, write_to_stdout, write_to_stderr;
};

int main(int argc, char *argv[])
{
	int fds[2];
	struct child_data daemonized;
	pid_t pid;

	plan_tests(6);

	if (pipe(fds) != 0)
		err(1, "Failed pipe");

	/* Since daemonize forks and parent exits, we need to fork
	 * that parent. */
	pid = fork();
	if (pid == -1)
		err(1, "Failed fork");

	if (pid == 0) {
		char buffer[2];
		pid = getpid();
		daemonize(0, 0, 1);
		daemon_is_ready();
		/* Keep valgrind happy about uninitialized bytes. */
		memset(&daemonized, 0, sizeof(daemonized));
		daemonized.pid = getpid();
		daemonized.in_root_dir = (getcwd(buffer, 2) != NULL);
		daemonized.read_from_stdin
			= read(STDIN_FILENO, buffer, 1) == -1 ? errno : 0;
		daemonized.write_to_stdout
			= write(STDOUT_FILENO, buffer, 1) == -1 ? errno : 0;
		if (write(STDERR_FILENO, buffer, 1) != 1) {
			daemonized.write_to_stderr = errno;
			if (daemonized.write_to_stderr == 0)
				daemonized.write_to_stderr = -1;
		} else
			daemonized.write_to_stderr = 0;

		/* Make sure parent exits. */
		while (getppid() == pid)
			sleep(1);
		daemonized.ppid = getppid();
		if (write(fds[1], &daemonized, sizeof(daemonized))
		    != sizeof(daemonized))
			exit(1);
		exit(0);
	}

	if (read(fds[0], &daemonized, sizeof(daemonized)) != sizeof(daemonized))
		err(1, "Failed read");

	ok1(daemonized.pid != pid);
	ok1(daemonized.ppid == 1);
	ok1(daemonized.in_root_dir);
	ok1(daemonized.read_from_stdin == 0);
	ok1(daemonized.write_to_stdout == 0);
	ok1(daemonized.write_to_stderr == 0);

	return exit_status();
}
