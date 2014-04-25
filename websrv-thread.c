/*
 * Mike Gedelman
 * 
 * server.c
 *  please note some of this code comes from an assignment I did for CS 455 
 * namely establish()
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>  
#include <signal.h> 
#include <unistd.h> 
#include <sys/wait.h> 
#include <netinet/in.h> 
#include <netdb.h>
#include <math.h>
#include <sys/types.h> 
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <sys/time.h>
#include <sys/select.h>

#define _GNU_SOURCE
#include <sys/socket.h> 

#include "thread_pool.h"

#define MAXHOSTNAME 400

#define REQUEST_BUF_SIZE 4096 /* use 4K size buffer for incoming messages */
#define WEBROOT "." /* current directory is webroot */
#define FILE_BUFFER (1024*1024*1024) /* use up to 1G memory for retrieving
                                        files */

#define MAX_THREADS 10 /* up to 10 threads */
#define VERBOSE

static char *CRLF = "\r\n"; /* carriage return + line feed */

int request_timeout = 10; /* time out after 10 seconds for requests */


char myname[MAXHOSTNAME];
int myport;

struct response {
   int http_version; /* 0: http/1.0; 1: http/1.1 */
   int status_code;
   char* reason_phrase;
   char* message_body;
   char* content_type;
   int content_length;
};

struct request_options {
   int http_version; /* 0: http/1.0; 1: http/1.1 */
   int close_connection; 
   char* host;
};

void response_enum(
   struct response *r, 
   int status_code, 
   char* reason_phrase,
   char* message_body,
   char* content_type)
{
   r->status_code  = status_code;
   r->reason_phrase = reason_phrase;
   r->message_body = message_body;
   r->content_type  = content_type;
}
                           

/* http://www.cis.temple.edu/~ingargio/old/cis307s96/readings/docs/sockets.html
 * code to establish a socket; originally from bzs@bu-cs.bu.edu
 */ 
int establish(unsigned short portnum) { 
   int s; 
   struct sockaddr_in sa; 
   struct hostent *hp; 

   memset(&sa, 0, sizeof(struct sockaddr_in)); /* clear our address */
   gethostname(myname, MAXHOSTNAME); /* who are we? */ 
   hp=gethostbyname(myname);

   if (hp == NULL) /* we don't exist !? */
      return(-1); 

   sa.sin_family= hp->h_addrtype; /* this is our host address */ 
   sa.sin_port= htons(portnum); /* this is our port number */ 

   if ((s= socket(AF_INET, SOCK_STREAM, 0)) < 0) /* create socket */
      return(-1); 
   if (bind(s,&sa,sizeof(struct sockaddr_in)) < 0) { 
      close(s); 
      return(-1); /* bind address to socket */ 
   } 

   listen(s, 3); /* max # of queued connects */ 
   return s; 
}

/* block on given fd; 0 for timeout; -1 for error */
int easy_select(int fd, int seconds) {
   fd_set rfds;
   FD_ZERO(&rfds);
   FD_SET(fd, &rfds);
   if (seconds <= 0) {
      if (select(fd+1, &rfds, NULL, NULL, NULL) < 0) {
         perror("select");
         return -1;
      }
      else
         return 1;
   }
   else {
      struct timeval tv;
      tv.tv_sec = seconds;
      tv.tv_usec = 0;
      int retval;
      if ((retval = select(fd+1, &rfds, NULL, NULL, &tv)) < 0) {
         perror("select");
         return -1;
      }
      else 
         return retval;
   }
}

/* block on given fd; 0 for timeout; -1 for error */
int easy_select_w(int fd, int seconds) {
   fd_set rfds;
   FD_ZERO(&rfds);
   FD_SET(fd, &rfds);
   if (seconds <= 0) {
      if (select(fd+1, NULL, &rfds, NULL, NULL) < 0) {
         perror("select");
         return -1;
      }
      else
         return 1;
   }
   else {
      struct timeval tv;
      tv.tv_sec = seconds;
      tv.tv_usec = 0;
      int retval;
      if ((retval = select(fd+1, NULL, &rfds, NULL, &tv)) < 0) {
         perror("select");
         return -1;
      }
      else 
         return retval;
   }
}

