#include "external-transaction.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <ccan/tdb/tdb.h>

static volatile sig_atomic_t alarmed;
static void do_alarm(int signum)
{
	alarmed++;
}

static int do_transaction(const char *name)
{
	TDB_DATA k = { .dptr = (void *)"a", .dsize = 1 };
	TDB_DATA d = { .dptr = (void *)"b", .dsize = 1 };
	struct tdb_context *tdb = tdb_open(name, 0, 0, O_RDWR, 0);

	if (!tdb)
		return -1;

	alarmed = 0;
	tdb_setalarm_sigptr(tdb, &alarmed);

	alarm(1);
	if (tdb_transaction_start(tdb) != 0)
		goto maybe_alarmed;

	if (tdb_store(tdb, k, d, 0) != 0) {
		tdb_transaction_cancel(tdb);
		tdb_close(tdb);
		return -2;
	}

	if (tdb_transaction_commit(tdb) == 0) {
		tdb_delete(tdb, k);
		tdb_close(tdb);
		return 1;
	}

	tdb_delete(tdb, k);
maybe_alarmed:
	tdb_close(tdb);
	if (alarmed)
		return 0;
	return -3;
}


/* Do this before doing any tdb stuff.  Return handle, or -1. */
int prepare_external_agent(void)
{
	int pid;
	int command[2], response[2];
	struct sigaction act = { .sa_handler = do_alarm };
	char name[PATH_MAX];

	if (pipe(command) != 0 || pipe(response) != 0)
		return -1;

	pid = fork();
	if (pid < 0)
		return -1;

	if (pid != 0) {
		close(command[0]);
		close(response[1]);
		/* FIXME: Make fds consective. */
		dup2(command[1]+1, response[1]);
		return command[1];
	}

	close(command[1]);
	close(response[0]);
	sigaction(SIGALRM, &act, NULL);

	while (read(command[0], name, sizeof(name)) != 0) {
		int result = do_transaction(name);
		if (write(response[1], &result, sizeof(result))
		    != sizeof(result))
			err(1, "Writing response");
	}
	exit(0);
}

/* Ask the external agent to try to do a transaction. */
bool external_agent_transaction(int handle, const char *tdbname)
{
	int res;

	if (write(handle, tdbname, strlen(tdbname)+1)
	    != strlen(tdbname)+1)
		err(1, "Writing to agent");

	if (read(handle+1, &res, sizeof(res)) != sizeof(res))
		err(1, "Reading from agent");

	if (res > 1)
		errx(1, "Agent returned %u\n", res);

	return res;
}
