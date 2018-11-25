/*
 * ps4client host tool for PS4 providing host fileio system 
 * Copyright (C) 2003,2015 Antonio Jose Ramos Marquez (aka bigboss) @psxdev on twitter
 * Repository https://github.com/psxdev/ps4client
 * based on psp2client,ps2vfs, ps2client, ps2link, ps2http tools. 
 * Credits goes for all people involved in ps2dev project https://github.com/ps2dev
 * This file is subject to the terms and conditions of the ps4client License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <pthread.h>
#include <errno.h>
#include "debugnet.h"

#ifndef _WIN32
#include <netinet/in.h>
#include <arpa/inet.h>

#else
#include <windows.h>
#define sleep(x) Sleep(x * 1000)
#endif

#include "network.h"
#include "ps4link.h"
#include "utility.h"


extern int errno ;

pthread_t console_thread_id;
pthread_t request_thread_id;

//debugnet udp log
int console_socket;
//ps4link tcp fio service
int request_socket;
//ps4link udp command service
int command_socket=-1; 

int ps4sh_socket = -1; 


int ps4link_counter = 0;

// ps4link_dd is now an array of structs
struct {
	char *pathname; // remember to free when closing dir
	DIR *dir;
} ps4link_dd[10] = {
  { NULL, NULL }, { NULL, NULL }, { NULL, NULL }, { NULL, NULL }, { NULL, NULL },
  { NULL, NULL }, { NULL, NULL }, { NULL, NULL }, { NULL, NULL }, { NULL, NULL }
};

////////////////////////
// PS4LINK FUNCTIONS //
////////////////////////

int ps4link_connect(char *hostname) 
{

	
	// Listen datagram socket port for debugnet console.
	console_socket = network_listen(0x4712, SOCK_DGRAM);

	// Create the console thread.
	if (console_socket > 0) 
	{ 
		pthread_create(&console_thread_id, NULL, ps4link_thread_console, (void *)&console_thread_id); 
	}

	// Connect to the request port.
	request_socket = network_connect(hostname, 0x4711, SOCK_STREAM);
	// request_socket = network_listen(0x4711, SOCK_STREAM);

	// Create the request thread.
	while(request_socket<0)
	{
		request_socket = network_connect(hostname, 0x4711, SOCK_STREAM);
		sleep(1);
		debugNetPrintf(DEBUG,"waiting ps4...\n");
	 	
	}
	if (request_socket > 0) 
	{ 
		pthread_create(&request_thread_id, NULL, ps4link_thread_request, (void *)&request_thread_id); 
	}
  	
	// Connect to the command port future use to send commands to psp2
	//command_socket = network_connect(hostname, 0x4712, SOCK_DGRAM);

	// Delay for a moment to let ps2link finish setup.
#ifdef _WIN32
	Sleep(1);
#else
	sleep(1);
#endif

	// End function.
	return 0;

}

int ps4link_mainloop(int timeout) 
{

	// Disconnect from the command port.
	//if (network_disconnect(command_socket) < 0) 
	//{ 
	//	return -1; 
	//}

	// If no timeout was given, timeout immediately.
	if (timeout == 0) 
	{ 
		return 0; 
	}

	// If timeout was never, loop forever.
	if (timeout < 0) 
	{ 
		for (;;) 
		{ 
			sleep(600); 
		} 
	}

	// Increment the timeout counter until timeout is reached.
	while (ps4link_counter++ < timeout) 
	{ 
		sleep(1); 
	};

	// End function.
	return 0;
}

int ps4link_disconnect(void) 
{

	// Disconnect from the command port.
	//if (network_disconnect(command_socket) < 0) { return -1; }

	// Disconnect from the request port.
	if (network_disconnect(request_socket) < 0) 
	{ 
		return -1; 
	}

	// Disconnect from console port.
	if (network_disconnect(console_socket) < 0) 
	{ 
		return -1; 
	}

	// End function.
	return 0;

}

////////////////////////////////
//   PS4LINK PS4SH FUNCTIONS  //
////////////////////////////////
int ps4link_log_listener(char *src_ip, int port) 
{
	int fd;
	int server_addr_size = sizeof(struct sockaddr_in);
	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = inet_addr(src_ip);
	memset(&(server_addr.sin_zero), '\0', 8);
	if((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
	{
		perror("UDP log socket"); 
	}
	if((bind(fd, (struct sockaddr *)&server_addr, server_addr_size)) == -1) 
	{
		perror("UDP log bind");
	}
	return fd;
}


int ps4link_fio_listener(char *dst_ip, int port, int timeout) 
{
	int addr_size = sizeof(struct sockaddr_in);
	int  flags=0;
	int ret, error;
	unsigned int len;
	struct sockaddr_in addr;
	struct timeval time;
	fd_set rset, wset;
	int fd;
	int yes = 1;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(dst_ip);
	memset(&(addr.sin_zero), '\0', 8);

	if((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ) 
	{
		perror("socket");
		return -1;
	}
	if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) 
	{
		perror("setsockopt");
	}
	error = 0;
	if(fcntl(fd, F_SETFL, flags|O_NONBLOCK) < 0) 
	{
		perror("fcntl");
	}
	if((ret = connect(fd, (struct sockaddr *)&addr, addr_size)) < 0) 
	{
		if (errno != EINPROGRESS) 
		{
			return(-1);
		}
	}
	if(ret == 0) 
	{
		goto done;
	}
	FD_ZERO(&rset);
	FD_SET(fd, &rset);
	wset = rset;

	time.tv_usec = 0;
	time.tv_sec = timeout;
	if((ret = select(fd+1, &rset, &wset, 0, &time)) == 0) 
	{
		close(fd);
		errno = ETIMEDOUT;
		return(-1);
	}

	len = sizeof(error);

	if(FD_ISSET(fd, &rset) || FD_ISSET(fd, &wset)) 
	{
		if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) 
		{
			return(-1);
		}
	}
	else 
	{
		perror("");
	}

done:
	fcntl(fd, F_SETFL, flags);
	if(error) 
	{
		close(fd);
		errno = error;
		return(-1);
	}
	return fd;
}

int ps4link_srv_setup(char *src_ip, int port) 
{
	int flags=0;
	int ret, fd;
	struct sockaddr_in addr;
	int backlog = 10;
	int yes = 1;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(src_ip);
	memset(&(addr.sin_zero), '\0', 8);

	if((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ) {
		perror("socket");
		return -1;
	}
	if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
		perror("setsockopt");
		return -1;
	}
	if(fcntl(fd, F_SETFL, flags|O_NONBLOCK) < 0) {
		perror("fcntl");
		return -1;
	}
	if((bind(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in))) == -1){
		perror("bind");
		return -1;
	}
	if((ret = listen(fd, backlog)) != 0) {
		perror("listen");
		return -1;
	}
	return fd;
}

//static int DEBUG;

void ps4link_set_debug(int level) {
   // DEBUG = level;
    return;
}

int ps4link_debug(void) {
    return DEBUG;
}

void ps4link_set_root(char *p) {
    /* debugNetPrintf(DEBUG,"new root = %s\n", p); */
    /* strcpy(rootdir, p); */
    return;
}

