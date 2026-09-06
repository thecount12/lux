/* 9P2000 server + client tests (no Lux, no display). Usage: ./ninep_test */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <errno.h>

#include "ninep.h"

static char ctlbuf[256];
static int ctllen;

static int
onread(void* aux, char** data, int* len)
{
	const char* name = (const char*)aux;
	const char* s;

	if (strcmp(name, "status") == 0)
		s = "ok\n";
	else
		s = "";
	*len = (int)strlen(s);
	*data = (char*)malloc((size_t)(*len) + 1);
	if (*data == NULL)
		return -1;
	memcpy(*data, s, (size_t)(*len) + 1);
	return 0;
}

static int
onwrite(void* aux, const char* data, int len)
{
	FILE* fp;

	(void)aux;
	if (len > (int)sizeof(ctlbuf) - 1)
		len = (int)sizeof(ctlbuf) - 1;
	memcpy(ctlbuf, data, (size_t)len);
	ctlbuf[len] = '\0';
	ctllen = len;
	fp = fopen("/tmp/lux-ninep-test-ctl", "w");
	if (fp != NULL) {
		fwrite(ctlbuf, 1, (size_t)ctllen, fp);
		fclose(fp);
	}
	return 0;
}

static void
fillfiles(NinePFile* files, NinePOps* ops)
{
	memset(files, 0, sizeof(NinePFile) * 2);
	strcpy(files[0].name, "status");
	files[0].readable = 1;
	files[0].aux = (void*)"status";
	strcpy(files[1].name, "ctl");
	files[1].writable = 1;
	files[1].readable = 0;
	files[1].aux = (void*)"ctl";
	ops->onread = onread;
	ops->onwrite = onwrite;
}

static int
test_client(int fd)
{
	NinePClient* c;
	char* data = NULL;
	int len = 0;
	NinePList list;
	NinePStat st;

	c = ninep_client_attach(fd, "test", "");
	if (c == NULL) {
		fprintf(stderr, "attach failed\n");
		return -1;
	}
	if (ninep_client_ls(c, "/", &list) < 0 || list.count != 2) {
		fprintf(stderr, "ls failed (%s)\n", ninep_client_err(c));
		ninep_client_close(c);
		return -1;
	}
	ninep_list_free(&list);
	if (ninep_client_read(c, "/status", &data, &len) < 0 ||
	    len != 3 || memcmp(data, "ok\n", 3) != 0) {
		fprintf(stderr, "read failed (%s)\n", ninep_client_err(c));
		free(data);
		ninep_client_close(c);
		return -1;
	}
	free(data);
	if (ninep_client_write(c, "/ctl", "hello", 5) < 0) {
		fprintf(stderr, "write failed (%s)\n", ninep_client_err(c));
		ninep_client_close(c);
		return -1;
	}
	if (ninep_client_stat(c, "/status", &st) < 0 || st.isdir ||
	    strcmp(st.name, "status") != 0) {
		fprintf(stderr, "stat failed (%s)\n", ninep_client_err(c));
		ninep_client_close(c);
		return -1;
	}
	if (ninep_client_read(c, "/missing", &data, &len) == 0) {
		fprintf(stderr, "missing file should fail\n");
		free(data);
		ninep_client_close(c);
		return -1;
	}
	ninep_client_close(c);
	{
		FILE* fp = fopen("/tmp/lux-ninep-test-ctl", "r");
		if (fp == NULL || fread(ctlbuf, 1, sizeof(ctlbuf) - 1, fp) < 5) {
			if (fp)
				fclose(fp);
			fprintf(stderr, "write data missing\n");
			return -1;
		}
		fclose(fp);
		ctlbuf[5] = '\0';
		unlink("/tmp/lux-ninep-test-ctl");
		if (strcmp(ctlbuf, "hello") != 0) {
			fprintf(stderr, "ctl mismatch: %s\n", ctlbuf);
			return -1;
		}
	}
	return 0;
}

