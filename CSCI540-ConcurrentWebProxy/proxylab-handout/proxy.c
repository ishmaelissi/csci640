/*
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *     Andrew Carnegie, ac00@cs.cmu.edu
 *     Harry Q. Bovik, bovik@cs.cmu.edu
 *
 *  Edited by: Akshay Joshi -for [proxy lab]
 *  This programs implements Concurrent web proxy server.
 *   Reference:Computer Systems: A Programmer's Perspective (CSAPP book)
 *
 *
 *
 *
 *
 *
 *
 *
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */

#include "csapp.h"
sem_t lock;
sem_t LogMLock;
ssize_t rc;
//give sigint to exit the process properly
void sigintHandler(int sig_num)
{
	system("clear");
	printf("\nExiting through SIGINT handler\nGood BYE!!!\n");
	exit(1);
}

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
void outputFile(char * log_entry);
//my wrappers for Rio_readn_w, Rio_readlineb_w, and Rio_writen_w
ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n)
{
	if ((rc = rio_readnb(rp, usrbuf, n)) < 0)  printf("\nread failure");
	return rc;
}

ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen)
{
	if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0)  printf("\nRio_readlineb failure");
	return rc;
}

void Rio_writen_w(int fd, void *usrbuf, size_t n)
{
	if (rio_writen(fd, usrbuf, n) != n)  printf("\nRio_writen failure");
	return;
}

//This is my version of csapp.c open_clientfd for thread safe purpose
int open_clientfd_ts(char *hostname, int port)
{
	int clientfd;
	struct hostent *hp;
	struct hostent *PreviousHpPtr;
	struct sockaddr_in serveraddr;
	if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;
	/* Check errno for cause of error */
	P(&lock);
	/* Fill in the server.s IP address and port */
	if ((hp = gethostbyname(hostname)) == NULL)
	{
		printf("\nhostname not found: %s!", hostname);
		V(&lock);
		return -2; /* Check h_errno for cause of error */
	}
	memcpy(&PreviousHpPtr, &hp, sizeof(hp));
	V(&lock);
	bzero((char *) &serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	bcopy((char *)PreviousHpPtr->h_addr_list[0],
			(char *)&serveraddr.sin_addr.s_addr, PreviousHpPtr->h_length);
	serveraddr.sin_port = htons(port);
	/* Establish a connection with the server */
	if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0)
	{
		printf("\nserver can not be connected: %s!", hostname);
		return -1;
	}
	return clientfd;
}






void RequestResolver(int * vargp)
{
	int connfd = *vargp;
	Pthread_detach(pthread_self());
	Free(vargp);
	size_t n;
	rio_t rio;
	int port;
	struct sockaddr_in ip;
	socklen_t addrlen = sizeof(ip);
	if( getpeername(connfd,&ip,&addrlen) == -1)
	{
		printf("\ngetpeername not working");
		return;
	}
	char buffer[MAXLINE], ReqHeader[MAXLINE],target_addr[MAXLINE];
	char url[MAXLINE];
	char log_entry[MAXLINE];
	strcpy(ReqHeader, "");
	rio_readinitb( &rio, connfd);
	/*

	   csapp page 868
	   The rio_readlineb function reads the next text line from file rp (including
	   the terminating newline character), copies it to memory location usrbuf, and terminates
	   the text line with the null (zero) character.

	 */
	while ((n = Rio_readlineb_w( &rio, buffer, MAXLINE)) != 0)
	{
		if((strstr(buffer, " HTTP/1.1") != NULL) || (strstr(buffer, " HTTP/1.0") != NULL))
		{
			if( (strstr(buffer, "GET ") == NULL) )//if buffer does not have GET string in buffer return
			{
				Close(connfd);
				return;
			}
			char* req_type = Malloc((sizeof(char))*MAXLINE);//calling csapp malloc
			char* http_type = Malloc((sizeof(char))*MAXLINE);
			char* path = Malloc((sizeof(int))*MAXLINE);
			if( sscanf(buffer, "%s %s %s", req_type, url, http_type) != 3)
			{
				printf("\nimproper request: %s!", buffer);
				Free(req_type);
				Free(http_type);
				Free(path);
				Close(connfd);
				return;
			}
			parse_uri(url, target_addr, path, &port);

			strcat(req_type, " /");
			strcat(path, " ");
			strcat(http_type, "\n");
			strcat(ReqHeader, req_type);
			strcat(ReqHeader, path);
			strcat(ReqHeader, http_type);
			Free(req_type);
			Free(http_type);
			Free(path);
		}
		else
		{
			strcat(ReqHeader, buffer);
			if( strcmp(buffer, "\r\n") == 0)
			{
				rio_t remote_rio;
				int remote_fd;
				int content_len = -1;
				int chunked = 0;
				int read_len = 0;
				int total_size = 0;

				if( (remote_fd = open_clientfd_ts(target_addr, port) ) < 0)
				{
					if(remote_fd == -2) strcpy(buffer, "ERROR 404:\nHost not found: ");
					else  strcpy(buffer, "connection to server failed: ");
					strcat(buffer, target_addr);
					strcat(buffer, "\n");
					Rio_writen_w(connfd, buffer, strlen(buffer));
					Close(connfd);
					return;
				}
				Rio_readinitb(&remote_rio, remote_fd);
				Rio_writen_w(remote_fd, ReqHeader, strlen(ReqHeader));
				do{
					Rio_readlineb_w(&remote_rio, buffer, MAXLINE);
					Rio_writen_w(connfd, buffer, strlen(buffer));
					sscanf(buffer, "Content-Length: %d", &content_len);
					if( strstr(buffer, "chunked"))  chunked  = 1;
				}while(strcmp(buffer, "\r\n")) ;

				if(chunked)
				{
					while(((read_len = Rio_readlineb_w(&remote_rio, buffer, MAXLINE)) > 0) && strcmp(buffer,"0\r\n"))
					{
						Rio_writen_w(connfd, buffer, read_len);
						total_size += read_len;
					}
				}
				else
				{
					total_size = content_len;
					while (MAXLINE <content_len)
					{
						read_len = Rio_readnb_w(&remote_rio, buffer, MAXLINE);
						Rio_writen_w(connfd, buffer, read_len);
						content_len -= MAXLINE;
					}
					if (0 <content_len)
					{
						read_len = Rio_readnb_w(&remote_rio, buffer, content_len);
						Rio_writen_w( connfd, buffer, content_len );
					}
				}
				Close(remote_fd);
				format_log_entry(log_entry, &ip, url, total_size);
				break; //All done
			}
		}
	}
	P(&LogMLock);
	outputFile(log_entry);
	V(&LogMLock);
	Close(connfd);
}