/* below are send and recv functions to be used by threads. */
int tp_send(int sockfd, char *buf, size_t nbytes, int flags) {
   int bytes_sent = 0;
   while (bytes_sent < nbytes) {
      int bytes = send(sockfd, buf, nbytes, flags);
      tp_suspend();
      if (bytes < 0) {
         if (errno == EAGAIN || errno == EWOULDBLOCK)
            easy_select_w(sockfd, 0);
         else {
            perror("tp_send");
            return -1;
         }
      }
      else
         bytes_sent += bytes;
   }
}

int tp_recv(int sockfd, char *buf, size_t len, int flags) {
   int bytes_recv = 0;
   while (bytes_recv < 1) {
      int bytes = recv(sockfd, buf, len, flags);
      tp_suspend();
      if (bytes < 0) {
         if (errno == EAGAIN || errno == EWOULDBLOCK) 
            easy_select(sockfd, request_timeout); /* TODO handle timeout cnd */
         else {
            perror("tp_recv");
            return -1;
         }
      }
      else
         bytes_recv += bytes;
   }
}

/* pretty much wrapper fxn for recv_request, for threading */
void serve_request(void *ptr) {
   int s = *(int*)ptr;
   printf("serving request on socket %i\n", s);
   recv_request(s);
   close(s);

   return;
}  

/* A pool of threads is created using a thread pooling library based on the
 * user-space threads "sigaltstack" method. These threads will be activated
 * accordingly to deal with incoming requests.
 */
int main(int argc, char **argv) {
   int s, t;

   if (argc != 2) {
      printf("usage: %s portnum\n", argv[0]);
      exit(-1);
   }
   
   myport = (int) strtol(argv[1], (char**)NULL, 10); /* str -> int */

   if ((s= establish(myport)) < 0) {
      perror("establish"); 
      exit(1); 
   }
   fcntl(s, F_SETFL, O_NONBLOCK);

   tp_init(3);
   tp_create(3);
  
   for (;;) {
      if ((t = accept4(s,NULL,NULL,SOCK_NONBLOCK)) < 0) {
         if (errno == EWOULDBLOCK || errno == EAGAIN) {
            if (tp_continue() < 0) /* no active threads */
               easy_select(s, 0); /* then wait for incoming connection */
               
            continue;
         }
         else if (errno == EINTR)
            continue;
         else {
            perror("accept");
            exit(1);
         }
      }
      else {
         puts("got connection");
         tp_activate(serve_request, (void*)&t);
         puts("served");
      }
   }
 
   /* should never reach this code */
   close(s);
   return(0);     
}

/*
 * Send function. Makes sure we're sending the entire length of the string.
 */
int send_msg(int t, const char *c, int flags) {
   if (c == NULL)
      return -1;

   int bytes_sent = 0;
   int len = strlen(c);

   while (bytes_sent < len) 
      if ((bytes_sent += send(t,c+bytes_sent,len-bytes_sent,flags)) < 0) {
         perror("send");
         return -1;
      }

   return bytes_sent;
}

int recv_msg(int t, int min_size, char *buf, int bufsize) {
   int bytes_recv = 0;
   
   while (bytes_recv < min_size) 
      if ( (bytes_recv += recv(t,buf+bytes_recv,bufsize,0)) < 0 ) {
         perror("recv");
         return -1;
      }
                        
   return bytes_recv;
}

void respond(int, struct response*, struct request_options*);

void err_response(int sock, int status_code, const char *reason) {
      struct response r;
      struct request_options opt;
      opt.close_connection = 1;
      response_enum(&r, status_code, reason, NULL, NULL);

      char content[256];
      sprintf(content, 
              "<!DOCTYPE HTML><html><head><title>%i: %s</title></head> \
               <body><h1>%i: %s</h1></body></html>",
              status_code,
              reason,
              status_code,
              reason);

      r.content_type = "text/html";
      r.content_length = strlen(content);
      r.message_body = content;

      respond(sock, &r, &opt);
      close(sock);
      return;
}

