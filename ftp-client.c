-#include<stdio.h>
#include<string.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<unistd.h>
#include<stdlib.h>
#define TYPE SOCK_STREAM
#define DOMAIN AF_INET
typedef struct message
{
    int len;
    char buf[4096-sizeof(int)];
}MSG;

void func_gets(int sfd,char *comd);
void func_puts(int sfd,char* comd);
int recv_n(int sfd,char*p,int len);
void send_n(int sfd,char*p,int len);

int main(int argc,char *argv[])
{
    if(argc!=3)
    {
        printf("args err\n");
        exit(-1);
    }
    int sfd=socket(DOMAIN,TYPE,0);
    if(-1==sfd)
    {
        perror("socket");
        exit(-1);
    }
    system("clear");
    struct sockaddr_in dest;
    memset(&dest,0,sizeof(dest));
    dest.sin_family=DOMAIN;
    dest.sin_addr.s_addr=inet_addr(argv[1]);
    dest.sin_port=htons((unsigned short)atoi(argv[2]));
    if(connect(sfd,(struct sockaddr*)&dest,sizeof(dest)))
    {
        perror("connect");
        close(sfd);
        exit(-1);
    }
    printf("connect server %s :%s success\n",argv[1],argv[2]);
    MSG *msg=NULL;
    char buf_msg[4096];
    int ret;
    char comd[128];
    while(memset(comd,0,sizeof(comd)),(ret=read(0,comd,127))>0)
    {
        comd[ret-1]=0;
        if(strncmp(comd,"gets",4)==0)
        {
            func_gets(sfd,comd);
        }
        else if(strncmp(comd,"puts",4)==0)
        {
            func_puts(sfd,comd);
        }            
        else
        {
            send(sfd,comd,strlen(comd)+1,0);
            ret=recv(sfd,buf_msg,sizeof(buf_msg),0);
            if(-1==ret)
            {
                perror("recv");
                close(sfd);
                exit(-1);
            }
            msg=(MSG*)buf_msg;
            if(-1==msg->len)
            {
                printf("%s",msg->buf);
                continue;
            }
            if(strncmp(comd,"ls",2)==0||strncmp(comd,"cd",2)==0)
                system("clear");
            while(1)
            {
                printf("%s",msg->buf);
                if(msg->len<4091)
                    break;
                ret=recv(sfd,buf_msg,sizeof(buf_msg),0);
                if(-1==ret)
                {
                    perror("recv");
                    close(sfd);
                    exit(-1);
                }
                msg=(MSG*)buf_msg;
            }
        }
    }
    close(sfd);
    exit(0);
}
void func_gets(int sfd,char *comd)
{
    char *filename=comd+5;
    FILE * fp=fopen(filename,"wb");
    if(NULL==fp)
    {
        perror("fopen");
        close(sfd);
        exit(-1);
    }
    MSG* msg=NULL;
    char buf_msg[4096];
    send(sfd,comd,strlen(comd)+1,0);
    int ret;
    while(1)
    {
        ret=recv_n(sfd,buf_msg,4096);
        msg=(MSG*)buf_msg;
        if(-1==msg->len)//-1表示接受到的是提示信息，直接打印
        {
            printf("%s",msg->buf);
            fclose(fp);
            unlink(filename);
            return;
        }
        fwrite(msg->buf,1,msg->len,fp);
        if(msg->len<4092)
            break;
    }
    fflush(fp);
    fclose(fp);
    printf("download success\n");
}
void func_puts(int sfd,char *comd)
{
    char* filename=comd+5;
    FILE* fp=fopen(filename,"rb");
    if(NULL==fp)
    {
        printf("NO SUCH FILE\n");
        return ;
    }
    send(sfd,comd,strlen(comd)+1,0);
    MSG msg;
    while(!feof(fp))
    {
        msg.len=fread(msg.buf,1,4092,fp);
        send_n(sfd,(char*)&msg,msg.len+4);
    }
    fclose(fp);
    printf("upload success\n");
}
void send_n(int sfd,char* p,int len)
{
    int sum,ret;
    sum=0;
    while(sum<len)
    {
        ret=send(sfd,p+sum,len-sum,0);
        sum+=ret;
    }
}
int recv_n(int sfd,char* p,int len)
{
    int sum=0,ret;
    MSG *msg;
    int mlen,flag=1;
    while(1)
    {
        ret=recv(sfd,p+sum,len-sum,0);
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
