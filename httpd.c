/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdint.h>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"
#define STDIN   0
#define STDOUT  1
#define STDERR  2    //标准输入输出，错误输出的文件描述符

void accept_request(void *);
void bad_request(int);     //400错误，请求不能被理解，或者说请求错误
void cat(int, FILE *);      //处理文件，读取文件内容发送到客户端
void cannot_execute(int);   //500错误，服务器内部错误
void error_die(const char *);     //错误处理函数
void execute_cgi(int, const char *, const char *, const char *); //cgi调用函数
int get_line(int, char *, int);        //从缓冲区读取一行
void headers(int, const char *);      //应答报文的头部行
void not_found(int);       //404，资源不存在
void serve_file(int, const char *);    //处理文件请求
int startup(u_short *);                //初始化服务器
void unimplemented(int);              //请求方法被禁用

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void accept_request(void *arg) //参数可以是任意类型的指针，但这里的arg其实不是指针，而是一个和指针占据字节大小相同的长整形变量，因为可以当做void* 类型，从main（）中传入的参数可以看出
{
    int client = (intptr_t)arg;//client是本次连接的唯一标识socket
    char buf[1024]; //缓冲区
    size_t numchars;
    char method[255];
    char url[255];
    char path[512]; //路径
    size_t i, j;
    struct stat st; //文件状态信息
                    /*    struct stat  
                      //   {   
                      //       dev_t       st_dev;     /* ID of device containing file -文件所在设备的ID*/    
                      //       ino_t       st_ino;     /* inode number -inode节点号*/    
                      //       mode_t      st_mode;    /* protection -保护模式?*/    
                      //       nlink_t     st_nlink;   /* number of hard links -链向此文件的连接数(硬连接)*/    
                      //       uid_t       st_uid;     /* user ID of owner -user id*/    
                      //       gid_t       st_gid;     /* group ID of owner - group id*/    
                      //       dev_t       st_rdev;    /* device ID (if special file) -设备号，针对设备文件*/    
                      //       off_t       st_size;    /* total size, in bytes -文件大小，字节为单位*/    
                      //       blksize_t   st_blksize; /* blocksize for filesystem I/O -系统块的大小*/    
                      //       blkcnt_t    st_blocks;  /* number of blocks allocated -文件所占块数*/    
                      //       time_t      st_atime;   /* time of last access -最近存取时间*/    
                      //       time_t      st_mtime;   /* time of last modification -最近修改时间*/    
                      //       time_t      st_ctime;   /* time of last status change - */    
                      //  };  标识文件夹信息的结构体,以下是该结构体的用法
                      //struct stat buf;    
                      // int result;    
                     // result = stat ("./Makefile", &buf);    
    
    int cgi = 0;      /* becomes true if server decides this is a CGI ，是否调用cgi程序
                       * program */
    char *query_string = NULL;

    numchars = get_line(client, buf, sizeof(buf));
    i = 0; j = 0;
    while (!ISspace(buf[i]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[i];
        i++;
    }
    j=i;
    method[i] = '\0';

    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) //未实现的方法，只有get和post两个选项
    {
        unimplemented(client);
        return;
    }

    if (strcasecmp(method, "POST") == 0) //若方法为post则需要向服务器传递实体信息，调用CGI程序
        cgi = 1;

    i = 0;
    while (ISspace(buf[j]) && (j < numchars))
        j++;
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars))
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';

    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0')) //只有为get方法时，才考虑qstring，因为get方法可以将参数跟在URL后面
            query_string++;
        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }

    sprintf(path, "htdocs%s", url);
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
    if (stat(path, &st) == -1) {   //文件不存在，那么就丢弃缓存区的其他内容
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
        not_found(client);
    }
    else
    {
        if ((st.st_mode & S_IFMT) == S_IFDIR)//获取文件信息，如果是目录就拼接默认主页  st_mode：文件类型和权限
            strcat(path, "/index.html");
        if ((st.st_mode & S_IXUSR) ||  (st.st_mode & S_IXGRP) ||   (st.st_mode & S_IXOTH)    )  //用户执行、用户组执行、其他执行
            cgi = 1;
        if (!cgi)
            serve_file(client, path);
        else
            execute_cgi(client, path, method, query_string);
    }

    close(client);
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{                              // 返回给客户端这是个错误请求，HTTP 状态吗 400 BAD REQUEST.
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
    char buf[1024];

    fgets(buf, sizeof(buf), resource);//从文件里读一行，遇到换行符或者eof error停止，循环读取发送
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
    perror(sc);
    exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path, const char *method, const char *query_string)   //连接socket，路径，方法，查询请求
{
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A'; buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0)    //get方法可以忽略头部
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
    else if (strcasecmp(method, "POST") == 0) /*POST*/
    {
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16])); //这里只能读取一个字符，应该用字符串拆分
            numchars = get_line(client, buf, sizeof(buf));
        }
        if (content_length == -1) {   //内容长度为-1；
            bad_request(client);
            return;
        }
    }
    else/*HEAD or other*/
    {
    }


    if (pipe(cgi_output) < 0) {
        cannot_execute(client);
        return;
    }
    if (pipe(cgi_input) < 0) {
        cannot_execute(client);
        return;
    }

    if ( (pid = fork()) < 0 ) {
        cannot_execute(client);
        return;
    }
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    if (pid == 0)  /* child: CGI script */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], STDOUT);  //标准输出重定向到子进程管道写端，往fd[1]写入的数据可以由fd[0]读出（这里是让两个文件描述符指向同一个文件，其实是因为dup返回的是不小于第二个参数的可用文件描述符
        dup2(cgi_input[0], STDIN);    //标准输入1重定向至子进程管道读端
        close(cgi_output[0]);
        close(cgi_input[1]);
        sprintf(meth_env, "REQUEST_METHOD=%s", method); //拼接字符串
        putenv(meth_env); //添加环境变量，是一对键值对
        if (strcasecmp(method, "GET") == 0) {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else {   /* POST */
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        execl(path, NULL); //用来执行第一个参数所代表的文件路径，接下来的参数代表执行该文件时传递的参数，最后一个指针必须用空指针NULL作结束；成功不返回值，失败-1；
        exit(0);
    } else {    /* parent */
        close(cgi_output[1]);
        close(cgi_input[0]);   //父进程用input写，用output读，子进程反之
        if (strcasecmp(method, "POST") == 0)  //读取从客户端请求数据发送给CGI子程序
            for (i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        while (read(cgi_output[0], &c, 1) > 0)从CGI子进程读取响应数据返回给客户端
            send(client, &c, 1, 0);

        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);//等待子进程结束
    }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0); //第二和第三个参数指定读缓冲区的位置和大小，第四个参数为可选参数值，一般设为0，返回值为实际读取的数据长度
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
            if (c == '\r') //回车
            {
                n = recv(sock, &c, 1, MSG_PEEK);//MSG_PEEK,窥探读缓存中的数据，查看而不取走
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n')) //换行
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';

    return(i);
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client)   //资源未找到，用HTML封装
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A'; buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */ //丢掉头部信息
        numchars = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r"); //以只读方式打开文件
    if (resource == NULL)
        not_found(client);
    else
    {
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(u_short *port)  //服务器启动工作，先设好监听端口
{
    int httpd = 0;
    int on = 1;
    struct sockaddr_in name;

    httpd = socket(PF_INET, SOCK_STREAM, 0);//创建服务器用于监听的Unix系统性的TCP socket，返回-1表示创建失败，成功返回一个文件描述符
                                            //第一个参数为底层协议ip4||6,第二个代表流或是数据报，也就是TCP和udp的选择，第三个参数为具体的协议，不过一般已经由前两个决定
    if (httpd == -1)
        error_die("socket");
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);  //主机字节序转为网络字节序，端口和IP都要
    if ((setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)  
    {  
        error_die("setsockopt failed");
    }
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)  //给socket绑定IP和port，第二个是打包好的IP地址，第三个为该结构体长度，成功返回0，失败-1，设置错误信息
        error_die("bind");
    if (*port == 0)  /* if dynamically alloating a port */
    {
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)//getsockname函数用于获取与某个套接字关联的本地协议地址（本机IP和端口）
                                                                         //getpeername函数用于获取与某个套接字关联的外地协议地址(对端IP和端口），成功返回0，失败-1
            error_die("getsockname");
        *port = ntohs(name.sin_port);//在getsockname这一步相当于自动分配了端口号，因而在这一步可以直接通过字节序转换得到了主机字节序的端口号16位
    }
    if (listen(httpd, 5) < 0) //第二个长度为监听队列的最大长度，超过服务器将不接受新的客户端连接，客户端也将受到ECONNREFUSED错误信息，监听成功返回0，失败-1；
        error_die("listen");
    return(httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void)
{
    int server_sock = -1;
    u_short port = 4000;
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t  client_name_len = sizeof(client_name);
    pthread_t newthread;

    server_sock = startup(&port);//传入port传回已经设置为监听状态的socket的文件描述符，若传入port为0，则默认动态分配port
    printf("httpd running on port %d\n", port);

    while (1)
    {
        client_sock = accept(server_sock,
                (struct sockaddr *)&client_name,
                &client_name_len); //accept接收客户端的连接第一个参数为已经设置为listen状态的socket，成功时返回值为一个新的连接socket的文件描述符，唯一的标识被接受的这个连接，后续服务器可通过该socket与客户端进行通信
        if (client_sock == -1)     //后续服务器可通过该socket与客户端进行通信，第二个参数带回了与服务器相连的远程socket地址，第三个参数表示地址长度，函数返回-1表示调用失败
            error_die("accept");
        /* accept_request(&client_sock); */
        //创建线程，第一个参数是新线程的标识符，第二个参数为线程属性，第三个为该线程运行的函数，第四个参数为该运行函数的参数，成功返回0，失败返回错误码
        if (pthread_create(&newthread , NULL, (void *)accept_request, (void *)(intptr_t)client_sock) != 0) //这里线程运行函数的参数为建立的新连接的文件描述符
                                 //int pthread_create(pthread_t * thread , const pthread_attr_t * attr, void *(*start_routine)(void *), void * arg)函数原型
            perror("pthread_create");
    }

    close(server_sock);

    return(0);
}
