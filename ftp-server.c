#include<stdio.h>
#include<string.h>
#include<signal.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/epoll.h>
#include<stdlib.h>  //exit
#include<unistd.h>
#include<sys/types.h>
#include<sys/socket.h>
#define MAXCHILD 10
#define CFGNAME "config.cfg"
#define MAXEVENTS 21
typedef struct message
{
    int len;
    char buf[4096-sizeof(int)];
}MSG;
typedef struct child_tag
{
    int pid;
    int fd;
    int busy;
    int client_fd;
}child_t;
struct epoll_event ev,events[MAXEVENTS];//事件

void handle_comd(int cfd,char* buf_comd);
void child_task(int p_fd);
void recv_fd(int p_fd,int *cfd,char* buf_comd);
int init_socket(char* configname);
void send_fd(int child_fd,int cfd,char* buf_comd);
void make_child(child_t* child);
void func_gets(int cfd,char *filename);
void func_puts(int cfd,char* filename);
void func_shell(int cfd,char* cmd);
void sighandler(int signal);
void send_n(int cfd,char* p,int len);
int recv_n(int cfd,char* p,int len);

int main(int argc,char *argv[])
{
    signal(SIGCHLD,SIG_IGN);//SIG_IGN 表示忽略信号，不做处理   
							//SIG_DFL表示交由系统缺省处理，相当于白注册了
    signal(SIGINT,sighandler);
    char *configname=CFGNAME;
    if(argc==2)
        configname=argv[1];
    child_t child[MAXCHILD]; //定义子进程数组
    make_child(child);//初始化子进程
    int sfd=init_socket(configname);//初始化套接字
    int epfd=epoll_create(1);
    if(-1==epfd)
    {
        perror("epoll_create");
        exit(-1);
    }
    ev.events=EPOLLIN;
    ev.data.fd=sfd;
    if(epoll_ctl(epfd,EPOLL_CTL_ADD,sfd,&ev))//把套接字描述符加入监听队列
    {
        perror("epoll_ctl_add");
        close(sfd);
        exit(-1);
    }
    int ret,i,j,nfd,is_clientfd;
    char buf_comd[128]={0};
    struct sockaddr_in client;
    socklen_t len=sizeof(client);
    while(1)
    {
        nfd=epoll_wait(epfd,events,MAXEVENTS,-1);//开始监听
        for(i=0;i<nfd;i++)
        {
            if(events[i].data.fd==sfd)//如果是新接入的客户机
            {
                int cfd=accept(sfd,(struct sockaddr*)&client,&len);
                if(-1==cfd)//产生一个新的描述符接管客户机
                {
                    perror("accept");
                    close(sfd);
                    exit(-1);
                }
                ev.data.fd=cfd;
                if(epoll_ctl(epfd,EPOLL_CTL_ADD,cfd,&ev))//把新的描述符加入监听队列
                {
                    perror("epoll_ctl_add");
                    close(sfd);
                    exit(-1);
                }
                printf("client: %s,port:%u connected\n",inet_ntoa(client.sin_addr),ntohs(client.sin_port));
            }
            else
            {
                if(events[i].events&EPOLLIN)//如果是其他
                {
                    is_clientfd=1;
                    for(j=0;j<MAXCHILD;j++)
                    {
                        if(child[j].fd==events[i].data.fd)
                        {
                            is_clientfd=0;
                            break;
                        }
                    }
                    if(is_clientfd)//如果是客户端发送请求，
                    {
                        for(j=0;j<MAXCHILD;j++)
                        {
                            if(child[j].busy==0)
                                break;
                        }
                        if(j==MAXCHILD)//如果没有子进程空闲，等待下一次
                            continue;
                        if((ret=recv(events[i].data.fd,buf_comd,128,0))==0)//如果客户端退出
                        {
                            ev.data.fd=events[i].data.fd;
                            if(epoll_ctl(epfd,EPOLL_CTL_DEL,events[i].data.fd,&ev))
                            {
                                perror("epoll_ctl_del");
                                exit(-1);
                            }
                            close(events[i].data.fd);
                            printf("client: %s:%u quit\n",inet_ntoa(client.sin_addr),ntohs(client.sin_port));
                        }
                        else if(-1==ret)
                        {
                            perror("recv");
                            exit(-1);
                        }
                        else//客户端正常请求
                        {
                            child[j].busy=1;
                            child[j].client_fd=events[i].data.fd;
                            ev.data.fd=child[j].fd;
                            epoll_ctl(epfd,EPOLL_CTL_ADD,ev.data.fd,&ev);//把该进程间通信的描述符加入到监控队列
                            send_fd(child[j].fd,events[i].data.fd,buf_comd); //把客户端描述符发送给子进程处理
                            ev.data.fd=child[j].client_fd;
                            epoll_ctl(epfd,EPOLL_CTL_DEL,ev.data.fd,&ev);//把客户端描述符移出监控队列（为了传文件时不受影响）
                        }
                    }
                    else//如果是子进程发来任务完成的信息
                    {
                        recv(child[j].fd,&child[j].busy,sizeof(int),0);
                        printf("process %d finished\n",child[j].pid);
                        ev.data.fd=events[i].data.fd;
                        if(epoll_ctl(epfd,EPOLL_CTL_DEL,events[i].data.fd,&ev))//把进程间描述符移除监控队列
                        {
                            perror("epoll_ctl_del");
                            exit(-1);
                        }
                        ev.data.fd=child[j].client_fd;
                        if(epoll_ctl(epfd,EPOLL_CTL_ADD,child[j].client_fd,&ev))//把客户端描述符添加到监控队列
                        {
                            perror("epoll_ctl_add");
                            exit(-1);
                        }
                    }
                }
            }
        }
    }
}
void make_child(child_t* child)
{
    int i=0;
    for(i=0;i<MAXCHILD;i++)
    {
        int fds[2];
        socketpair(AF_LOCAL,SOCK_STREAM,0,fds);
        int pid=fork();
        if(pid==0)//子进程
        {
            close(fds[0]);
            child_task(fds[1]);
        }
        else if(pid==-1)
        {
            perror("fork");
            exit(-1);
        }
        close(fds[1]);
        child[i].pid=pid;
        child[i].fd=fds[0];
        child[i].busy=0;
    }
}
int init_socket(char* configname)
{
    int sfd=socket(AF_INET,SOCK_STREAM,0);
    if(-1==sfd)
    {
        perror("socket");
        exit(-1);
    }
    FILE *fp=fopen(configname,"r");
    if(NULL==fp)
    {
        perror("fopen");
        close(sfd);
        exit(-1);
    }
    char buf_addr[40]={0};
    fread(buf_addr,1,40,fp);
    fclose(fp);
    char pip[]="IP Address: ";
    char pport[]="Port: ";
    char ip[16]={0};
    char port[7]={0};
    int i=0;
    char *p=strstr(buf_addr,pip)+strlen(pip);
    if(NULL==p)
    {
        printf("config.cfg error\n");
        exit(-1);
    }
    for(i=0;p[i]!='\n';i++)
        ip[i]=p[i];
    printf("ip: %s  ",ip);
    p=strstr(buf_addr,pport)+strlen(pport);
    if(NULL==p)
    {
        printf("config.cfg error\n");
        exit(-1);
    }

    for(i=0;p[i]!='\n';i++)
        port[i]=p[i];
    printf("port: %s\n",port);
    struct sockaddr_in server;
    memset(&server,0,sizeof(server));
    server.sin_family=AF_INET;
    server.sin_addr.s_addr=inet_addr(ip);
    server.sin_port=htons((unsigned short)atoi(port));
    if(bind(sfd,(struct sockaddr*)&server,sizeof(server)))
    {
        perror("bind");
        close(sfd);
        exit(-1);
    }
    if(listen(sfd,10))
    {
        perror("listen");
        close(sfd);
        exit(-1);
    }
    return sfd;
}
void send_fd(int child_fd,int cfd,char *buf_comd)
{
    struct msghdr msg;
    char buf[CMSG_LEN(sizeof(int))];
    struct cmsghdr *cmsg=(struct cmsghdr*)buf;
    cmsg->cmsg_len=CMSG_LEN(sizeof(int));
    cmsg->cmsg_level=SOL_SOCKET;
    cmsg->cmsg_type=SCM_RIGHTS;
    int * fdptr=(int*)CMSG_DATA(cmsg);
    *fdptr=cfd;
    msg.msg_control=buf;
    msg.msg_controllen=sizeof(buf);
    struct iovec iov[1];
    iov[0].iov_base=buf_comd;
    iov[0].iov_len=strlen(buf_comd);
    msg.msg_iov=iov;
    msg.msg_iovlen=1;
    msg.msg_name=NULL;
    msg.msg_namelen=0;
    sendmsg(child_fd,&msg,0);
}
void recv_fd(int p_fd,int *cfd,char *buf_comd)
{
    struct msghdr msg;
    char buf[CMSG_LEN(sizeof(int))];
    struct cmsghdr *cmsg=(struct cmsghdr*)buf;
    cmsg->cmsg_len=CMSG_LEN(sizeof(int));
    cmsg->cmsg_level=SOL_SOCKET;
    cmsg->cmsg_type=SCM_RIGHTS;
    msg.msg_control=buf;
    msg.msg_controllen=sizeof(buf);
    struct iovec iov[1];
    iov[0].iov_base=buf_comd;
    iov[0].iov_len=128;
    msg.msg_iov=iov;
    msg.msg_iovlen=1;
    msg.msg_name=NULL;
    msg.msg_namelen=0;
    recvmsg(p_fd,&msg,0);
    int * fdptr=(int*)CMSG_DATA(cmsg);
    *cfd=*fdptr;
}
void child_task(int p_fd)
{
    int cfd;
    int flag=0;
    while(1)
    {
        char buf_comd[128]={0};
        recv_fd(p_fd,&cfd,buf_comd);
        printf("process %d get command %s\n",getpid(),buf_comd);
        handle_comd(cfd,buf_comd);
        send(p_fd,&flag,sizeof(int),0);
    }
}
void handle_comd(int cfd,char* buf_comd)
{
    if(strncmp(buf_comd,"gets",4)==0)
    {
        char *filename=buf_comd+5;
        func_gets(cfd,filename);
    }
    else if(strncmp(buf_comd,"puts",4)==0)
    {
        char *filename=buf_comd+5;
        func_puts(cfd,filename);
    }
    else
    {
        char *cmd=buf_comd;
        if(strncmp(buf_comd,"cd",2)==0)
        {
            char *dir=buf_comd+3;
            if(chdir(dir))
            {
                MSG msg;
                strcpy(msg.buf,"NO SUCH DIRECTORY!\n");
                msg.len=-1;
                send(cfd,(char*)&msg,strlen(msg.buf)+5,0);
                return ;
            }
            cmd="ls";
        }
        func_shell(cfd,cmd);
    }
}

