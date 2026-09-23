// GK6323 private decoder bridge for RustDesk. Protocol is little-endian u32.
// hello: [0x474b4849, codec(4=AVC,36=HEVC)], input: [0x474b494e,len] + Annex-B AU.
// output: [0x47524631,width,height,rgba_len] + tightly packed RGBA pixels.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define PATH "/data/local/tmp/goke-vdec.sock"
#define GOKE_MAX_INPUT (16u * 1024u * 1024u)
#define MAX_WIDTH 4096u
#define MAX_HEIGHT 2304u
#define SYM(n) do { *(void**)(&n) = dlsym(lib, #n); if (!n) { fprintf(stderr,"missing %s: %s\n",#n,dlerror()); return -1; } } while (0)

typedef struct {
    int (*sys_init)(void), (*av_init)(void), (*av_deinit)(void);
    int (*get_default)(void*,int), (*create)(void*,uint32_t*), (*destroy)(uint32_t);
    int (*open)(uint32_t,int,void*), (*close)(uint32_t,int);
    int (*set_attr)(uint32_t,int,void*),(*get_attr)(uint32_t,int,void*), (*get_handles)(uint32_t,void*,void*);
    int (*start)(uint32_t),(*stop)(uint32_t);
    int (*get_buf)(uint32_t,uint32_t,void*),(*put_buf)(uint32_t,void*);
    int (*recv_frame)(uint32_t,void*),(*release_frame)(uint32_t,void*);
    void* (*mmz_map)(uint32_t,int);
    int (*mmz_unmap)(void*);
    int (*nv21_to_argb)(const uint8_t*,int,const uint8_t*,int,uint8_t*,int,int,int);
    int (*argb_to_abgr)(const uint8_t*,int,uint8_t*,int,int,int);
} Api;
static Api a;
static volatile sig_atomic_t running=1;
static void signal_stop(int sig) { (void)sig; running=0; }
static int load_api(void) {
    void *lib=dlopen("/vendor/lib/libgk_msp.so",RTLD_NOW|RTLD_GLOBAL);
    if(!lib){fprintf(stderr,"dlopen: %s\n",dlerror());return -1;}
    int (*GK_SYS_Init)(void),(*GK_API_AVPLAY_Init)(void),(*GK_API_AVPLAY_DeInit)(void);
    int (*GK_API_AVPLAY_GetDefaultConfig)(void*,int),(*GK_API_AVPLAY_Create)(void*,uint32_t*);
    int (*GK_API_AVPLAY_Destroy)(uint32_t),(*GK_API_AVPLAY_ChnOpen)(uint32_t,int,void*);
    int (*GK_API_AVPLAY_ChnClose)(uint32_t,int),(*GK_API_AVPLAY_SetAttr)(uint32_t,int,void*),(*GK_API_AVPLAY_GetAttr)(uint32_t,int,void*);
    int (*mpi_avplay_get_sync_vdec_handle)(uint32_t,void*,void*);
    int (*mpi_vdec_chan_start)(uint32_t),(*mpi_vdec_chan_stop)(uint32_t);
    int (*mpi_vdec_chan_get_buffer)(uint32_t,uint32_t,void*),(*mpi_vdec_chan_put_buffer)(uint32_t,void*);
    int (*mpi_vdec_chan_recv_frm)(uint32_t,void*),(*mpi_vdec_chan_rls_frm)(uint32_t,void*);
    void* (*MPI_MMZ_Map)(uint32_t,int); int (*MPI_MMZ_Unmap)(void*);
    SYM(GK_SYS_Init);SYM(GK_API_AVPLAY_Init);SYM(GK_API_AVPLAY_DeInit);
    SYM(GK_API_AVPLAY_GetDefaultConfig);SYM(GK_API_AVPLAY_Create);SYM(GK_API_AVPLAY_Destroy);
    SYM(GK_API_AVPLAY_ChnOpen);SYM(GK_API_AVPLAY_ChnClose);SYM(GK_API_AVPLAY_SetAttr);SYM(GK_API_AVPLAY_GetAttr);
    SYM(mpi_avplay_get_sync_vdec_handle);SYM(mpi_vdec_chan_start);SYM(mpi_vdec_chan_stop);
    SYM(mpi_vdec_chan_get_buffer);SYM(mpi_vdec_chan_put_buffer);
    SYM(mpi_vdec_chan_recv_frm);SYM(mpi_vdec_chan_rls_frm);SYM(MPI_MMZ_Map);SYM(MPI_MMZ_Unmap);
    void *yuv=dlopen("/system/lib/libyuv.so",RTLD_NOW|RTLD_LOCAL);
    if(!yuv){fprintf(stderr,"libyuv: %s\n",dlerror());return -1;}
    void *nv21=dlsym(yuv,"NV21ToARGB"),*swap=dlsym(yuv,"ARGBToABGR");
    if(!nv21||!swap)return -1;
    a=(Api){GK_SYS_Init,GK_API_AVPLAY_Init,GK_API_AVPLAY_DeInit,GK_API_AVPLAY_GetDefaultConfig,
        GK_API_AVPLAY_Create,GK_API_AVPLAY_Destroy,GK_API_AVPLAY_ChnOpen,GK_API_AVPLAY_ChnClose,
        GK_API_AVPLAY_SetAttr,GK_API_AVPLAY_GetAttr,mpi_avplay_get_sync_vdec_handle,mpi_vdec_chan_start,mpi_vdec_chan_stop,
        mpi_vdec_chan_get_buffer,mpi_vdec_chan_put_buffer,mpi_vdec_chan_recv_frm,mpi_vdec_chan_rls_frm,
        MPI_MMZ_Map,MPI_MMZ_Unmap,nv21,swap};
    return 0;
}
static int read_full(int fd,void *buf,size_t n){uint8_t*p=buf;while(n){ssize_t r=read(fd,p,n);if(r<0&&errno==EINTR)continue;if(r<=0)return -1;p+=r;n-=r;}return 0;}
static int write_full(int fd,const void *buf,size_t n){const uint8_t*p=buf;while(n){ssize_t r=write(fd,p,n);if(r<0&&errno==EINTR)continue;if(r<=0)return -1;p+=r;n-=r;}return 0;}
static void log_hevc_nals(const uint8_t *data,uint32_t len,uint32_t index){
    fprintf(stderr,"input[%u] len=%u nal=",index,len);
    unsigned count=0;
    for(uint32_t i=0;i+5<len&&count<20;i++){
        if(data[i]||data[i+1]||data[i+2]!=1)continue;
        fprintf(stderr,"%s%u",count?",":"",(data[i+3]>>1)&63u);
        count++;i+=3;
    }
    if(!count)fprintf(stderr,"none");
    fputc('\n',stderr);
}
typedef struct {uint8_t *ylin,*vulin,*yscale,*vuscale,*argb;} Scratch;
static int rgba_from_frame(const uint32_t*f,uint8_t*dst,Scratch*sc,uint32_t*out_w,uint32_t*out_h) {
    uint32_t w=f[71],h=f[72],stride=f[6];
    if(!w||!h||w>MAX_WIDTH||h>MAX_HEIGHT||stride<w||stride%64||stride>MAX_WIDTH)return -1;
    uint32_t ow=w,oh=h;
    if(ow>1280||oh>800){
        if((uint64_t)w*800>(uint64_t)h*1280){ow=1280;oh=((uint64_t)h*ow/w)&~1u;}
        else{oh=800;ow=((uint64_t)w*oh/h)&~1u;}
    }
    if(ow<2||oh<2)return -1;
    uint8_t*y=a.mmz_map(f[2],0),*vu=a.mmz_map(f[7],0);
    if(!y||!vu){if(y)a.mmz_unmap(y);if(vu)a.mmz_unmap(vu);return -1;}
    uint32_t tiles=stride/64;
    for(uint32_t row=0;row<h;row++){
        for(uint32_t tx=0;tx<(w+63)/64;tx++){
            uint32_t n=w-tx*64;if(n>64)n=64;
            uint32_t src=((row/16)*tiles+tx)*1024+(row%16)*64;
            memcpy(sc->ylin+row*w+tx*64,y+src,n);
        }
    }
    for(uint32_t row=0;row<h/2;row++){
        for(uint32_t tx=0;tx<(w+63)/64;tx++){
            uint32_t n=w-tx*64;if(n>64)n=64;
            uint32_t src=((row/8)*tiles+tx)*512+(row%8)*64;
            memcpy(sc->vulin+row*w+tx*64,vu+src,n);
        }
    }
    a.mmz_unmap(vu);a.mmz_unmap(y);
    const uint8_t *yinput=sc->ylin,*vuinput=sc->vulin;
    if(ow!=w||oh!=h){
        uint16_t xmap[1280],uvmap[640];
        for(uint32_t col=0;col<ow;col++)xmap[col]=(uint32_t)col*w/ow;
        for(uint32_t col=0;col<ow/2;col++)uvmap[col]=((uint32_t)(col*2)*w/ow)&~1u;
        for(uint32_t row=0;row<oh;row++){
            uint32_t sy=row*h/oh;
            const uint8_t *src=sc->ylin+sy*w;
            uint8_t *dstrow=sc->yscale+row*ow;
            for(uint32_t col=0;col<ow;col++){
                dstrow[col]=src[xmap[col]];
            }
        }
        for(uint32_t row=0;row<oh/2;row++){
            uint32_t sy=row*h/oh;
            const uint8_t *src=sc->vulin+sy*w;
            uint8_t *dstrow=sc->vuscale+row*ow;
            for(uint32_t col=0;col<ow/2;col++){
                uint32_t sx=uvmap[col];
                dstrow[col*2]=src[sx];
                dstrow[col*2+1]=src[sx+1];
            }
        }
        yinput=sc->yscale;vuinput=sc->vuscale;
    }
    int r=a.nv21_to_argb(yinput,(int)ow,vuinput,(int)ow,sc->argb,(int)ow*4,(int)ow,(int)oh);
    if(r)return r;
    r=a.argb_to_abgr(sc->argb,(int)ow*4,dst,(int)ow*4,(int)ow,(int)oh);
    *out_w=ow;*out_h=oh;
    return r;
}
typedef struct {int fd;uint32_t vdec;volatile int stop;uint32_t frames;uint32_t fails;} Drain;
static void* drain_loop(void*arg){Drain*d=arg;Scratch sc={malloc(MAX_WIDTH*MAX_HEIGHT),malloc(MAX_WIDTH*MAX_HEIGHT/2),malloc(1280*800),malloc(1280*800/2),malloc(MAX_WIDTH*MAX_HEIGHT*4u)};
    uint8_t *rgba=malloc(MAX_WIDTH*MAX_HEIGHT*4u);
    if(!rgba||!sc.ylin||!sc.vulin||!sc.yscale||!sc.vuscale||!sc.argb){d->stop=1;free(rgba);free(sc.ylin);free(sc.vulin);free(sc.yscale);free(sc.vuscale);free(sc.argb);return NULL;}
    uint32_t last_w=0,last_h=0,last_stride=0;
    while(!d->stop&&running){uint32_t f[1024]={0};int r=a.recv_frame(d->vdec,f);
        if(r){usleep(4000);continue;}
        uint32_t w=f[71],h=f[72],ow=0,oh=0;int okay=w>0&&h>0&&w<=MAX_WIDTH&&h<=MAX_HEIGHT;
        if(w!=last_w||h!=last_h||f[6]!=last_stride){
            fprintf(stderr,"frame source=%ux%u stride=%u y=%x vu=%x flags=%x %x %x %x %x\n",
                w,h,f[6],f[2],f[7],f[0],f[1],f[3],f[4],f[5]);
            last_w=w;last_h=h;last_stride=f[6];
        }
        if(d->frames==0){
            for(uint32_t i=0;i<96;i+=8){
                fprintf(stderr,"meta[%02u] %08x %08x %08x %08x %08x %08x %08x %08x\n",
                    i,f[i],f[i+1],f[i+2],f[i+3],f[i+4],f[i+5],f[i+6],f[i+7]);
            }
        }
        if(okay&&rgba_from_frame(f,rgba,&sc,&ow,&oh)==0){uint32_t head[4]={0x47524631u,ow,oh,ow*oh*4u};
            if(write_full(d->fd,head,sizeof(head))||write_full(d->fd,rgba,head[3]))d->stop=1;
            else d->frames++;
        }else d->fails++;
        a.release_frame(d->vdec,f);
    }
    free(rgba);free(sc.ylin);free(sc.vulin);free(sc.yscale);free(sc.vuscale);free(sc.argb);return NULL;
}
static int session(int fd,uint32_t codec){
    uint32_t cfg[256]={0},attr[256]={0},h=~0u,v=~0u,s=~0u;int opened=0,started=0,created=0,rc=-1;
    if(a.get_default(cfg,1)||a.create(cfg,&h))goto out;created=1;
    if(a.open(h,2,NULL))goto out;opened=1;
    if(a.get_attr(h,2,attr))goto out;attr[0]=codec;
    if(a.set_attr(h,2,attr)||a.get_handles(h,&v,&s)||a.start(v))goto out;started=1;
    fprintf(stderr,"session codec=%u avplay=%x vdec=%x\n",codec,h,v);
    Drain d={.fd=fd,.vdec=v};pthread_t th;
    if(pthread_create(&th,NULL,drain_loop,&d))goto out;
    uint8_t *data=malloc(GOKE_MAX_INPUT);if(!data){d.stop=1;pthread_join(th,NULL);goto out;}
    uint32_t in=0;
    while(running&&!d.stop){uint32_t head[2];if(read_full(fd,head,sizeof(head)))break;
        if(head[0]!=0x474b494eu||!head[1]||head[1]>GOKE_MAX_INPUT)break;
        if(read_full(fd,data,head[1]))break;
        if(codec==36&&in<8)log_hevc_nals(data,head[1],in);
        uint32_t buf[64]={0};int r=a.get_buf(v,head[1],buf);
        if(r||!buf[0]||buf[2]<head[1]){fprintf(stderr,"getbuf %x cap=%u\n",r,buf[2]);break;}
        memcpy((void*)(uintptr_t)buf[0],data,head[1]);buf[2]=head[1];buf[4]=in*33;buf[6]=1;
        r=a.put_buf(v,buf);if(r){fprintf(stderr,"putbuf %x\n",r);break;}
        in++;
    }
    free(data);d.stop=1;shutdown(fd,SHUT_RDWR);pthread_join(th,NULL);
    fprintf(stderr,"session done input=%u output=%u mapfail=%u\n",in,d.frames,d.fails);rc=0;
out:
    if(started)a.stop(v);if(opened)a.close(h,2);if(created)a.destroy(h);
    return rc;
}
int main(void){setbuf(stderr,NULL);signal(SIGPIPE,SIG_IGN);signal(SIGTERM,signal_stop);signal(SIGINT,signal_stop);
    if(load_api()||a.sys_init()||a.av_init())return 1;
    struct stat st;if(stat("/data/data/com.carriez.flutter_hbb",&st)){perror("app uid");return 1;}
    int sock=socket(AF_UNIX,SOCK_STREAM,0);if(sock<0)return 1;
    struct sockaddr_un addr={.sun_family=AF_UNIX};strncpy(addr.sun_path,PATH,sizeof(addr.sun_path)-1);
    unlink(PATH);if(bind(sock,(struct sockaddr*)&addr,sizeof(addr))||chmod(PATH,0666)||listen(sock,1)){perror("bind/listen");return 1;}
    fprintf(stderr,"listening %s app_uid=%u\n",PATH,st.st_uid);
    while(running){int fd=accept(sock,NULL,NULL);if(fd<0){if(errno==EINTR)continue;break;}
        struct ucred peer;socklen_t n=sizeof(peer);
        if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&n)|| (peer.uid!=st.st_uid&&peer.uid!=0)){
            fprintf(stderr,"rejected uid=%u\n",peer.uid);close(fd);continue;
        }
        uint32_t hello[2];if(!read_full(fd,hello,sizeof(hello))&&hello[0]==0x474b4849u&&
            (hello[1]==4||hello[1]==36)){session(fd,hello[1]);}
        close(fd);
    }
    close(sock);unlink(PATH);a.av_deinit();return 0;
}
