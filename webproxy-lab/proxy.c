#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
//sed -i 's/\r$//' driver.sh  (윈도우는 적용해야함)
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

// int main()
// {
//   printf("%s", user_agent_hdr);
//   return 0;
// }


void doit(int fd);
void read_requesthdrs(rio_t *rp);

void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);




int main(int argc, char **argv){
  int listenfd, connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  char client_hostname[MAXLINE], client_port[MAXLINE];

  if(argc != 2){
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(0);
  }
  listenfd = Open_listenfd(argv[1]);

  while(1){
    clientlen = sizeof(struct sockaddr_storage);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
    printf("Connected to (%s, %s)\n", client_hostname, client_port);
    doit(connfd);
    Close(connfd);
  }
  
}

void doit(int fd){
  char webname[MAXLINE], typename[MAXLINE], portnum[MAXLINE];

  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;
  size_t n;
  // 클라이언트 요청 라인 읽기
  Rio_readinitb(&rio, fd);
  rio_readlineb(&rio, buf, MAXLINE);
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  if(strcasecmp(method, "GET")){ //strcasecmp : 문자열을 비교하되, 대소문자는 무시하고 비교하라 0이면 같은것, 1이면 다른것
    clienterror(fd, method, "501", "Not implemented", "Tiny couldn't find this file");
    return;
  }


  parse_uri(uri, webname, portnum, typename);

  int serverfd = Open_clientfd(webname, portnum);

    if(serverfd < 0){
      fprintf(stderr, "conncection failed");
      return;
    }

    getheader(serverfd ,webname, portnum, typename);
// 서버의 응답을 클라이언트에게 전달 
  rio_t server_rio;
    Rio_readinitb(&server_rio, serverfd);  // 서버 응답 읽기
    while((n= Rio_readnb(&server_rio, buf, MAXLINE)) >0 ){
      rio_writen(fd, buf,n);// 클라이언트에게 전달
      printf("%s", buf);
    }

    Close(serverfd);
}

void getheader(int serverfd, char *webname, char *portnum , char * typename){
  char buf[MAXLINE];

  sprintf(buf, "GET %s HTTP/1.0\r\n", typename);
  rio_writen(serverfd, buf, strlen(buf));
  sprintf(buf, "Host: %s \r\n", webname);
  rio_writen(serverfd,buf, strlen(buf));
  sprintf(buf, "%s", user_agent_hdr);
  rio_writen(serverfd,buf, strlen(buf));
  sprintf(buf,"Connection: close \r\n" );
  rio_writen(serverfd,buf, strlen(buf));
  sprintf(buf,"Proxy-Connection: close\r\n\r\n");
  rio_writen(serverfd, buf, strlen(buf));

}

int parse_uri(char *uri, char *webname,char *portnum, char* typename)
{  //http://www.example.com:8080/index.html
  char *temp = strstr(uri, "://"); //uri 안에 있는 부분 문자열을 찾는다
  
  if(temp == NULL)
  return -1;

  char *path, *port ;

  char *start_except_http = temp + 3;
//1. 먼저 / 부터 분리
 path = strchr(start_except_http, "/");

// / 가 있으면 분리, 없으면 기본 설정으로 
  if(path){
    strcpy(typename, path);
    *path ='\0';
  }
  else{
    strcpy(typename, "/");
  }
//2. :부터 분리
  port = strchr(start_except_http, ':');
//:가 있으면 분리 없으면 기본 설정으로(80)
  if(port){
    *port = '\0';
    strcpy(portnum,port + 1);
    strcpy(webname, start_except_http);
  }
  else{
    strcpy(webname, start_except_http);
    strcpy(portnum,"80");
  }

  return 0;

}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg){
  char buf[MAXLINE], body[MAXBUF];

  sprintf(body, "<html><title>TinyError</title>");
  sprintf(body,"%s<body bgcolor = ""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s : %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em> The proxyWeb server</em>\r\n", body);

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));


}

void read_requesthdrs(rio_t *rp){
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")){ //buf에 개행이 있으면 종료
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

void serve_static(int fd , char *filename, int filesize, char *method){
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer : Proxy Web Server(What is this)\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf)); //위의 내용을 한꺼번에 fd에 보냄 
  printf("Response headers:\n");
  printf("%s", buf);

  if(strcasecmp(method,"GET") == 0){
    srcfd = Open(filename,O_RDONLY,0);
    srcp = Mmap(0,filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    Close(srcfd);
    Rio_writen(fd,srcp, filesize);
    Munmap(srcp, filesize);
  }
}

void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else
    strcpy(filetype, "text/plain");
}