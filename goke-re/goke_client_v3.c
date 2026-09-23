// feed 4K stream, measure per-frame arrival, save samples
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
static int fd; static int count; static long t0;
static struct timespec ts;
static long ms(void){clock_gettime(CLOCK_MONOTONIC,&ts);return ts.tv_sec*1000+ts.tv_nsec/1000000;}
static int io_full(int fd2,void*p,size_t n,int writeit){uint8_t*b=p;while(n){ssize_t r=writeit?write(fd2,b,n):read(fd2,b,n);if(r<0&&errno==EINTR)continue;if(r<=0)return -1;b+=r;n-=r;}return 0;}
static void*reader(void*unused){(void)unused;static long last=0;while(1){uint32_t h[4];if(io_full(fd,h,16,0))break;if(h[0]!=0x47524631||!h[1]||!h[2]||h[3]!=h[1]*h[2]*4||h[3]>40u*1024*1024)break;uint8_t*buf=malloc(h[3]);if(!buf)break;if(io_full(fd,buf,h[3],0)){free(buf);break;}
    long now=ms()-t0;
    if(count<5||count%30==0)printf("frame %d %ux%u at %ldms (delta %ld)\n",count,h[1],h[2],now,now-last);
    last=now;
    if(count==2||count==30||count==60){char path[128];snprintf(path,sizeof(path),"/data/local/tmp/gk4k_frame_%02d.rgba",count);FILE*f=fopen(path,"wb");if(f){fwrite(buf,1,h[3],f);fclose(f);}}
    count++;free(buf);}
return NULL;}
int main(int argc,char**argv){int codec=argc>1?atoi(argv[1]):36;setbuf(stdout,0);t0=ms();uint32_t hello[2]={0x474b4849,codec};int tries;for(tries=0;tries<6;tries++){if(fd>=0)close(fd);fd=socket(AF_UNIX,SOCK_STREAM,0);struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,"/data/local/tmp/goke-vdec.sock");if(connect(fd,(void*)&a,sizeof(a))){perror("connect");usleep(500000);continue;}signal(SIGPIPE,SIG_IGN);if(io_full(fd,hello,8,1)){usleep(500000);continue;}break;}if(tries==6){printf("connect failed after retries\n");return 1;}pthread_t th;pthread_create(&th,NULL,reader,NULL);
char path[160];sprintf(path,"/data/local/tmp/%s_params.bin",codec==4?"h264":"hevc");FILE*pf=fopen(path,"rb");sprintf(path,"/data/local/tmp/%s_frames.bin",codec==4?"h264":"hevc");FILE*ff=fopen(path,"rb");sprintf(path,"/data/local/tmp/%s_index.bin",codec==4?"h264":"hevc");FILE*ix=fopen(path,"rb");if(!pf||!ff||!ix){printf("no stream files\n");return 1;}
uint8_t*buf=malloc(1u<<20);size_t n=fread(buf,1,1u<<20,pf);uint32_t h[2]={0x474b494e,n};io_full(fd,h,8,1);io_full(fd,buf,n,1);
int total=0;for(;;){uint32_t len;if(fread(&len,4,1,ix)!=1||len>(1u<<20)||fread(buf,1,len,ff)!=len)break;h[1]=len;if(io_full(fd,h,8,1)||io_full(fd,buf,len,1))break;total++;usleep(33000);}
printf("fed %d AUs, draining...\n",total);sleep(15);shutdown(fd,SHUT_RDWR);pthread_join(th,NULL);printf("RESULT outputs=%d\n",count);close(fd);return 0;}