int ps4link_set_path(char *p)
 {
  
    return 0;
}

////////////////////////////////
// PS4LINK COMMAND FUNCTIONS //
////////////////////////////////

int ps4link_command_execuser(int argc,char *argv,int argvlen)
{
    struct { unsigned int number; unsigned short length; int argc; char argv[256]; } PACKED command;
	int ret;
    // Build the command packet.
    command.number = htonl(PS4LINK_EXECUSER_CMD);
    command.length = htons(sizeof(command));
    command.argc   = htonl(argc);
    memcpy(command.argv, argv, argvlen);
    command.argv[argvlen] = '\0';
    // Send the command packet.
    ret=network_send(command_socket, &command, sizeof(command));
	return ret;
	
} 
int ps4link_command_execkernel(int argc,char *argv,int argvlen)
{
    struct { unsigned int number; unsigned short length; int argc; char argv[256]; } PACKED command;
	int ret;
    // Build the command packet.
    command.number = htonl(PS4LINK_EXECKERNEL_CMD);
    command.length = htons(sizeof(command));
    command.argc   = htonl(argc);
    memcpy(command.argv, argv, argvlen);
    command.argv[argvlen] = '\0';
    // Send the command packet.
    ret=network_send(command_socket, &command, sizeof(command));
	return ret;
	
} 
int ps4link_command_execdecrypt(int argc,char *argv,int argvlen)
{
    struct { unsigned int number; unsigned short length; int argc; char argv[256]; } PACKED command;
	int ret;
    // Build the command packet.
    command.number = htonl(PS4LINK_EXECDECRYPT_CMD);
    command.length = htons(sizeof(command));
    command.argc   = htonl(argc);
    memcpy(command.argv, argv, argvlen);
    command.argv[argvlen] = '\0';
    // Send the command packet.
    ret=network_send(command_socket, &command, sizeof(command));
	return ret;
	
} 
int ps4link_command_exit(int argc,char *argv,int argvlen)
{
    struct { unsigned int number; unsigned short length; int argc; char argv[256]; } PACKED command;
	int ret;
    // Build the command packet.
    command.number = htonl(PS4LINK_EXIT_CMD);
    command.length = htons(sizeof(command));
    command.argc   = htonl(argc);
    memcpy(command.argv, argv, argvlen);
    command.argv[argvlen] = '\0';
    // Send the command packet.
    ret=network_send(command_socket, &command, sizeof(command));
	return ret;
	
} 
int ps4link_command_execwhoami(int argc,char *argv,int argvlen)
{
    struct { unsigned int number; unsigned short length; int argc; char argv[256]; } PACKED command;
	int ret;
    // Build the command packet.
    command.number = htonl(PS4LINK_EXECWHOAMI_CMD);
    command.length = htons(sizeof(command));
    command.argc   = htonl(argc);
    memcpy(command.argv, argv, argvlen);
    command.argv[argvlen] = '\0';
    // Send the command packet.
    ret=network_send(command_socket, &command, sizeof(command));
	return ret;
	
} 
int ps4link_command_execshowdir(int argc,char *argv,int argvlen)
{
    struct { unsigned int number; unsigned short length; int argc; char argv[256]; } PACKED command;
	int ret;
    // Build the command packet.
    command.number = htonl(PS4LINK_EXECSHOWDIR_CMD);
    command.length = htons(sizeof(command));
    command.argc   = htonl(argc);
    memcpy(command.argv, argv, argvlen);
    command.argv[argvlen] = '\0';
    // Send the command packet.
    ret=network_send(command_socket, &command, sizeof(command));
	return ret;
	
} 
int ps4link_command_execptrace(int argc,char *argv,int argvlen)
{
    struct { unsigned int number; unsigned short length; int argc; char argv[256]; } PACKED command;
	int ret;
    // Build the command packet.
    command.number = htonl(PS4LINK_EXECPTRACE_CMD);
    command.length = htons(sizeof(command));
    command.argc   = htonl(argc);
    memcpy(command.argv, argv, argvlen);
    command.argv[argvlen] = '\0';
    // Send the command packet.
    ret=network_send(command_socket, &command, sizeof(command));
	return ret;
	
} 