void func_gets(int cfd,char *filename)
{
    FILE* fp=fopen(filename,"rb");
    MSG msg;
    if(NULL==fp)
    {
        strcpy(msg.buf,"NO SUCH FILE!\n");
        msg.len=-1;
        send(cfd,(char*)&msg,strlen(msg.buf)+5,0);
        return ;
    }
    while(!feof(fp))
    {
        msg.len=fread(msg.buf,1,sizeof(msg.buf),fp);
        send_n(cfd,(char*)&msg,msg.len+4);
    }
    fclose(fp);
}
void func_puts(int cfd,char* filename)
{
    FILE * fp=fopen(filename,"wb");
    if(NULL==fp)
    {
        perror("fopen");
        exit(-1);
    }
    char buf_msg[4096];
    MSG *msg;
    while(1)
    {
        recv_n(cfd,buf_msg,4096);
        msg=(MSG*)buf_msg;
        fwrite(msg->buf,1,msg->len,fp);
        if(msg->len<4092)
            break;
    }
    fflush(fp);
    fclose(fp);
}
void func_shell(int cfd,char* cmd)
{
    FILE* fp=popen(cmd,"r");
    MSG msg;
    if(NULL==fp)
    {
        strcpy(msg.buf,"NO SUCH COMMAND!\n");
        msg.len=-1;
        send(cfd,(char*)&msg,strlen(msg.buf)+5,0);
        pclose(fp);
        return ;
    }
    printf("process %d handl shell command %s",getpid(),cmd);
    int ret;
    while(!feof(fp))
    {
        ret=fread(msg.buf,1,4091,fp);
        msg.buf[ret]=0;
        msg.len=ret;
        send(cfd,(char*)&msg,msg.len+5,0);
    }
    pclose(fp);
}

void sighandler(int signal)
{
    system("pgrep FTP_SERVER |xargs kill -9");
}
void send_n(int cfd,char* p,int len)
{
    int sum,ret;
    sum=0;
    while(sum<len)
    {
        ret=send(cfd,p+sum,len-sum,0);
        sum+=ret;
    }
}
int recv_n(int cfd,char* p,int len)
{
    int sum,ret;
    sum=0;
    MSG *msg;
    int mlen,flag=1;
    while(1)
    {
        ret=recv(cfd,p+sum,len-sum,0);
        sum+=ret;
        if(flag&&sum>4)
        {
            flag=0;
            msg=(MSG*)p;
            mlen=msg->len;
        }
        if(-1==mlen||sum>=mlen)
            break;
    }
    return sum;
}