/* wrapper for err_response since we use this so much */
#define bad_request(t) err_response(t, 400, "Bad Request")

int find_double_crlf(char *s, size_t size) {
   int i;
   int j;
   for (i=0;(i+3)<size;i++)
      if (s[i]=='\r' && s[i+1] == '\n' && s[i+2] == '\r' && s[i+3] == '\n')
         return i;

   /*for (i=0;(i+1)<size;i++)
      if ((s[i] == '\r' && s[i+1] == '\r') || (s[i] == '\n' && s[i+1] == '\n'))
         return i;*/

   return -1;
}

int recv_request(int sock) {
   char recv_buffer[REQUEST_BUF_SIZE];
   struct response r;
   
   int double_clrf_flag = 0;
   int crlf_index;
   char *buf_ptr = recv_buffer;
   int bytes_recvd = 0;
   while (1) {
      int bytes = tp_recv(sock, buf_ptr, REQUEST_BUF_SIZE, 0);
      if (tp_recv < 0)
         return -1;
      else if (tp_recv == 0) {
         err_response(sock, 408, "Request Timeout");
         return -1;
      }
         
      bytes_recvd += bytes;

      if ((crlf_index = find_double_crlf(recv_buffer, bytes_recvd)) == 0) {
         bytes_recvd = 0;
         recv_buffer[0] = 0;
      }
      else if (crlf_index > 0) {
         recv_buffer[crlf_index] = 0;
         break;
      }
      else
         buf_ptr += bytes_recvd;
   }

   #ifdef VERBOSE
   puts(recv_buffer);
   #endif

   char req[4];
   memcpy(req, recv_buffer, 3);
   req[3] = 0;

   if ( strcmp(req, "GET") == 0)
      return get_request(sock, recv_buffer+4);
   else {
      bad_request(sock);
      return 0;
   }
}

/*
 * builds a respones header into buf based on response r
 * assumes all fields in respose r are being used
 * use cgi flag to only build status line, allowing the script to specify 
 *  its own headers
 */
void response_header(struct response *r, char *buf, int cgi) {
   char tmp[128];

   /* build status line */
   sprintf(tmp, 
          "HTTP/1.%i %i %s",
          r->http_version,
          r->status_code,
          r->reason_phrase);

   strcpy(buf, tmp);
   
   if (cgi)
      return;

   /* get date for timestamp */
   time_t current_time;
   char *c_time_str;
   current_time = time(NULL);
   c_time_str = ctime(&current_time);

   /* build headers */
   sprintf(tmp,
          "\r\nContent-Type: %s\r\nContent-Length: %i\r\nDate: %s\r\n",
          r->content_type,
          r->content_length,
          c_time_str);

   strcat(buf, tmp);
}

void respond(int t, struct response *r, struct request_options *opt) {
   char buf[255];

   response_header(r, buf, 0);
   
   if (r->message_body) {
      tp_send(t, buf, strlen(buf), MSG_MORE);
      tp_send(t, r->message_body, r->content_length, 0);
   }
   else 
      tp_send(t, buf, strlen(buf), 0);

   /*if (!opt->close_connection) {
      recv_request(t);
   }
   else {
      close(t);
      return;
   }*/
   return;
}

/* get index of first occurence of c; -1 if not found */
int strchr_ind(const char *s, char c) {
   int i;
   for (i=0;s[i]!=0;i++)
      if (s[i] == c)
         return i;

   return -1;
}

int min(int a, int b) {
   if (a <= b)
      return a;
   else 
      return b;
}

/* req should point to the first request header field, not the request-line
 * Options supported:
 *   Connection [close|Keep-Alive]
 *   Host
 */