//int ps4link_command_sample(void) 
//{
//	struct { unsigned int number; unsigned short length; } PACKED command;

	// Build the command packet.
//	command.number = htonl(PS4LINK_COMMAND_OPERATION);
//	command.length = htons(sizeof(command));

	// Send the command packet.
//	return network_send(command_socket, &command, sizeof(command));

//}

////////////////////////////////
// PS4LINK REQUEST FUNCTIONS //
////////////////////////////////

int ps4link_request_connect(void *packet) 
{
	struct { unsigned int number; unsigned short length; int flags;} PACKED *request = packet;
	int result = 1;
	debugNetPrintf(DEBUG,"Received connect request \n");
	cli_connect();
	debugNetPrintf(DEBUG,"Connect done \n");
	
	return ps4link_response_connect(result);
	
	
	
}
int ps4link_request_open(void *packet) 
{
	struct { unsigned int number; unsigned short length; int flags; char pathname[256]; } PACKED *request = packet;
	int result = -1;
	struct stat stats;

	// Fix the arguments.
	fix_pathname(request->pathname);
	if(request->pathname[0]==0)
	{
		return ps4link_response_open(-1);
	}
	//printf("antes %x\n",ntohl(request->flags));
	request->flags = fix_flags(ntohl(request->flags));
	//printf("despues %x\n",request->flags);

	debugNetPrintf(DEBUG,"Opening %s flags %x\n",request->pathname,request->flags);
    
	if(((stat(request->pathname, &stats) == 0) && (!S_ISDIR(stats.st_mode))) || (request->flags & O_CREAT))
	{
		// Perform the request.
#if defined (__CYGWIN__) || defined (__MINGW32__)
		result = open(request->pathname, request->flags | O_BINARY, 0644);
#else
		result = open(request->pathname, request->flags, 0644);
#endif
	}

	// Send the response.
	debugNetPrintf(DEBUG,"Open return %d\n",result);
	return ps4link_response_open(result);

}

