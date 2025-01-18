#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>

#define METHOD_AMOUNT 8
#define EXTENSION_AMOUNT 5
#define BUFFER_SIZE 256
#define PORT "8090"
#define BACKLOG 20

void Close(int sockfd);
void ack_Close(int connfd);
void (*Signal(int signo, void (*func)(int) ) )(int);  
void sig_child(int signo);

/* ----------------------- CHILD PROCESS -------------------- */
char *get_file_extension(char *file_name);
char *get_file_name(char *req_file); 
int directory_compare(const char *string, const char *dstring);
void get_current_time(char *formated_return);
off_t get_last_modified(char *file_name, char *formated_return);
int load_headers(char *send_buffer, char *file_name);
int distance_last_occurence(const char *dstring, int c);
void send_all(int connfd, char *send_buffer);
void not_found(int connfd, char *send_buffer);
void bad_request(int connfd, char *send_buffer);

//method lookup table
enum Methods {GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE,};
struct Method_Table
{
	char method_name[8];
	enum Methods method_match;
};
struct Method_Table Met_Lookup[METHOD_AMOUNT] = { {"GET", GET}, {"HEAD", HEAD}, {"POST", POST}, {"PUT", PUT}, {"DELETE", DELETE},
											      {"CONNECT", CONNECT}, {"OPTIONS", OPTIONS}, {"TRACE", TRACE} };
//file extension lookup table
struct Extension_Table
{
	char *file_ext;
	char *ext_type;
};
struct Extension_Table Ext_Lookup[EXTENSION_AMOUNT] = { {".txt", "text/plain"}, {".png", "image/png"}, {".html", "text/html"}, {".htm", "text/html"}, };