void parse_options(int t, struct request_options *opt, char *req) {
   if (req[0] == '\r' && req[1] == '\n')
      return;

   while(1) {
      char option[50];
      char value[50];
      int colon_ind = strchr_ind(req, ':');
      strncpy(option, req, colon_ind);
      option[colon_ind+1]=0;

      int value_end = min(strchr_ind(req, '\r'), strchr_ind(req, ' '));
      int i;
      int j=0;
      for (i=colon_ind+2; i < value_end; i++)
         value[j++] = req[i];
      value[j] = 0;

      if (strcmp(option, "Host") == 0) {
         char *port_s;
         if (port_s = strchr(value, ':')) {
            port_s++;
            int port = (int) strtol(port_s, (char**)NULL, 10);
            
            value[strchr_ind(value, ':')] = 0;
            if (port != myport)
               ;
               //bad_request(t);
   
         }
        if (value != myname)
               ;
               //bad_request(t);
      }
      else if (strcmp(option, "Connection") == 0) {
         if (strcasecmp(value,"close") == 0)
            opt->close_connection = 1;
         else if (strcasecmp(value,"Keep-Alive") == 0)
            opt->close_connection = 1;
         else
            ;
            //bad_request(t);
      }
      if (req = strchr(req, '\n'))
         req++;
      else
         return;
   }
}

void get_regular_file(int,char*,struct request_options*,char*);
void get_cgi_file(int,char*,struct request_options*);
void get_dir(int,char*,struct request_options*);

/* expects the "GET" part to already be chopped off of req
 * also req is notw null terminated, not CRLF terminated.
 */
int get_request(int t, char *req) {
   char file[255];
   struct request_options opt;

   /* mark end of request by changing the blank line to \0 
   int i;
   for (i=0; req[i]; i++)
      if (req[i] == '\r' && req[i+1] == '\n' && req[i+2] == '\r' && req[i+3] == '\n') {
         req[i] = 0;
         break;
      }
   int req_len = strlen(req);*/

   /* get filename */
   int cr_index = strchr_ind(req, '\r');
   int sp_index = strchr_ind(req, ' ');
   if ((cr_index < 1) && (sp_index < 1)) {
      bad_request(t);
      return;
   }
   
   int index;
   if ((cr_index > 0) && (sp_index > 0))
      index = min(cr_index,sp_index);
   else {
      if (cr_index < 1)
         index = sp_index;
      else
         index = cr_index;
   }

   strncpy(file, req, index);
   file[index]=0;
   req += index+1;

   /* check for HTTP version */
   char http_specifier[9];
   memcpy(http_specifier, req, 8);
   http_specifier[8] = 0;
      
   if (strcmp(http_specifier, "HTTP/1.1") == 0) {
      opt.http_version = 1;
      opt.close_connection = 0;
   }
   else if (strcmp(http_specifier, "HTTP/1.0") == 0) {
      opt.http_version = 0;
      opt.close_connection = 1;
   }
   else {
      bad_request(t);
      return;
   }

   char *p = strchr(req, '\n');
   if (p) {
      p++;
      parse_options(t, &opt, p);
   }

   /* check ending, and then call appropriate fxn. */
   char *ending = NULL;
   int path_len = strlen(file);
   int i;
   for (i=path_len; i > 0; i--)
      if (file[i] == '.')
         ending = file+i+1;

   if (ending == NULL)
      get_regular_file(t, file, &opt, "text/plain");
   else if (strcmp(ending,"cgi")==0)
      get_cgi_file(t, file, &opt);
   else if (strcmp(ending,"jpeg")==0 || strcmp(ending,"jpg")==0)
      get_regular_file(t, file, &opt, "image/jpeg");
   else if (strcmp(ending,"gif")==0)
      get_regular_file(t, file, &opt, "image/gif");
   else if (strcmp(ending, "html")==0 || strcmp(ending,"htm")==0)
      get_regular_file(t, file, &opt, "text/html");
   else
      get_regular_file(t, file, &opt, "text/plain"); /* default to text/plain */

   return;
}

void file_error(t) {
   switch (errno) {
         case ENOTDIR:
         case ENOENT:
            err_response(t, 404, "File not found");
            break;
         case EACCES:
            err_response(t, 403, "Forbidden");
            break;
         case EIO:
         default:
            err_response(t, 500, "Internal server error");
      }   
      return;
}