int ps4link_request_close(void *packet) 
{
	struct { unsigned int number; unsigned short length; int fd; } PACKED *request = packet;
	int result = -1;

	// Perform the request.
	result = close(ntohl(request->fd));

	debugNetPrintf(DEBUG,"close return %d",result);
	// Send the response.
	return ps4link_response_close(result);

}

int ps4link_request_read(void *packet) 
{
	struct { unsigned int number; unsigned short length; int fd; int size; } PACKED *request = packet;
	int result = -1, size = -1; 
	char buffer[65536], *bigbuffer;

	// If a big read is requested...
	if (ntohl(request->size) > sizeof(buffer)) 
	{
		// Allocate the bigbuffer.
		bigbuffer = malloc(ntohl(request->size));

		// Perform the request.
		result = size = read(ntohl(request->fd), bigbuffer, ntohl(request->size));

		// Send the response.
		ps4link_response_read(result, size);

		// Send the response data.
		network_send(request_socket, bigbuffer, size);

		// Free the bigbuffer.
		free(bigbuffer);

		// Else, a normal read is requested...
	} 
	else 
	{

		// Perform the request.
		size = read(ntohl(request->fd), buffer, ntohl(request->size));
		//int error=errno ;
		//debugNetPrintf(DEBUG,"Error reading file: %s %s\n", strerror( error ),buffer);
		result=size;
		debugNetPrintf(DEBUG,"read %d bytes of file descritor %d\n",result,ntohl(request->fd));

		// Send the response.
		ps4link_response_read(result, size);

		// Send the response data.
		network_send(request_socket, buffer, size);

	}

	// End function.
	return 0;

}

int ps4link_request_write(void *packet) 
{
	struct { unsigned int number; unsigned short length; int fd; int size; } PACKED *request = packet;
	int result = -1; 
	char buffer[65536], *bigbuffer;

	// If a big write is requested...
	if (ntohl(request->size) > sizeof(buffer)) 
	{

		// Allocate the bigbuffer.
		bigbuffer = malloc(ntohl(request->size));

		// Read the request data.
		network_receive_all(request_socket, bigbuffer, ntohl(request->size));

		// Perform the request.
		result = write(ntohl(request->fd), bigbuffer, ntohl(request->size));

		// Send the response.
		ps4link_response_write(result);

		// Free the bigbuffer.
		free(bigbuffer);

	} 
	// Else, a normal write is requested...
	else 
	{

		// Read the request data.
		network_receive_all(request_socket, buffer, ntohl(request->size));

		// Perform the request.
		result = write(ntohl(request->fd), buffer, ntohl(request->size));

		// Send the response.
		ps4link_response_write(result);

	}

	// End function.
	return 0;

}

int ps4link_request_lseek(void *packet) 
{
	struct { unsigned int number; unsigned short length; int fd; int offset; int whence; } PACKED *request = packet;
	int result = -1;

	// Perform the request.
	result = lseek(ntohl(request->fd), ntohl(request->offset), ntohl(request->whence));
	debugNetPrintf(DEBUG,"%d result of lseek %d offset %d whence\n",result,ntohl(request->offset), ntohl(request->whence));
	// Send the response.
	return ps4link_response_lseek(result);

}

int ps4link_request_opendir(void *packet) 
{ 
	int loop0 = 0;
	struct { unsigned int command; unsigned short length; int flags; char pathname[256]; } PACKED *request = packet;
	int result = -1;
	struct stat stats;

	// Fix the arguments.
	fix_pathname(request->pathname);
	if(request->pathname[0]==0)
	{
		return ps4link_response_opendir(-1);

	}

	if((stat(request->pathname, &stats) == 0) && (S_ISDIR(stats.st_mode)))
	{
		// Allocate an available directory descriptor.
		for (loop0=0; loop0<10; loop0++) 
		{ 
			if (ps4link_dd[loop0].dir == NULL) 
			{ 
				result = loop0; 
				break; 
			} 
		}

		// Perform the request.
		if (result != -1)
		{
			ps4link_dd[result].pathname = (char *) malloc(strlen(request->pathname) + 1);
			strcpy(ps4link_dd[result].pathname, request->pathname);
			ps4link_dd[result].dir = opendir(request->pathname);
		}
		
	}

	// Send the response.
	return ps4link_response_opendir(result);
}

