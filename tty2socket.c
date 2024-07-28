/*
 *	tty2socket
 *	File: tty2socket.c
 *	Date: 2022.11.22
 *	By MIT License.
 *	Copyright (C) 2022 Ziyao.
 */

/*
 *	tty2socket,a simple tool to redirect stdin & stdout
 *	towards UNIX socket,works just like CGI
 */

/*
 *	Try to be compatible with s6-ipcserverd
 */

#define _GNU_SOURCE	// for struct ucred, this is NOT POSIX compliant
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdarg.h>

#include<unistd.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<sys/un.h>
#include<errno.h>
#include<signal.h>
#include<sys/wait.h>

#define CONF_BACKLOG		128

static int gSocket, gStop, gLogFile, gLogLevel;
static int gCompatS6;

#define perr(s) fputs(s, stderr)

static void
usage(const char *self)
{
	fprintf(stderr, "%s: Usage\n\t%s ", self, self);
	fputs(
"[options] <SOCKET_PATH> <Program>\n"
"Forward Program's stdin and stdout to a UNIX socket\n"
"Options:\n"
"\t-l filename\tspecify the log file\n"
"\t-v,-V\t\tenable verbose log\n"
"\t-d\t\tdaemonise and change working directory to /\n"
"\t--s6\t\tenable compatible features with s6-ipcserver\n"
"\t-h\t\tprint this help\n", stderr);
}

static void
log_init(const char *path)
{
	gLogFile = open(path, O_WRONLY | O_CREAT,
			S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

	if (gLogFile < 0) {
		fprintf(stderr,"Cannot open log file %s\n",path);
		exit(-1);
	}
}

#define LOG_ERROR	0
#define LOG_WARN	1
#define LOG_INFO	2

static void
log_write(int level, const char *fmt, ...)
{
	if (level > gLogLevel)
		return;

	va_list arg;
	va_start(arg, fmt);
	char tmp[1];
	size_t length = vsnprintf(tmp, 1, fmt, arg) + 1;

	va_end(arg);
	va_start(arg, fmt);
	char *s = malloc(sizeof(char) * length);
	if (!s)
		return;

	vsprintf(s, fmt, arg);
	va_end(arg);
	s[length - 1] = '\n';

	static const char *levelInfo[] = { "[ERROR]: ",
					   "[WARN]: ",
					   "[INFO]: " };

	write(gLogFile, levelInfo[level], strlen(levelInfo[level]));
	write(gLogFile, s, length);

	free(s);
	return;
}

static void
sig_child(int sig)
{
	(void)sig;

	pid_t pid = wait(NULL);
	log_write(LOG_INFO, "Childprocess %d exit", pid);
}

static void
sig_exit(int sig)
{
	(void)sig;

	gStop = 1;
	log_write(LOG_INFO, "Receive signal, exiting");
}

static void
replace_self(const char *file, int conn)
{
	dup2(conn, STDIN_FILENO);
	dup2(conn, STDOUT_FILENO);
	dup2(gLogFile, STDERR_FILENO);
	execlp(file, "tty2socket", NULL);
	log_write(LOG_ERROR, "execlp()");
}

/*
 *	Environment Varibles:
 *		IPCREMOTEEGID, IPCREMOTEEUID, IPCCONNNUM, IPCREMOTEPATH
 */
static void
prepare_env(int conn)
{
	char tmp[64];
	struct ucred cred;
	socklen_t size = sizeof(cred);

	if (getsockopt(conn, SOL_SOCKET, SO_PEERCRED, &cred, &size)) {
		log_write(LOG_ERROR, "get remote peer credentials");
		exit(-1);
	}

	setenv("PROTO", "IPC", 1);
	sprintf(tmp,"%d", cred.uid);
	setenv("IPCREMOTEEUID", tmp, 1);
	sprintf(tmp,"%d", cred.gid);
	setenv("IPCREMOTEEGID", tmp, 1);
	setenv("IPCCONNNUM", "1", 1);
}

static void
spawn_process(const char *file, int conn)
{
	pid_t pid = fork();
	if (!pid) {
		if (gCompatS6)
			prepare_env(conn);
		replace_self(file, conn);
	} else if (pid > 0) {
		log_write(LOG_INFO, "New child: pid %d, fd %d", pid, conn);
	} else if (pid < 0) {
		log_write(LOG_ERROR, "Error when forking a new process");
	}
}

static int
daemonise(void)
{
	pid_t pid = fork();
	if (pid < 0)
		return -1;
	if (pid)
		exit(0);

	if (setsid() < 0)
		return -1;

	pid = fork();
	if (pid < 0)
		return -1;

	if (pid)
		exit(0);

	chdir("/");
	umask(0);

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	return 0;
}

int
main(int argc, const char *argv[])
{
	const char *argPath = NULL, *argProc;
	int step = 0, argDaemon = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-l")) {
			log_init(argv[i + 1]);
			i++;
		} else if (!strcmp(argv[i], "-v")) {
			gLogLevel = LOG_WARN;
		} else if (!strcmp(argv[i], "-V")) {
			gLogLevel = LOG_INFO;
		} else if (!strcmp(argv[i], "-d")) {
			argDaemon = 1;
		} else if (!strcmp(argv[i], "-h")) {
			usage(argv[0]);
			return 0;
		} else if (!strcmp(argv[i], "--s6")) {
			gCompatS6 = 1;
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "%s: Unknown option %s\n",
					argv[0], argv[i]);
			usage(argv[0]);
			return -1;
		} else {
			if (step == 0) {
				argPath = argv[i];
				step++;
			} else if (step == 1) {
				argProc = argv[i];
				step++;
			}
		}
	}

	if (!argPath || !argProc) {
		usage(argv[0]);
		return -1;
	}

	if (argDaemon) {
		if (daemonise()) {
			log_write(LOG_ERROR, "Cannot daemonise");
			return -1;
		}
	}

	if (!gLogFile)
		gLogFile = argDaemon ? open("/dev/null", O_WRONLY) :
				       STDERR_FILENO;

	gSocket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (gSocket < 0) {
		perr("Cannot create a UNIX domain socket\n");
		return -1;
	}

	struct sockaddr_un addr;
	strcpy(addr.sun_path, argPath);		// FIXME: Check for its length
	addr.sun_family = AF_UNIX;

	/*	check whether the socket is in use	*/
	int ret = connect(gSocket, (struct sockaddr *)&addr, sizeof(addr));
	if (ret >= 0 || errno == EAGAIN) {
		perr("Socket is busy, is another daemon already running?\n");
		return -1;
	}

	unlink(argPath);	// make sure the path is not busy

	if (bind(gSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "Cannot bind the socket on %s", argPath);
		return -1;
	}
	if (listen(gSocket, CONF_BACKLOG) < 0) {
		perr("Cannot listen on the socket");
		return -1;
	}

	struct sigaction tmpAction = { .sa_handler = sig_child };
	sigaction(SIGCHLD, &tmpAction, NULL);
	tmpAction.sa_handler = sig_exit;
	sigaction(SIGINT, &tmpAction, NULL);
	sigaction(SIGTERM, &tmpAction, NULL);

	while (!gStop) {
		int conn = accept(gSocket, NULL, NULL);
		if (conn < 0) {
			if (errno == EINTR)
				continue;

			log_write(LOG_ERROR, "Accept on the socket");
		}

		spawn_process(argProc, conn);
		close(conn);
	}

	unlink(argPath);
	close(gLogFile);

	return 0;
}
