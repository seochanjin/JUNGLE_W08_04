/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

//Rio_writen : 원하는 문자열 만큼 출력되지 않으면, 다시 시도해서 원하는 만큼 출력될때까지 반복
//Rio_readlineb : 개행(\n) 만나거나 MAXLINE -1 도달하면 종료

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
//void serve_static(int fd, char *filename, int filesize);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0); //소켓을 도메인 네임으로
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}



void doit(int fd){
  int is_static;
  struct  stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  //리퀘스트랑 헤더 읽기
  Rio_readinitb(&rio, fd); //rio 읽기를 위한 버퍼 초기화(buf와 fd 연결)
  Rio_readlineb(&rio, buf, MAXLINE); // 줄 읽기 
  printf("Request headers:\n");
  printf("%s", buf);//fd의 첫 번째 줄 출력 
  sscanf(buf, "%s %s %s", method, uri, version); //buf 안에 있는 내용을 3개의 인자에 넣음

  //GET /home.html HTTP/1.0 [엔터][엔터]
  //HEAD /home.html HTTP/1.0 [엔터][엔터]
  //* 과제 11.11 HTTP Header 지원 (헤더(메타데이터)만 보여주는 로직 추가)
  if(strcasecmp(method, "GET") && strcasecmp(method, "HEAD") ){ //strcasecmp : 문자열을 비교하되, 대소문자는 무시하고 비교하라 0이면 같은것, 1이면 다른것
    clienterror(fd, method, "501", "Not implemented", "Tiny couldn't find this file");
    return;
  }
  read_requesthdrs(&rio);

  is_static = parse_uri(uri, filename, cgiargs); //정적이면 1 , 동적이면 0 
  if(stat(filename, &sbuf) < 0){ // stat: 속성 정보(metadata)' 를 가져오고 sbuf라는 구조체에 내용을 채움
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  if(is_static){ //정적이면
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){ //S_ISREG : 일반 파일인가? S_IRUSR : 소유자의 읽기 권한 
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method);
  }
  else{ //동적이면
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){ //S_ISREG : 일반 파일인가? S_IXUSR : 소유자의 실행 권한 
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd,filename, cgiargs);
  }
  
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg){
  char buf[MAXLINE] , body[MAXBUF];
  /******오류 가능성 높음 */
  sprintf(body, "<html><title>Tiny Error</title>"); //C 프로그램이 만든 평범한 문자열을 웹 브라우저가 받아서, 그것이 HTML 코드라는 것을 인지하고, 그 코드의 규칙에 따라 해석
  sprintf(body, "%s<body bgcolor = ""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum,shortmsg);
  sprintf(body, "%s<p>%s : %s\r\n", body, longmsg,cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd,buf,strlen(buf)); //Rio_writen : 원하는 문자열 만큼 출력되지 않으면, 다시 시도해서 원하는 만큼 출력될때까지 반복
  sprintf(buf, "Content-type: text/html\r\n"); //sprintf ->Rio_writen 한줄 씩 보냄(buf를 덮어씀)
  Rio_writen(fd,buf, strlen(buf)); // buf의 내용을 fd에 보냄 
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd,buf, strlen(buf));
  Rio_writen(fd, body,strlen(body));

}

void read_requesthdrs(rio_t *rp){ //서버에 적히는 내용
  char buf[MAXLINE];

  Rio_readlineb(rp, buf,MAXLINE); 
  while(strcmp(buf, "\r\n")){ //line마다 읽는 건데 rp의 맨 마지막이 \r\n으로 끝나기 때문에 rp가 끝난 거임 
    Rio_readlineb(rp,buf, MAXLINE); //Rio_readlineb : 개행(\n) 만나거나 MAXLINE -1 도달하면 종료
    printf("%s", buf); //rp 가 포인터의 성질도 갖고 있어 끝으로 움직임 그래서 다음 줄이 되는 거임
    printf("cutie\n");
  }
  return;
}
// uri : 클라이언트가 요청한 Uniform Resource Identifier
// uri를 인자들로 쪼개주는 역할 
// cgiargs: CGI 인자들을 저장할 변수 
int parse_uri(char *uri , char *filename, char *cgiargs){ //정적인가 동적인가 확인
  char *ptr;
//문자열에 cgi-bin이 포함되어있는가? NO [1]정적컨텐츠 [2]동적컨텐츠
  if(!strstr(uri, "cgi-bin")){ //[[1]정적컨텐츠 
    strcpy(cgiargs, ""); //넘겨줄 cgiargs 인자가 없습니다 
    strcpy(filename, "."); //filename 에 . 를 복사
    strcat(filename, uri); //filename에 uri 문자열을 이어 붙임
    if(uri[strlen(uri) -1] == '/') //uri 가 /로 끝나는 경우 : home.html 를 붙여줌(기본파일)
      strcat(filename, "home.html"); 
    return 1;
  }
  else{ //[2]동적 컨텐츠
    ptr = index(uri, '?'); //?가 있는 주소를 ptr에 저장
    if(ptr){
      strcpy(cgiargs, ptr +1); //ptr 뒤에 있는 문자부터 cgiargs 에 복사
      *ptr = '\0'; //?를 \0으로 바꿈(uri는 ? 전까지의 문자열을 가짐 ex)/cgi-bin/adder?a=10&b=20 였던 uri는 /cgi-bin/adder 라는 문자열)
    }
    else{
      strcpy(cgiargs,""); //ptr(?가 없다면) cgiargs에는 아무 것도 저장안함
    }
    strcpy(filename,"."); // ./ 로 시작
    strcat(filename, uri); //uri를 뒤에 복사
    return 0;
  }

}