void get_regular_file(
   int t, 
   char *uri, 
   struct request_options *opt, 
   char *content_type) 

{
   struct stat sbuf;
   int fd;
   struct response r;

   /*if (strcmp(uri,"/") == 0) {
      uri = "/index.html";
      content_type = "text/html";
   }*/


   int uri_len = strlen(uri);
   int path_len = uri_len + strlen(WEBROOT);
   char path[path_len];
   strcpy(path, WEBROOT);
   strcat(path, uri);

   if ((lstat(path, &sbuf)) < 0) {
      file_error(t);
      return;
   }

   switch (sbuf.st_mode & S_IFMT) { 
      case S_IFREG:
      case S_IFLNK:
         break;
      case S_IFDIR:
         get_dir(t, path, opt);
         return;
      case S_IFCHR:
      case S_IFBLK:
      case S_IFIFO:
      case S_IFSOCK:
         err_response(t, 404, "File not found");
         return;
   }

   if ((fd = open(path, O_RDONLY)) < 0) {
      file_error(t);
      return;
   }

   char header_buf[255];
   response_enum(&r, 200, "OK", NULL, content_type);
   r.http_version = opt->http_version;
   r.content_length = sbuf.st_size;
   response_header(&r, header_buf, 0);

   if ((send(t, header_buf, strlen(header_buf), MSG_MORE)) < 0)
      perror("send");

   puts(header_buf);

   int bytes_sent = 0;
   int servsz;
   void *src;
   while (bytes_sent < sbuf.st_size) {
      if ((sbuf.st_size - bytes_sent) > FILE_BUFFER)
         servsz = FILE_BUFFER;
      else
         servsz = sbuf.st_size - bytes_sent;

      if ((src = mmap(0, servsz, PROT_READ, MAP_PRIVATE, fd, bytes_sent))
            == MAP_FAILED) {
         perror("mmap");
         return -1;
      }

      bytes_sent += tp_send(t, src, servsz, MSG_MORE);
      munmap(src, servsz);
   }
   tp_send(t, "\r\n", 3, 0);

   return;
}

void get_cgi_file(int t, char *uri, struct request_options *opt) {
   struct stat sbuf;
   struct response r;

   int uri_len = strlen(uri);
   int path_len = uri_len + strlen(WEBROOT);
   char path[path_len];
   strcpy(path, WEBROOT);
   strcat(path, uri);

   if ((lstat(path, &sbuf)) < 0) {
      file_error(t);
      /* TODO check for executable */
   }

   /*int fd[2];
   pipe(fd);*/

      char header_buf[255];
      response_enum(&r, 200, "OK", NULL, NULL);
      r.http_version = opt->http_version;
      //r.content_length = bytes;
      response_header(&r, header_buf, 1);

      puts(header_buf);

      if ((send(t, header_buf, strlen(header_buf), MSG_MORE)) < 0)
         perror("send");

   int pid = fork();

   if (pid < 0) {
      perror("fork");
      err_response(t, 500, "Internal server error");
      return;
   }

   /* child: map stdout to connection socket */
   else if (pid == 0) {
      dup2(t, 1);
      exit(system(path));
   }
}

/* called from get_regular_file - we already know this exists and that it is
 * a dir, so we need not double check that.
 * fork a child, then do ls -lh on the path
 */
void get_dir(int t, char *path, struct request_options *opt) {
   char header_buf[255];
   struct response r;

   response_enum(&r, 200, "OK", NULL, NULL);
   r.http_version = opt->http_version;
   response_header(&r, header_buf, 1);
   strcat(header_buf, "\r\nContent-Type: text/plain\r\n\r\n");

   puts(header_buf);

   if ((send(t, header_buf, strlen(header_buf), MSG_MORE)) < 0)
      perror("send");

   int pid = fork();

   if (pid < 0) {
      perror("fork");
      return;
   }
   else if (pid == 0) {
      dup2(t, 1);
      char *command[255];
      sprintf(command, "ls -lh %s", path);
      exit(system(command));
   }
}