//Refered from Section 12.3 Concurrent Programming with Threads Page No. 953 CSAPP book
/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
	//refer line 7-11 Page No. 953
	Signal(SIGPIPE, SIG_IGN);
	sem_init(&lock, 0, 1);
	sem_init(&LogMLock, 0, 1);
	int listenfd;
	int* connfdp;
	int port;
	unsigned int  clientlen;
	struct sockaddr_in clientaddr;
	struct hostent * hp;
	char * haddrp;
	pthread_t tid;
	/* Check arguments */
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
		exit(0);
	}
	signal(SIGINT, sigintHandler);//signal handler for SIGINT to quit properly
	//refered from line 17-23 from the book page no 953
	port = atoi(argv[1]);
	listenfd = Open_listenfd(port);
	while (1)//to terminate this loop send SIGINT
	{
		connfdp = Malloc( sizeof(int));
		clientlen = sizeof(clientaddr);
		*connfdp = Accept(listenfd, (SA * ) & clientaddr,  &clientlen);
		if(*connfdp==-1)
		{
			if(errno==EBADF)
			{
				perror("\nBadfile descriptor");
			}
			else
			{
				perror("\nOther error");
			}
		}
		/* Determine the domain name and IP address of the client */
		hp = gethostbyaddr((const char * ) & clientaddr.sin_addr.s_addr,
				sizeof(clientaddr.sin_addr.s_addr), AF_INET);
		haddrp = inet_ntoa(clientaddr.sin_addr);
		printf("server connected to %s (%s)\n", hp->h_name, haddrp);
		Pthread_create(&tid, NULL, (void *)RequestResolver, (void *)connfdp);
	}
	return 0;
}



void outputFile(char * log_entry)
{
	FILE *ofPtr;
	const char *mode = "a";//open for appending data
	const char outputFilename[] = "proxy.log";

	ofPtr = Fopen(outputFilename, mode);//csapp fopen
	if (ofPtr == NULL)
	{
		perror("\nUnable to open file");
		if(errno==EINVAL)	perror("\na routing probably failed");
		exit(1);
	}
	fprintf(ofPtr, "%s\n", log_entry);
	Fclose(ofPtr);//csapp close
}




/*
 * parse_uri - URI parser
 *
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
	char *hostbegin;
	char *hostend;
	char *pathbegin;
	int len;

	if (strncasecmp(uri, "http://", 7) != 0) {
		hostname[0] = '\0';
		return -1;
	}

	/* Extract the host name */
	hostbegin = uri + 7;
	hostend = strpbrk(hostbegin, " :/\r\n\0");
	len = hostend - hostbegin;
	strncpy(hostname, hostbegin, len);
	hostname[len] = '\0';

	/* Extract the port number */
	*port = 80; /* default */
	if (*hostend == ':')
		*port = atoi(hostend + 1);

	/* Extract the path */
	pathbegin = strchr(hostbegin, '/');
	if (pathbegin == NULL) {
		pathname[0] = '\0';
	}
	else {
		pathbegin++;
		strcpy(pathname, pathbegin);
	}

	return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring.
 *
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
		char *uri, int size)
{
	time_t now;
	char time_str[MAXLINE];
	unsigned long host;
	unsigned char a, b, c, d;

	/* Get a formatted time string */
	now = time(NULL);
	strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

	/*
	 * Convert the IP address in network byte order to dotted decimal
	 * form. Note that we could have used inet_ntoa, but chose not to
	 * because inet_ntoa is a Class 3 thread unsafe function that
	 * returns a pointer to a static variable (Ch 13, CS:APP).
	 */
	host = ntohl(sockaddr->sin_addr.s_addr);
	a = host >> 24;
	b = (host >> 16) & 0xff;
	c = (host >> 8) & 0xff;
	d = host & 0xff;


	/* Return the formatted log entry string */
	sprintf(logstring, "%s: %d.%d.%d.%d %s %d", time_str, a, b, c, d, uri,size);
}