// void serve_static(int fd, char *filename, int filesize)
// {
//   int srcfd;
//   char *srcp, filetype[MAXLINE], buf[MAXBUF];

//   get_filetype(filename, filetype); 
//   sprintf(buf, "HTTP/1.0 200 OK\r\n");  //buf에 내용 덧붙이기
//   sprintf(buf, "%sServer : Tiny Web Server\r\n", buf); //"HTTP/1.0 200 OK\r\nServer: Tiny Web Server\r\n"
//   sprintf(buf, "%sConnection: close\r\n", buf);
//   sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
//   sprintf(buf, "%sContent-type : %s\r\n\r\n", buf, filetype);
//   Rio_writen(fd, buf, strlen(buf));
//   printf("Response headers:\n");
//   printf("%s", buf);

//   srcfd = Open(filename, O_RDONLY, 0); // O_RDONLY(파일시스템): READ_ONLY
//   srcp = Mmap(0,filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); //mmap: 주소로 접근 //PROT_READ(메모리): READ_ONLY
//   Close(srcfd);
//   Rio_writen(fd, srcp, filesize); //scrp(wnth)에서 filesize만큼의 데이터를 클라이언트소켓(fd)로 전송
//   Munmap(srcp, filesize); //mmap으로 설정했던 메모리 매핑 해제, 자원반남

// //mmap 디스크 → 커널 → 네트워크 카드
// //malloc 디스크 → 커널 → 사용자 메모리 → 커널 → 네트워크 카드 (두번 복사)

// //과제 11.9 
//   // srcfd = Open(filename, O_RDONLY, 0);
//   // srcp = Malloc(filesize);
//   // Rio_readn(srcfd, srcp, filesize);
//   // Close(srcfd);
//   // Rio_writen(fd, srcp, filesize); //scrp의 내용을 fd(소켓)으로 전송 

//   // Free(srcp);
// }


void serve_static(int fd, char *filename, int filesize, char *method) //클라이언트에 적히는 내용
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  get_filetype(filename, filetype); 
  sprintf(buf, "HTTP/1.0 200 OK\r\n");  //buf에 내용 덧붙이기
  sprintf(buf, "%sServer : Tiny Web Server\r\n", buf); //"HTTP/1.0 200 OK\r\nServer: Tiny Web Server\r\n"
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type : %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf)); //fd에 buf의 내용을 buf의 길이 만큼 써라 //buf에 있는 내용 n 만큼을 이제 소켓 fd에 보냄
  printf("Response headers:\n");
  printf("%s", buf);

  if(strcasecmp(method, "GET") == 0){ /**** 여기 때문에 method를 넣음*/
  srcfd = Open(filename, O_RDONLY, 0); // O_RDONLY(파일시스템): READ_ONLY
  srcp = Mmap(0,filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); //mmap: 주소로 접근 //PROT_READ(메모리): READ_ONLY
  Close(srcfd);
  Rio_writen(fd, srcp, filesize); //scrp(wnth)에서 filesize만큼의 데이터를 클라이언트소켓(fd)로 전송
  Munmap(srcp, filesize); //mmap으로 설정했던 메모리 매핑 해제, 자원반남

  }
}

void serve_dynamic(int fd, char *filename, char *cgiargs){
  char buf[MAXLINE] , *emptylist[] = {NULL};

  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf)); // 최소의 HTTP 응답 헤더를 클라이언트(fd)에게 보냄
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));
//setenv: 변수를 생성해서 값을 저장, 3인자가 1이면: 새 값으로 덮어씀 0: 기존 값이면 변경 X
  if(Fork() == 0){ //Fork = 자식 프로세스 생성 Fork() == 0 : 새로 생성된 자식프로세스만 true
    setenv("QUERY_STRING", cgiargs,1); //QUERY_STRING :CGI 에게 인자를 전달하는 공식적인 통로 , CGI는 cgiargs를 읽어서 사용자의 입력을 받음
    Dup2(fd, STDOUT_FILENO); //표준 출력(모니터)을 닫고, 그 자리에 클라이언트와 연결된 소켓(fd)을 복제 , fd로 데이터를 전송함
    Execve(filename, emptylist, environ); //자식프로세스는 CGI 프로세스 실행
  }
  //부모 프로세스 (지배인) - 입력 및 준비 담당
  //자식 프로세스 (전문 비서) - 실행 및 출력 담당
  Wait(NULL); //부모 프로세스: 자식 프로세스 중 하나가 종료될 때까지 실행 정지
}

void get_filetype(char *filename, char *filetype){
  if(strstr(filename, ".html")) //filename 에 .html이 있는지 확인
    strcpy(filetype, "text/html"); //filetype에 text/html 를 복사
  else if(strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if(strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if(strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if(strstr(filename, ".mpg"))
    strcpy(filetype, "video/mpeg"); 
  else if(strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}