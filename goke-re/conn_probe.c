#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
int main(void){
    int fd=socket(AF_UNIX,SOCK_STREAM,0);
    struct sockaddr_un a={.sun_family=AF_UNIX};
    strcpy(a.sun_path,"/data/local/tmp/goke-vdec.sock");
    int r=connect(fd,(void*)&a,sizeof(a));
    printf("connect=%d errno=%d(%s)\n",r,errno,strerror(errno));
    if(r)return 1;
    uint32_t hello[2]={0x474b4849u,36};
    ssize_t w=write(fd,hello,8);
    printf("write=%zd errno=%d(%s)\n",w,errno,strerror(errno));
    uint32_t back[4]={0,0,0,0};
    r=(int)read(fd,back,1);
    printf("read=%d errno=%d(%s)\n",r,errno,strerror(errno));
    return 0;
}