/* ---------------------------------------------------------- */
int main(void)
{
	int sockfd;
	int connfd;
	int status = 0;
	pid_t pid;
	
	struct addrinfo hints;
	struct addrinfo *res;
	struct addrinfo *p;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;ch
gai_retry:
	status = getaddrinfo(INADDR_ANY, PORT, &hints, &res);
	if(status < 0)
	{
		if(errno == EINTR)
			goto gai_retry;
		fprintf(stderr, "getaddrinfo : %s", gai_strerror(status));
		exit(1);
	}
	/* Resources: 0005; cycle through the linked list addrinfo for the first match */
	for(p = res; p != NULL; p = p->ai_next)
	{
socket_retry:
		sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if(sockfd == -1)
		{
			if(errno == EINTR)
				goto socket_retry;
			continue;
		}
bind_retry:
		status = bind(sockfd, p->ai_addr, p->ai_addrlen);
		if(status == -1)
		{
			if(errno == EINTR)
				goto bind_retry;
			continue;
		}
		break;
	}
	if(sockfd == -1)
	{
		fprintf(stderr, "sockfd error\n");		
		exit(1);
	}
	if(status == -1)
	{
		fprintf(stderr, "sockfd error\n");		
		exit(1);
	}
listen_retry:
	status = listen(sockfd, BACKLOG);
	if(status == -1)
	{
		if(errno == EINTR)
			goto listen_retry;
		fprintf(stderr, "listen error\n");
		exit(1);
	}
	Signal(SIGCHLD, sig_child);
	/* main loop */
	while(1)
	{
accept_retry:
	connfd = accept(sockfd, NULL, NULL);
	if(connfd == -1)
	{
		perror("accept: ");
		switch(errno) //handling tcp errors
		{
			case EINTR: case ECONNABORTED: case ENETDOWN: case EPROTO: case ENOPROTOOPT:
			case EHOSTDOWN: case ENONET: case EHOSTUNREACH: case EOPNOTSUPP: case ENETUNREACH:
				goto accept_retry; 
		}
		exit(1);
	}
	pid = fork();
	if(pid < 0)
	{
		fprintf(stderr, "fork error\n");
		ack_Close(connfd);
	}
	if(pid == 0)
	{
	Close(sockfd);

	FILE *resourcefp;
	char *file_name;
	char recv_buffer[BUFFER_SIZE]; 
	char send_buffer[BUFFER_SIZE]; 
	char file_buffer[BUFFER_SIZE];
	char recv_method[8];
	char recv_resource[64];
	ssize_t check;

	//recv into buffer	
recv_retry:
	check =	recv(connfd, recv_buffer, sizeof recv_buffer, 0);
	if(check < 0)
	{
		if(errno == EINTR)
			goto recv_retry;
		perror("recv: ");
		ack_Close(connfd);
	}
	//captures method
	int i = 0; 
	for(; recv_buffer[i] != ' ' && i < METHOD_AMOUNT; i++)
		recv_method[i] = recv_buffer[i];
	recv_method[i] = '\0';
	//find method in table
	int y = 0;
	for(; status != 0 && y < METHOD_AMOUNT; y++)
		status = strcasecmp(recv_method, Met_Lookup[y].method_name);
	if(status != 0)
	{
		bad_request(connfd, send_buffer);
	}
	switch(Met_Lookup[y].method_match) 
	{
		case GET	: i = i + 2; //skip passed " \"
					  int j = 0;
					  for(; recv_buffer[i] != ' ' && j < sizeof(recv_buffer); j++, i++)
						  recv_resource[j] = recv_buffer[i];
					  recv_resource[j] = '\0';

					  if( (strcasecmp(recv_resource, "")) == 0) /* serve home-page if empty */
					  {
						  resourcefp = fopen("Webpages/home.html", "r");
						  if(resourcefp == NULL)
						  {
							  fprintf(stderr, "call your default page Webpages/home.html\n");
							  ack_Close(connfd);
						  }
						  load_headers(send_buffer, "home.html"); 
					  }
					  else
					  {
					  file_name = get_file_name(recv_resource); 
					  if(file_name == NULL)
					  {
						  fprintf(stderr, "get_file_name failed\n");
						  not_found(connfd, send_buffer);
					  }
						  load_headers(send_buffer, file_name); //ends

						  char directory_file_name[120] = "Webpages/";
						  int j = 9;
						  int i = 0;
						  for(; i < file_name[i] != '\0'; i++, j++)
							  directory_file_name[j] = file_name[i];
	
						  resourcefp = fopen(directory_file_name, "r");	/* general file serve */
						  if(resourcefp == NULL)
						  {
							  fprintf(stderr, "(fopen) failed to open file\n");
							  ack_Close(connfd);
						  }
					  } //end else
					  fread(file_buffer, sizeof *file_buffer,
										sizeof file_buffer / sizeof *file_buffer, resourcefp);
					  fflush(resourcefp);
					  if( ( ferror(resourcefp) != 0))
					  {
						  fprintf(stderr, "fread error\n"); 
						  ack_Close(connfd);
					  }
					  //combine send_buffer and file_buffer
					  int len1 = 0;
					  int len2 = 0;
					  for(; send_buffer[len1] != '\0'; len1++); //find end for send_buffer
					  for(; file_buffer[len2] != '\0'; len2++)
					  {
						  send_buffer[len1] = file_buffer[len2];
						  len1++;
					  }

					  send_all(connfd, send_buffer);

					  break;
		case HEAD   : printf("head\n\n\n"); 
					  break;
		case PUT	: printf("put\n\n\n");
					  break;
		case POST	: printf("post\n\n\n");
					  break;
		case DELETE : printf("delete\n\n\n");
					  break;
		case CONNECT: printf("connect\n\n\n"); //the request target is the host name and port num seperated by a colon
					  break;
		case OPTIONS: printf("options\n\n\n"); //request target could be a single "*" asterik
					  break;
		case TRACE  : printf("trace\n\n\n");
					  break;
		defualt     : fprintf(stderr, "somehow left loop without a method match and didnt exit"
							  "\n[FIX IMEDIATELY]");
					  bad_request(connfd, send_buffer);
	}//switch
	
	ack_Close(connfd);
	}//fork == 0 
	 
	if(pid > 0)
	{
	Close(connfd);	
	}//fork > 0
	}//while
}//main
 
