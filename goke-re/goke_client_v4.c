#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
static int fd;static int count;
static int io_full(int fd,void*p,size_t n,int writeit){uint8_t*b=p;while(n){ssize_t r=writeit?write(fd,b,n):read(fd,b,n);if(r<0&&errno==EINTR)continue;if(r<=0)return -1;b+=r;n-=r;}return 0;}
static void*reader(void*unused){(void)unused;while(1){uint32_t h[6];if(io_full(fd,h,24,0))break;if(h[0]!=0x47524632||!h[1]||!h[2]||h[3]!=h[1]*h[2]||h[4]!=h[3]/2||h[5]!=h[1]||h[3]>4096*2304)break;uint8_t*buf=malloc(h[3]+h[4]);if(!buf)break;if(io_full(fd,buf,h[3]+h[4],0)){free(buf);break;}if(count==0||count==30||count==60||count==80){char path[128];snprintf(path,sizeof(path),"/data/local/tmp/goke_client_v4_frame_%02d.nv21",count);FILE*f=fopen(path,"wb");if(f){fwrite(buf,1,h[3]+h[4],f);fclose(f);}}count++;if(count%15==0)printf("received %d %ux%u\n",count,h[1],h[2]);free(buf);}return NULL;}
int main(int argc,char**argv){int codec=36;const char*prefix=argc>1?argv[1]:"hevc1080";setbuf(stdout,0);fd=socket(AF_UNIX,SOCK_STREAM,0);struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,"/data/local/tmp/goke-vdec.sock");if(connect(fd,(void*)&a,sizeof(a))){perror("connect");return 1;}uint32_t hello[2]={0x474b4849,codec};io_full(fd,hello,8,1);pthread_t th;pthread_create(&th,NULL,reader,NULL);
char path[160];sprintf(path,"/data/local/tmp/%s_params.bin",prefix);FILE*pf=fopen(path,"rb");sprintf(path,"/data/local/tmp/%s_frames.bin",prefix);FILE*ff=fopen(path,"rb");sprintf(path,"/data/local/tmp/%s_index.bin",prefix);FILE*ix=fopen(path,"rb");uint8_t buf[1048576];size_t n=fread(buf,1,sizeof(buf),pf);uint32_t h[2]={0x474b494e,n};io_full(fd,h,8,1);io_full(fd,buf,n,1);for(int i=0;i<90;i++){uint32_t len;if(fread(&len,4,1,ix)!=1||len>sizeof(buf)||fread(buf,1,len,ff)!=len)break;h[1]=len;if(io_full(fd,h,8,1)||io_full(fd,buf,len,1))break;usleep(33000);}sleep(2);shutdown(fd,SHUT_RDWR);pthread_join(th,NULL);printf("RESULT outputs=%d\n",count);close(fd);return 0;}