static int
test_socketpair(void)
{
	int sp[2];
	pid_t pid;
	NinePFile files[2];
	NinePOps ops;

	fillfiles(files, &ops);
	ctllen = 0;
	ctlbuf[0] = '\0';
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0) {
		perror("socketpair");
		return -1;
	}
	pid = fork();
	if (pid < 0) {
		perror("fork");
		return -1;
	}
	if (pid == 0) {
		close(sp[0]);
		ninep_serve(sp[1], files, 2, &ops);
		close(sp[1]);
		_exit(0);
	}
	close(sp[1]);
	if (test_client(sp[0]) < 0) {
		waitpid(pid, NULL, 0);
		return -1;
	}
	waitpid(pid, NULL, 0);
	return 0;
}

static int
test_tcp(void)
{
	NinePListener lis;
	NinePFile files[2];
	NinePOps ops;
	pid_t pid;
	int port;
	int fd;
	int i;
	char addr[64];

	fillfiles(files, &ops);
	ctllen = 0;
	ctlbuf[0] = '\0';
	if (ninep_listen("tcp!127.0.0.1!0", &lis) < 0) {
		fprintf(stderr, "tcp listen failed\n");
		return -1;
	}
	port = ninep_bound_port(&lis);
	if (port <= 0) {
		fprintf(stderr, "bound port failed\n");
		ninep_unlisten(&lis);
		return -1;
	}
	pid = fork();
	if (pid < 0) {
		ninep_unlisten(&lis);
		return -1;
	}
	if (pid == 0) {
		int client = ninep_accept(&lis);
		if (client >= 0) {
			ninep_serve(client, files, 2, &ops);
			close(client);
		}
		ninep_unlisten(&lis);
		_exit(0);
	}
	close(lis.fd);
	lis.fd = -1;
	snprintf(addr, sizeof(addr), "tcp!127.0.0.1!%d", port);
	fd = -1;
	for (i = 0; i < 50; i++) {
		fd = ninep_dial(addr);
		if (fd >= 0)
			break;
		usleep(20000);
	}
	if (fd < 0) {
		fprintf(stderr, "tcp dial failed: %s\n", strerror(errno));
		waitpid(pid, NULL, 0);
		return -1;
	}
	if (test_client(fd) < 0) {
		waitpid(pid, NULL, 0);
		return -1;
	}
	waitpid(pid, NULL, 0);
	return 0;
}

static int
test_unix(void)
{
	NinePListener lis;
	NinePFile files[2];
	NinePOps ops;
	pid_t pid;
	int fd;
	int i;
	const char* path = "/tmp/lux-ninep-test.9p";

	fillfiles(files, &ops);
	ctllen = 0;
	ctlbuf[0] = '\0';
	if (ninep_listen(path, &lis) < 0) {
		fprintf(stderr, "unix listen failed\n");
		return -1;
	}
	pid = fork();
	if (pid < 0) {
		ninep_unlisten(&lis);
		return -1;
	}
	if (pid == 0) {
		int client = ninep_accept(&lis);
		if (client >= 0) {
			ninep_serve(client, files, 2, &ops);
			close(client);
		}
		ninep_unlisten(&lis);
		_exit(0);
	}
	close(lis.fd);
	lis.fd = -1;
	fd = -1;
	for (i = 0; i < 50; i++) {
		fd = ninep_dial(path);
		if (fd >= 0)
			break;
		usleep(20000);
	}
	if (fd < 0) {
		fprintf(stderr, "unix dial failed\n");
		waitpid(pid, NULL, 0);
		return -1;
	}
	if (test_client(fd) < 0) {
		waitpid(pid, NULL, 0);
		return -1;
	}
	waitpid(pid, NULL, 0);
	return 0;
}

int
main(void)
{
	if (test_socketpair() < 0) {
		fprintf(stderr, "socketpair test failed\n");
		return 1;
	}
	if (test_unix() < 0) {
		fprintf(stderr, "unix test failed\n");
		return 1;
	}
	if (test_tcp() < 0) {
		fprintf(stderr, "tcp test failed\n");
		return 1;
	}
	printf("ninep_test ok\n");
	return 0;
}
