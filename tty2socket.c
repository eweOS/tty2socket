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

/*	POSIX headers		*/
#include<unistd.h>
#include<sys/socket.h>
#include<sys/un.h>

#define CONF_BACKLOG		128

static int gSocket,gStop,gLogFile,gLogLevel;

#define perr(s) fputs(s,stderr)

static void usage(const char *self)
{
	fprintf(stderr,"%s: Usage\n\t%s ",self,self);
	fputs(
"<SOCKET_PATH> <Program>",stderr);
}

static void log_init(const char *path)
{
	gLogFile = open(path,O_WRONLY);
	if (gLogFile < 0) {
		fprintf(stderr,"Cannot open log file %s\n",path);
		exit(-1);
	}
	return 0;
}

#define LOG_ERROR	0
#define LOG_WARN	1
#define LOG_INFO	2
static void log(int level,const char *fmt,...)
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
	write(logFile,levelInfo[level],strlen(levelInfo[level]));
	write(logFile,s,length - 1);

	free(s);
	return;
}

int main(int argc,const char *argv[])
{
	const char *argPath = NULL,*argProc;
	int step = 0;
	for (int i = 1;i < argc;i++) {
		if (strcmp(argv[i],"-l")) {
			log_init(argv[i + 1]);
			i++;
		} else if (strcmp(argv[i],"-v")) {
			logLevel = LOG_WARN;
		} else if (strcmp(argv[i],"-V")) {
			logLevel = LOG_INFO;
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
	if (!gLogFile)
		log_init("/dev/null");

	gSocket = socket(AF_UNIX,SOCK_STREAM,0);
	if (gSocket < 0) {
		perr("Cannot create a UNIX domain socket");
		return -1;
	}

	struct sockaddr_un addr;
	strcpy(addr.sun_path,argPath);		// FIXME: Check for its length
	addr.sun_family = AF_UNIX;
	if (bind(gSocket,&addr,sizeof(addr)) < 0) {
		fprintf(stderr,"Cannot bind the socket on %s",argPath);
		return -1;
	}
	if (listen(gSocket,CONF_BACKLOG) < 0) {
		perr("Cannot listen on the socket");
		return -1;
	}

	while (!gStop) {
		int conn = accept(gSocket,NULL,NULL);
		if (conn < 0) {
			if (errno == EINTR)
				continue;
			log(LOG_ERROR,"Accept on the socket");
			abort();
		}
	}

	return 0;
}
