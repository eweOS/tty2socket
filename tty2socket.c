/*
 *	tty2socket
 *	File: tty2socket.c
 *	Date: 2022.09.20
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

/*	Standard C99 headers	*/
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdarg.h>

/*	POSIX headers		*/
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

static int gSocket,gStop,gLogFile,gLogLevel;

#define perr(s) fputs(s,stderr)

static void usage(const char *self)
{
	fprintf(stderr,"%s: Usage\n\t%s ",self,self);
	fputs(
"<SOCKET_PATH> <Program>\n",stderr);
}

static void log_init(const char *path)
{
	gLogFile = open(path,O_WRONLY);
	if (gLogFile < 0) {
		fprintf(stderr,"Cannot open log file %s\n",path);
		exit(-1);
	}
	return;
}

#define LOG_ERROR	0
#define LOG_WARN	1
#define LOG_INFO	2

static void log_write(int level,const char *fmt,...)
{
	if (level > gLogLevel)
		return;

	va_list arg;
	va_start(arg,fmt);
	char tmp[1];
	size_t length = vsnprintf(tmp,1,fmt,arg) + 1;

	char *s = malloc(sizeof(char) * length);
	if (!s)
		return;
	vsprintf(s,fmt,arg);
	va_end(arg);

	static const char *levelInfo[] ={"[ERROR]: ","[WARN]: ","[INFO]: "};
	write(gLogFile,levelInfo[level],strlen(levelInfo[level]));
	write(gLogFile,s,length - 1);

	free(s);
	return;
}

static void sig_child(int sig)
{
	(void)sig;
	pid_t pid = wait(NULL);
	log_write(LOG_INFO,"Childprocess %d exit\n",pid);
	return;
}

static void replace_self(const char *file,int conn)
{
	dup2(conn,STDIN_FILENO);
	dup2(conn,STDOUT_FILENO);
	dup2(gLogFile,STDERR_FILENO);
	execlp(file,"tty2socket",NULL);
	log_write(LOG_ERROR,"execlp()");
	return;
}

static void spawn_process(const char *file,int conn)
{
	pid_t pid = fork();
	if (!pid) {
		return;
	} else if (pid > 0) {
		log_write(LOG_INFO,"New child: pid %d,fd %d",pid,conn);
		replace_self(file,conn);
	} else if (pid < 0) {
		log_write(LOG_ERROR,"Error when forking a new process");
	}
	return;
}

int main(int argc,const char *argv[])
{
	const char *argPath = NULL,*argProc;
	int step = 0;
	for (int i = 1;i < argc;i++) {
		if (!strcmp(argv[i],"-l")) {
			log_init(argv[i + 1]);
			i++;
		} else if (!strcmp(argv[i],"-v")) {
			gLogLevel = LOG_WARN;
		} else if (!strcmp(argv[i],"-V")) {
			gLogLevel = LOG_INFO;
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

	if (!gLogFile)
		gLogFile = STDOUT_FILENO;

	gSocket = socket(AF_UNIX,SOCK_STREAM,0);
	if (gSocket < 0) {
		perr("Cannot create a UNIX domain socket");
		return -1;
	}

	struct sockaddr_un addr;
	strcpy(addr.sun_path,argPath);		// FIXME: Check for its length
	addr.sun_family = AF_UNIX;
	if (bind(gSocket,(struct sockaddr*)&addr,sizeof(addr)) < 0) {
		fprintf(stderr,"Cannot bind the socket on %s",argPath);
		return -1;
	}
	if (listen(gSocket,CONF_BACKLOG) < 0) {
		perr("Cannot listen on the socket");
		return -1;
	}

	struct sigaction tmpAction = { .sa_handler = sig_child };
	sigaction(SIGCHLD,&tmpAction,NULL);

	while (!gStop) {
		int conn = accept(gSocket,NULL,NULL);
		if (conn < 0) {
			if (errno == EINTR)
				continue;
			log_write(LOG_ERROR,"Accept on the socket");
		}

		spawn_process(argProc,conn);
		close(conn);
	}

	unlink(argPath);

	return 0;
}