int ps4link_request_closedir(void *packet) 
{
	struct { unsigned int number; unsigned short length; int dd; } PACKED *request = packet;
	int result = -1;
	if(ntohl(request->dd)>=0)
	{
	// Perform the request.
	result = closedir(ps4link_dd[ntohl(request->dd)].dir);

	if(ps4link_dd[ntohl(request->dd)].pathname)
	{
		free(ps4link_dd[ntohl(request->dd)].pathname);
		ps4link_dd[ntohl(request->dd)].pathname = NULL;
	}

	// Free the directory descriptor.
	ps4link_dd[ntohl(request->dd)].dir = NULL;
	}
	
	// Send the response.
	return ps4link_response_closedir(result);

}
int ps4link_request_readdir(void *packet)
{
	DIR *dir;
	struct { unsigned int number; unsigned short length; int dd; } PACKED *request = packet;
	struct dirent *dirent; 
	struct stat stats; 
	unsigned int mode; 
    unsigned char type;
	unsigned int size;
	unsigned short ctime[8]; 
	unsigned short atime[8];
	unsigned short mtime[8];
	struct tm *loctime;
	char tname[512];
	
	loctime=(struct tm *) malloc(sizeof(struct tm));
	
	
	dir = ps4link_dd[ntohl(request->dd)].dir;

	
	// Perform the request.
	dirent = readdir(dir);
	
	// If no more entries were found...
	if (dirent == NULL) 
	{
		// Tell the user an entry wasn't found.
		return ps4link_response_readdir(0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
	}
	// need to specify the directory as well as file name otherwise uses CWD!
	sprintf(tname, "%s/%s", ps4link_dd[ntohl(request->dd)].pathname, dirent->d_name);


	// Fetch the entry's statistics. Go to stat to get type not all systems has a valid type entry on dirent
	stat(tname, &stats);

	
	// Convert the mode.
	mode = (stats.st_mode& 0xFFF);//0x01FF);//0x07);
	//debugNetPrintf(DEBUG,"mode %x st_mode %04o\n",mode,stats.st_mode);
	if (S_ISDIR(stats.st_mode)) 
	{ 
		mode |= 0x0040000;
		type=DT_DIR;
	}
#ifndef _WIN32
	if (S_ISLNK(stats.st_mode)) 
	{ 
		mode |= 0x0120000; 
		type=DT_LNK;
	}
#endif
	if (S_ISREG(stats.st_mode)) 
	{ 
		mode |= 0x0100000; 
		type=DT_REG;
	}
#ifndef _WIN32
	localtime_r(&(stats.st_ctime), loctime);
	#else
  	loctime=localtime(&(stats.st_ctime));
	#endif
	ctime[6] = (unsigned short)(loctime->tm_year+1900);
	ctime[5] = (unsigned short)(loctime->tm_mon + 1);
	ctime[4] = (unsigned short)loctime->tm_mday;
	ctime[3] = (unsigned short)loctime->tm_hour;
	ctime[2] = (unsigned short)loctime->tm_min;
	ctime[1] = (unsigned short)loctime->tm_sec;
	

	// Convert the access time.
	#ifndef _WIN32
	localtime_r(&(stats.st_atime), loctime);
	#else
	loctime=localtime(&(stats.st_atime));
	#endif
	atime[6] = (unsigned short)(loctime->tm_year+1900);
	atime[5] = (unsigned short)(loctime->tm_mon + 1);
	atime[4] = (unsigned short)loctime->tm_mday;
	atime[3] = (unsigned short)loctime->tm_hour;
	atime[2] = (unsigned short)loctime->tm_min;
	atime[1] = (unsigned short)loctime->tm_sec;

	// Convert the last modified time.
	#ifndef _WIN32
	localtime_r(&(stats.st_mtime), loctime);
	#else
	loctime=localtime(&(stats.st_mtime));
	#endif
	mtime[6] = (unsigned short)(loctime->tm_year+1900);
	mtime[5] = (unsigned short)(loctime->tm_mon + 1);
	mtime[4] = (unsigned short)loctime->tm_mday;
	mtime[3] = (unsigned short)loctime->tm_hour;
	mtime[2] = (unsigned short)loctime->tm_min;
	mtime[1] = (unsigned short)loctime->tm_sec;
	
	free(loctime);
  
	
	
	size = (unsigned int)stats.st_size;
	// Send the response.
	return ps4link_response_readdir(1, dirent->d_namlen, type, mode, size, dirent->d_name, ctime, atime, mtime);
	
	
}
/*int ps4link_request_readdir(void *packet) 
{
	DIR *dir;
		struct { unsigned int number; unsigned short length; int dd; } PACKED *request = packet;
		struct dirent *dirent; 
		struct stat stats; 
		unsigned int mode; 
	    unsigned char type;
		
	
		char tname[512];

		dir = ps4link_dd[ntohl(request->dd)].dir;

		// Perform the request.
		dirent = readdir(dir);

		// If no more entries were found...
		if (dirent == NULL) 
		{

			// Tell the user an entry wasn't found.
			return ps4link_response_readdir(0, 0, NULL);

		}

		// need to specify the directory as well as file name otherwise uses CWD!
		sprintf(tname, "%s/%s", ps4link_dd[ntohl(request->dd)].pathname, dirent->d_name);


		// Fetch the entry's statistics. Go to stat to get type not all systems has a valid type entry on dirent
		stat(tname, &stats);

		
		// Convert the mode.
		mode = (stats.st_mode& 0xFFF);//0x01FF);//0x07);
		//debugNetPrintf(DEBUG,"mode %x st_mode %04o\n",mode,stats.st_mode);
		if (S_ISDIR(stats.st_mode)) 
		{ 
			mode |= 0x0040000;
			type=DT_DIR;
		}
	#ifndef _WIN32
		if (S_ISLNK(stats.st_mode)) 
		{ 
			mode |= 0x0120000; 
			type=DT_LNK;
		}
	#endif
		if (S_ISREG(stats.st_mode)) 
		{ 
			mode |= 0x0100000; 
			type=DT_REG;
			
		}

	
	
		
  
		// Send the response.
		return ps4link_response_readdir(1, type, dirent->d_name);
	
}*/

int ps4link_request_remove(void *packet) 
{
	struct { unsigned int number; unsigned short length; char name[256]; } PACKED *request = packet;
	int result = -1;

	// Fix the arguments.
	fix_pathname(request->name);

	// Perform the request.
	result = remove(request->name);

	// Send the response.
	return ps4link_response_remove(result);
}

int ps4link_request_mkdir(void *packet) 
{
	struct { unsigned int number; unsigned short length; int mode; char name[256]; } PACKED *request = packet;
	int result = -1;

	// Fix the arguments.
	fix_pathname(request->name);
	// request->flags = fix_flags(ntohl(request->flags));

	// Perform the request.
	// do we need to use mode in here: request->mode ?
  
#ifdef _WIN32
	result = mkdir(request->name);  
#else
	result = mkdir(request->name, request->mode);
#endif
  
	// Send the response.
	return ps4link_response_mkdir(result);
}

int ps4link_request_rmdir(void *packet) 
{
	struct { unsigned int number; unsigned short length; char name[256]; } PACKED *request = packet;
	int result = -1;

	// Fix the arguments.
	fix_pathname(request->name);

	// Perform the request.
	result = rmdir(request->name);

	// Send the response.
	return ps4link_response_rmdir(result);
}

/////////////////////////////////
// PS4LINK RESPONSE FUNCTIONS //
/////////////////////////////////

int ps4link_response_connect(int result) 
{
	struct { unsigned int number; unsigned short length; int result; } PACKED response;

	// Build the response packet.
	response.number = htonl(PS4LINK_CONN_RLY);
	response.length = htons(sizeof(response));
	response.result = htonl(result);

	// Send the response packet.
	return network_send(request_socket, &response, sizeof(response));
}

int ps4link_response_open(int result) 
{
	struct { unsigned int number; unsigned short length; int result; } PACKED response;

	// Build the response packet.
	response.number = htonl(PS4LINK_OPEN_RLY);
	response.length = htons(sizeof(response));
	response.result = htonl(result);

	// Send the response packet.
	return network_send(request_socket, &response, sizeof(response));
}


int ps4link_response_close(int result) 
{
	struct { unsigned int number; unsigned short length; int result; } PACKED response;

	// Build the response packet.
	response.number = htonl(PS4LINK_CLOSE_RLY);
	response.length = htons(sizeof(response));
	response.result = htonl(result);

	// Send the response packet.
	return network_send(request_socket, &response, sizeof(response));

}

int ps4link_response_read(int result, int size) 
{
	struct { unsigned int number; unsigned short length; int result; int size; } PACKED response;

	// Build the response packet.
	response.number = htonl(PS4LINK_READ_RLY);
	response.length = htons(sizeof(response));
	response.result = htonl(result);
	response.size   = htonl(size);

	// Send the response packet.
	return network_send(request_socket, &response, sizeof(response));

}

int ps4link_response_write(int result) 
{
	struct { unsigned int number; unsigned short length; int result; } PACKED response;

	// Build the response packet.
	response.number = htonl(PS4LINK_WRITE_RLY);
	response.length = htons(sizeof(response));
	response.result = htonl(result);

	// Send the response packet.
	return network_send(request_socket, &response, sizeof(response));

}

int ps4link_response_lseek(int result) 
{
	struct { unsigned int number; unsigned short length; int result; } PACKED response;

	// Build the response packet.
	response.number = htonl(PS4LINK_LSEEK_RLY);
	response.length = htons(sizeof(response));
	response.result = htonl(result);

	// Send the response packet.
	return network_send(request_socket, &response, sizeof(response));

}

int ps4link_response_opendir(int result) 
{
	struct { unsigned int number; unsigned short length; int result; } PACKED response;

	// Build the response packet.
	response.number = htonl(PS4LINK_OPENDIR_RLY);
	response.length = htons(sizeof(response));
	response.result = htonl(result);

	// Send the response packet.
	return network_send(request_socket, &response, sizeof(response));

}

int ps4link_response_closedir(int result) 
{
	struct { unsigned int number; unsigned short length; int result; } PACKED response;

	// Build the response packet.
	response.number = htonl(PS4LINK_CLOSEDIR_RLY);
	response.length = htons(sizeof(response));
	response.result = htonl(result);

	// Send the response packet.
	return network_send(request_socket, &response, sizeof(response));

}

int ps4link_response_readdir(int result, unsigned char namelen, unsigned char type, int mode, unsigned int size, char *name, unsigned short *ctime, unsigned short *atime, unsigned short *mtime) 
{
	//struct { unsigned int number; unsigned short length; int result; unsigned char type; char name[256]; } PACKED response;

	struct { unsigned int number; unsigned short length; int result; unsigned char type; int mode; unsigned int size;  unsigned short ctime[8]; unsigned short atime[8]; unsigned short mtime[8]; char name[256]; } PACKED response;

	// Build the response packet.
	response.number = htonl(PS4LINK_READDIR_RLY);
	response.length = htons(sizeof(response));
	response.result = htonl(result);
	response.type   = type;
	response.mode	= htonl(mode);
	response.size = htonl(size);
	
	
	if(ctime)
	{
		response.ctime[0] = 0;
		response.ctime[1] = htons(ctime[1]);
		response.ctime[2] = htons(ctime[2]);
		response.ctime[3] = htons(ctime[3]);
		response.ctime[4] = htons(ctime[4]);
		response.ctime[5] = htons(ctime[5]);
		response.ctime[6] = htons(ctime[6]);
	}
	if(atime)
	{
		response.atime[0] = 0;
		response.atime[1] = htons(atime[1]);
		response.atime[2] = htons(atime[2]);
		response.atime[3] = htons(atime[3]);
		response.atime[4] = htons(atime[4]);
		response.atime[5] = htons(atime[5]);
		response.atime[6] = htons(atime[6]);
	}
	if(mtime)
	{
		response.mtime[0] = 0;
		response.mtime[1] = htons(mtime[1]);
		response.mtime[2] = htons(mtime[2]);
		response.mtime[3] = htons(mtime[3]);
		response.mtime[4] = htons(mtime[4]);
		response.mtime[5] = htons(mtime[5]);
		response.mtime[6] = htons(mtime[6]);
	}
	#ifdef _WIN32
	if (name) { sprintf(response.name, "%s", name); }
	#else
	if (name) { snprintf(response.name,namelen+1, "%s", name); }
	#endif
	// Send the response packet.
	int bytes=network_send(request_socket, &response, sizeof(response));
	printf("%s %d\n",response.name,namelen);
	return bytes;
}

int ps4link_response_remove(int result) 
{
	struct { unsigned int number; unsigned short length; int result; } PACKED response;

	// Build the response packet.
	response.number = htonl(PS4LINK_REMOVE_RLY);
	response.length = htons(sizeof(response));
	response.result = htonl(result);

	// Send the response packet.
	return network_send(request_socket, &response, sizeof(response));
}

int ps4link_response_mkdir(int result) 
{
	struct { unsigned int number; unsigned short length; int result; } PACKED response;

	// Build the response packet.
	response.number = htonl(PS4LINK_MKDIR_RLY);
	response.length = htons(sizeof(response));
	response.result = htonl(result);

	// Send the response packet.
	return network_send(request_socket, &response, sizeof(response));
}

int ps4link_response_rmdir(int result) 
{
	struct { unsigned int number; unsigned short length; int result; } PACKED response;

	// Build the response packet.
	response.number = htonl(PS4LINK_RMDIR_RLY);
	response.length = htons(sizeof(response));
	response.result = htonl(result);

	// Send the response packet.
	return network_send(request_socket, &response, sizeof(response));
}

///////////////////////////////
// PS4LINK THREAD FUNCTIONS //
///////////////////////////////

void *ps4link_thread_console(void *thread_id) 
{
	char buffer[1024];

	// If the socket isn't open, this thread isn't needed.
	if (console_socket < 0) { pthread_exit(thread_id); }

	// Loop forever...
	for (;;) 
	{

		// Wait for network activity.
		network_wait(console_socket, -1);

		// Receive the console buffer.
		network_receive(console_socket, buffer, sizeof(buffer));

		// Print out the console buffer.
		printf("%s", buffer);

		// Clear the console buffer.
		memset(buffer, 0, sizeof(buffer));

		// Reset the timeout counter.
		ps4link_counter = 0;

	}

	// End function.
	return NULL;

}

void *ps4link_thread_request(void *thread_id) 
{
	struct { unsigned int number; unsigned short length; char buffer[512]; } PACKED packet;

	// If the socket isn't open, this thread isn't needed.
	if (request_socket < 0) { pthread_exit(thread_id); }
	
	listen(request_socket , 5);
	// Loop forever...
	for (;;) {

		// Wait for network activity.
		network_wait(request_socket, -1);

		// Read in the request packet header.
		network_receive_all(request_socket, &packet, 6);

		// Read in the rest of the packet.
		network_receive_all(request_socket, packet.buffer, ntohs(packet.length) - 6);

		// Perform the requested action.
		switch(ntohl(packet.number))
		{
			case PS4LINK_CONN_CMD:
				ps4link_request_connect(&packet);     
				break;
			case PS4LINK_OPEN_CMD:     
				ps4link_request_open(&packet);     
				break;
			case PS4LINK_CLOSE_CMD:    
				ps4link_request_close(&packet);
				break;    
			case PS4LINK_READ_CMD:    
				ps4link_request_read(&packet);    
				break;
			case PS4LINK_WRITE_CMD:    
				ps4link_request_write(&packet);    
				break;
			case PS4LINK_LSEEK_CMD:    
				ps4link_request_lseek(&packet);   
				break;
			case PS4LINK_OPENDIR_CMD:  
				ps4link_request_opendir(&packet);  
				break;
			case PS4LINK_CLOSEDIR_CMD: 
				ps4link_request_closedir(&packet); 
				break;
			case PS4LINK_READDIR_CMD:  
				ps4link_request_readdir(&packet);  
				break;
			case PS4LINK_REMOVE_CMD:   
				ps4link_request_remove(&packet);
				break;
			case PS4LINK_MKDIR_CMD:    
				ps4link_request_mkdir(&packet);  
				break;   
			case PS4LINK_RMDIR_CMD:    
				ps4link_request_rmdir(&packet);  
				break;
			default:
				debugNetPrintf(DEBUG,"Received unsupported request number\n");
				break;
		}
   	 	
		// Reset the timeout counter.
		ps4link_counter = 0;

	}

	// End function.
	return NULL;

}