/* Resources: 0001 */
/* close for on going connection: revieve ACK to prevent connection reset */
void ack_Close(int connfd)
{
		char buffer[200];
		shutdown(connfd, SHUT_WR);  
									
		for(int res = 0; ;)
		{
			res = read(connfd, buffer, 0);
			if(res < 0) 
			{ 
				perror("ending read: ");
				exit(1);
			}
			if(res == 0)
				break;
		}
		Close(connfd);
		exit(0);
}
/* general close wrapper with error handling */
void Close(int sockfd)
{
	int status;
close_retry:
	status = close(sockfd);
	if(status == -1)
	{
		if(errno == EINTR)
			goto close_retry;

		perror("close: ");	
		exit(1);
	}
}
/* loads header fields from otherfunctions into send_buffer; returns the sizeof the headerfield */
int load_headers(char *send_buffer, char *file_name) 
{
	char *header_extension;
	char http_last_mod[32];
	char http_current_time[32];
	size_t size;
	off_t length;

	get_current_time(http_current_time);
	length = get_last_modified(file_name, http_last_mod);	
	header_extension = get_file_extension(file_name); /*content type*/
	if(header_extension == NULL)
		fprintf(stderr, "get_file_extension returned null");

	sprintf(send_buffer, "HTTP/1.1 200 OK\r\n" 
								"Date: %s\r\n"
								"Last-Modified: %s\r\n"
								"Content-Length: %ld\r\n"
								"Content-Type: %s\r\n"
								"\r\n", http_current_time, http_last_mod, length, header_extension);
	size = strlen(send_buffer);
	return size;
}	
/* gets the files last modified date put into http-format and returns the files length */
off_t get_last_modified(char *file_name, char *formated_return)
{
	char month[4];
	char weekday[4];
	//get stat structure for file in directory
	char dir_file_name[64] = "Webpages/";
	int j = 9;
	int i = 0;
	for(; file_name[i] != '\0'; i++, j++)
		dir_file_name[j] = file_name[i];

	struct stat file_stat;
	struct tm tm_time;

	stat(dir_file_name, &file_stat);  
	gmtime_r(&file_stat.st_mtime, &tm_time);

	switch(tm_time.tm_mon)
	{
		case 0: strcpy(month, "jan");
				break;
		case 1: strcpy(month, "feb");
				break;
		case 2: strcpy(month, "mar");
				break;
		case 3: strcpy(month, "apr");
				break;
		case 4: strcpy(month, "may");
				break;
		case 5: strcpy(month, "jun");
				break;
		case 6: strcpy(month, "jul");
				break;
		case 7: strcpy(month, "aug");
				break;
		case 8: strcpy(month, "Sep");
				break;
		case 9: strcpy(month, "Oct");
				break;
		case 10:strcpy(month, "Nov");
				break;
		case 11:strcpy(month, "Dec");
				break;
		default:fprintf(stderr, "gmtime return non month number\n");
	}
	switch(tm_time.tm_wday)
	{
		case 0: strcpy(weekday, "Mon");
		case 1: strcpy(weekday, "Tue");
	    case 2: strcpy(weekday, "Wes");
		case 3: strcpy(weekday, "Thr");
		case 4: strcpy(weekday, "Fri");
		case 5: strcpy(weekday, "Sat");
		case 6: strcpy(weekday, "Sun");
	}
	
	//parse struct into http-format //
	sprintf(formated_return, "%s, %02d %s %4d %02d:%02d:%02d GMT",
			weekday, tm_time.tm_mday, month, tm_time.tm_year + 1900, tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);

	return file_stat.st_size;
}
/* get http-formated gmt time for date field */
void get_current_time(char *formated_return)
{
	char month[4];
	char weekday[4];
	time_t t[64];
	struct tm *tm_time; 
	time(t);
	tm_time = gmtime(t); 
	switch(tm_time->tm_mon)
	{
		case 0: strcpy(month, "Jan");
				break;
		case 1: strcpy(month, "Feb");
				break;
		case 2: strcpy(month, "Mar");
				break;
		case 3: strcpy(month, "Apr");
				break;
		case 4: strcpy(month, "May");
				break;
		case 5: strcpy(month, "Jun");
				break;
		case 6: strcpy(month, "Jul");
				break;
		case 7: strcpy(month, "Aug");
				break;
		case 8: strcpy(month, "Sep");
				break;
		case 9: strcpy(month, "Oct");
				break;
		case 10:strcpy(month, "Nov");
				break;
		case 11:strcpy(month, "Dec");
				break;
		default:fprintf(stderr, "gmtime return non month number\n");
	}
	switch(tm_time->tm_wday)
	{
		case 0: strcpy(weekday, "Mon");
		case 1: strcpy(weekday, "Tue");
	    case 2: strcpy(weekday, "Wes");
		case 3: strcpy(weekday, "Thr");
		case 4: strcpy(weekday, "Fri");
		case 5: strcpy(weekday, "Sat");
		case 6: strcpy(weekday, "Sun");
	}
	sprintf(formated_return, "%s, %02d %s %4d %02d:%02d:%02d GMT",
			weekday, tm_time->tm_mday, month, tm_time->tm_year + 1900, tm_time->tm_hour, tm_time->tm_min, tm_time->tm_sec);
}
/* checks to see if the requested file exists in directory : returns NULL if it doesnt */
char *get_file_name(char *req_file)
{
	DIR *dir = opendir("Webpages/");
	if(dir == NULL)
	{
		perror("opendir :");
		return NULL;
	}	
	struct dirent *instance;
	char *found_file_name = NULL;
	while( (instance = readdir(dir) ) != NULL ) /* readdir increments in the directory */ 
	{
		if( (directory_compare(req_file, instance->d_name)) == 0 )
		{
			found_file_name = instance->d_name;
			break;
		}
	}
	return found_file_name;
}
/* takes the file_req string and the string in the directory that has its extension and limits the compare to before that */
int directory_compare(const char *string, const char *dstring) 
{
	int i = 0;

	while(dstring[i] && string[i])  
	{	
		if(tolower((unsigned char)string[i]) != tolower((unsigned char)dstring[i]))
			return -1;

		i++;
	}	
	if(dstring[i] == '.') 
		return 0;
	else
		return -1;
}
/* gets the file extension and uses the lookup table to return it in http-format */
char *get_file_extension(char *file_name)
{
	char *ext;
	ext = strrchr(file_name,'.'); 
	int check;
	int i;
	//file extension lookup
	for(i = 0; i < EXTENSION_AMOUNT; i++)
	{
		if( (check = strcasecmp(Ext_Lookup[i].file_ext, ext)) == 0)
			break;
	}
	if(check != 0)
		return NULL;

	return Ext_Lookup[i].ext_type;
}
/* Reference 0006: seperated for use in error handling : insures all is sent */
void send_all(int connfd, char *send_buffer)
{
send_again:

	int check = 1;
	size_t bytes_sent = 0;
			 	  
	while(check > 0 && bytes_sent < BUFFER_SIZE)
	{
		check = send(connfd, send_buffer, BUFFER_SIZE, 0);
		bytes_sent += check;
	}
	if(check < 0)
	{
		if(errno == EINTR)
			goto send_again;

		perror("send: ");
		ack_Close(connfd);
	}
}
/* error code functions <not_found> and <bad_request> are currently hard coded */
void not_found(int connfd, char *send_buffer)
{
	FILE *resourcefp = fopen("Webpages/error_pages/notfound.txt", "r");
	if(resourcefp == NULL)
	{
		fprintf(stderr, "error pages hard coded, name: Webpages/error_pages/notfound.txt\n");
	    ack_Close(connfd);
	}
	fread(send_buffer, sizeof(char), BUFFER_SIZE, resourcefp);
	printf("%s\n", send_buffer);
	send_all(connfd, send_buffer);
	ack_Close(connfd);
}

void bad_request(int connfd, char *send_buffer)
{
	FILE *resourcefp = fopen("Webpages/error_pages/badrequest.txt", "r");
	if(resourcefp == NULL)
	{
		fprintf(stderr, "error pages hard coded, name: Webpages/error_pages/badrequest.txt\n");
	    ack_Close(connfd);
	}
	fread(send_buffer, sizeof(char), BUFFER_SIZE, resourcefp);

	send_all(connfd, send_buffer);
	ack_Close(connfd);
}
/* Reference 0006 sigaction wrapper to handle signals */
void (*Signal(int signo, void (*func)(int) ))(int) 
{
	struct sigaction act;
	struct sigaction oact;
	
	act.sa_handler = func;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	if(signo == SIGALRM)
	{
#ifdef  SA_INTERRUPT
		act.sa_flags |= SA_INTERRUPT; //sun os 4.x
#endif
	}
	else 
	{
#ifdef SA_RESTART
		act.sa_flags |= SA_RESTART; //svr4 , 4.4 BSD
#endif
	}

	if (sigaction(signo, &act, &oact) < 0)
		return (SIG_ERR);
	return oact.sa_handler;
}
/* function to handle child process termination */
void sig_child(int signo)		   
{
	pid_t pid;
	int stat;

	int save_errno = errno; /* errno is possibly overwritten by waitpid */
	do
	{
		pid = waitpid(-1, &stat, WNOHANG);
	}
	while(pid == -1);
	
	errno = save_errno; 
	return; //could interupt
}
