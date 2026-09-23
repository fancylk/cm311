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
#define MMZ_CACHED 1 /* try cached mapping: MPI_MMZ_Map(phys, cached?) */
#define GOKE_MAX_INPUT (16u * 1024u * 1024u)
#define MAX_WIDTH 4096u
#define MAX_HEIGHT 2304u
#define MAX_OUTPUT_WIDTH 1920u
#define MAX_OUTPUT_HEIGHT 1080u
#define SYM(n) do { *(void**)(&n) = dlsym(lib, #n); if (!n) { fprintf(stderr,"missing %s: %s\n",#n,dlerror()); return -1; } } while (0)

typedef struct {
    int (*sys_init)(void), (*av_init)(void), (*av_deinit)(void);
    int (*get_default)(void*,int), (*create)(void*,uint32_t*), (*destroy)(uint32_t);
    int (*open)(uint32_t,int,void*), (*close)(uint32_t,int);
    int (*set_attr)(uint32_t,int,void*),(*get_attr)(uint32_t,int,void*), (*get_handles)(uint32_t,void*,void*);
    int (*start)(uint32_t),(*stop)(uint32_t);
    int (*get_buf)(uint32_t,uint32_t,void*),(*put_buf)(uint32_t,void*);
    int (*recv_frame)(uint32_t,void*),(*release_frame)(uint32_t,void*);
    int (*get_pack_type)(uint32_t,int*),(*set_pack_type)(uint32_t,int);
    int (*get_chan_attr)(uint32_t,void*),(*set_chan_attr)(uint32_t,void*);
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
    int (*mpi_vdec_get_chan_frm_pack_type)(uint32_t,int*),(*mpi_vdec_set_chan_frm_pack_type)(uint32_t,int);
    int (*mpi_vdec_get_chan_attr)(uint32_t,void*),(*mpi_vdec_set_chan_attr)(uint32_t,void*);
    void* (*MPI_MMZ_Map)(uint32_t,int); int (*MPI_MMZ_Unmap)(void*);
    SYM(GK_SYS_Init);SYM(GK_API_AVPLAY_Init);SYM(GK_API_AVPLAY_DeInit);
    SYM(GK_API_AVPLAY_GetDefaultConfig);SYM(GK_API_AVPLAY_Create);SYM(GK_API_AVPLAY_Destroy);
    SYM(GK_API_AVPLAY_ChnOpen);SYM(GK_API_AVPLAY_ChnClose);SYM(GK_API_AVPLAY_SetAttr);SYM(GK_API_AVPLAY_GetAttr);
    SYM(mpi_avplay_get_sync_vdec_handle);SYM(mpi_vdec_chan_start);SYM(mpi_vdec_chan_stop);
    SYM(mpi_vdec_chan_get_buffer);SYM(mpi_vdec_chan_put_buffer);
    SYM(mpi_vdec_chan_recv_frm);SYM(mpi_vdec_chan_rls_frm);SYM(MPI_MMZ_Map);SYM(MPI_MMZ_Unmap);
    SYM(mpi_vdec_get_chan_frm_pack_type);SYM(mpi_vdec_set_chan_frm_pack_type);
    SYM(mpi_vdec_get_chan_attr);SYM(mpi_vdec_set_chan_attr);
    void *yuv=dlopen("/system/lib/libyuv.so",RTLD_NOW|RTLD_LOCAL);
    if(!yuv){fprintf(stderr,"libyuv: %s\n",dlerror());return -1;}
    void *nv21=dlsym(yuv,"NV21ToARGB"),*swap=dlsym(yuv,"ARGBToABGR");
    if(!nv21||!swap)return -1;
    a=(Api){GK_SYS_Init,GK_API_AVPLAY_Init,GK_API_AVPLAY_DeInit,GK_API_AVPLAY_GetDefaultConfig,
        GK_API_AVPLAY_Create,GK_API_AVPLAY_Destroy,GK_API_AVPLAY_ChnOpen,GK_API_AVPLAY_ChnClose,
        GK_API_AVPLAY_SetAttr,GK_API_AVPLAY_GetAttr,mpi_avplay_get_sync_vdec_handle,mpi_vdec_chan_start,mpi_vdec_chan_stop,
        mpi_vdec_chan_get_buffer,mpi_vdec_chan_put_buffer,mpi_vdec_chan_recv_frm,mpi_vdec_chan_rls_frm,
        mpi_vdec_get_chan_frm_pack_type,mpi_vdec_set_chan_frm_pack_type,
        mpi_vdec_get_chan_attr,mpi_vdec_set_chan_attr,
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
/* per-frame MMZ map/unmap of multi-MB regions costs ~100ms; cache hot maps
   keyed by physical address (vdec DPB reuses the same few buffers). */
#define MAPCACHE 8
static struct {uint32_t phys;uint8_t *ptr;} map_cache[MAPCACHE];
static int map_cache_n=0;
static uint8_t *map_get(uint32_t phys){
    for(int i=0;i<map_cache_n;i++)if(map_cache[i].phys==phys)return map_cache[i].ptr;
    uint8_t *p=a.mmz_map(phys,MMZ_CACHED);
    if(!p)return NULL;
    if(map_cache_n<MAPCACHE){map_cache[map_cache_n].phys=phys;map_cache[map_cache_n].ptr=p;map_cache_n++;}
    else{a.mmz_unmap(map_cache[0].ptr);map_cache[0].phys=phys;map_cache[0].ptr=p;}
    return p;
}
static int nv21_from_frame(const uint32_t*f,Scratch*sc,uint32_t*out_w,uint32_t*out_h,const uint8_t**out_y,const uint8_t**out_vu) {
    uint32_t w=f[71],h=f[72],stride=f[6];
    if(!w||!h||w>MAX_WIDTH||h>MAX_HEIGHT||stride<w||stride%64||stride>MAX_WIDTH)return -1;
    uint32_t ow=w,oh=h;
    if(ow>MAX_OUTPUT_WIDTH||oh>MAX_OUTPUT_HEIGHT){
        if((uint64_t)w*MAX_OUTPUT_HEIGHT>(uint64_t)h*MAX_OUTPUT_WIDTH){ow=MAX_OUTPUT_WIDTH;oh=((uint64_t)h*ow/w)&~1u;}
        else{oh=MAX_OUTPUT_HEIGHT;ow=((uint64_t)w*oh/h)&~1u;}
    }
    if(ow<2||oh<2)return -1;
    /* v2: f[3]/f[8] are the pixel-data addresses; on 4K streams they sit at
       f[17]=0x8800 / +0x4400 inside the mapped buffers (header region before
       pixels). Read via the offset; fall back to the buffer start. */
    uint32_t yoff=f[3]&&f[3]>f[2]&&f[3]-f[2]<0x100000?f[3]-f[2]:0;
    uint32_t vuoff=f[8]&&f[8]>f[7]&&f[8]-f[7]<0x100000?f[8]-f[7]:0;
    uint8_t*y=map_get(f[2]),*vu=map_get(f[7]);
    if(!y||!vu){return -1;}
    uint32_t tiles=stride/64;
    const uint8_t *yinput=sc->ylin,*vuinput=sc->vulin;
    if(ow!=w||oh!=h){
        /* fused sample-at-read: for downscaled output, read only the source
           rows actually sampled and extract scaled pixels per 64B tile row —
           cuts uncached MMZ reads ~4x vs full untile at 4K. */
        uint16_t xmap[MAX_OUTPUT_WIDTH],uvmap[MAX_OUTPUT_WIDTH/2];
        for(uint32_t col=0;col<ow;col++)xmap[col]=(uint32_t)col*w/ow;
        for(uint32_t col=0;col<ow/2;col++)uvmap[col]=((uint32_t)(col*2)*w/ow)&~1u;
        for(uint32_t row=0;row<oh;row++){
            uint32_t sy=row*h/oh;
            uint32_t band=sy/16,ty=sy%16;
            uint8_t *dstrow=sc->yscale+row*ow;
            uint32_t emitted=0;
            for(uint32_t tx=0;tx<tiles&&emitted<ow;tx++){
                const uint8_t*t=y+yoff+((band*tiles+tx)*1024)+ty*64;
                for(uint32_t k=0;k<64&&emitted<ow;k++){
                    uint32_t sx=tx*64+k;
                    while(emitted<ow&&xmap[emitted]<sx){emitted++;}
                    if(emitted<ow&&xmap[emitted]==sx)dstrow[emitted++]=t[k];
                }
            }
        }
        uint32_t uvcols=ow/2;
        for(uint32_t row=0;row<oh/2;row++){
            uint32_t sy=row*h/oh;
            uint32_t band=sy/8,ty=sy%8;
            uint8_t *dstrow=sc->vuscale+row*ow;
            uint32_t emitted=0;
            for(uint32_t tx=0;tx<tiles&&emitted<uvcols;tx++){
                const uint8_t*t=vu+vuoff+((band*tiles+tx)*512)+ty*64;
                for(uint32_t k=0;k<64&&emitted<uvcols;k+=2){
                    uint32_t sx=tx*64+k;
                    while(emitted<uvcols&&uvmap[emitted]<sx){emitted++;}
                    if(emitted<uvcols&&uvmap[emitted]==sx){dstrow[emitted*2]=t[k];dstrow[emitted*2+1]=t[k+1];emitted++;}
                }
            }
        }
        yinput=sc->yscale;vuinput=sc->vuscale;
    }else{
    for(uint32_t row=0;row<h;row++){
        for(uint32_t tx=0;tx<(w+63)/64;tx++){
            uint32_t n=w-tx*64;if(n>64)n=64;
            uint32_t src=((row/16)*tiles+tx)*1024+(row%16)*64;
            memcpy(sc->ylin+row*w+tx*64,y+yoff+src,n);
        }
    }
    for(uint32_t row=0;row<h/2;row++){
        for(uint32_t tx=0;tx<(w+63)/64;tx++){
            uint32_t n=w-tx*64;if(n>64)n=64;
            uint32_t src=((row/8)*tiles+tx)*512+(row%8)*64;
            memcpy(sc->vulin+row*w+tx*64,vu+vuoff+src,n);
        }
    }
    yinput=sc->ylin;vuinput=sc->vulin;
    }
    /* PATCH(nv21): send NV21 planes; the app converts (NEON) into its own
       Surface buffer — 2.8MB/frame over the socket instead of 7.5MB RGBA. */
    *out_w=ow;*out_h=oh;
    *out_y=yinput;*out_vu=vuinput;
    return 0;
}
typedef struct {int fd;uint32_t vdec;volatile int stop;uint32_t frames;uint32_t fails;} Drain;
static void* drain_loop(void*arg){Drain*d=arg;Scratch sc={malloc(MAX_WIDTH*MAX_HEIGHT),malloc(MAX_WIDTH*MAX_HEIGHT/2),malloc(MAX_OUTPUT_WIDTH*MAX_OUTPUT_HEIGHT),malloc(MAX_OUTPUT_WIDTH*MAX_OUTPUT_HEIGHT/2),malloc(MAX_WIDTH*MAX_HEIGHT*4u)};
    uint8_t *rgba=malloc(MAX_WIDTH*MAX_HEIGHT*4u);
    if(!rgba||!sc.ylin||!sc.vulin||!sc.yscale||!sc.vuscale||!sc.argb){d->stop=1;free(rgba);free(sc.ylin);free(sc.vulin);free(sc.yscale);free(sc.vuscale);free(sc.argb);return NULL;}
    uint32_t last_w=0,last_h=0,last_stride=0;
    struct timespec T[6];
    #define TICKS(i) clock_gettime(CLOCK_MONOTONIC,&T[i])
    #define MSD(a,b) ((T[b].tv_sec-T[a].tv_sec)*1000+(T[b].tv_nsec-T[a].tv_nsec)/1000000)
    while(!d->stop&&running){uint32_t f[1024]={0};
        TICKS(0);
        int r=a.recv_frame(d->vdec,f);
        if(r){usleep(4000);continue;}
        TICKS(1);
        uint32_t w=f[71],h=f[72],ow=0,oh=0;int okay=w>0&&h>0&&w<=MAX_WIDTH&&h<=MAX_HEIGHT;
        /* This decoder returns corrupt or compressed pixels above 1080p.
           Close the bridge so RustDesk can fall back to its working VP9 path. */
        if(w>1920u||h>1080u||(f[16]&0x20u)){
            fprintf(stderr,"unsupported output %ux%u flag=%x; closing bridge for fallback\n",w,h,f[16]);
            a.release_frame(d->vdec,f);
            d->stop=1;
            shutdown(d->fd,SHUT_RDWR);
            break;
        }
        if(w!=last_w||h!=last_h||f[6]!=last_stride){
            fprintf(stderr,"frame source=%ux%u stride=%u y=%x+%x vu=%x+%x flag=%x hdrlen=%x\n",
                w,h,f[6],f[2],f[3]-f[2],f[7],f[8]-f[7],f[16],f[17]);
            last_w=w;last_h=h;last_stride=f[6];
        }
        if(d->frames==0&&f[16]==0x20&&access("/data/local/tmp/dump_cmp",0)==0){
            uint8_t*y=map_get(f[2]),*vu=map_get(f[7]);
            if(y&&vu){
                FILE*fy=fopen("/data/local/tmp/cmp_y.bin","wb");
                if(fy){fwrite(y,1,0x8800u+f[6]*h*2,fy);fclose(fy);}
                FILE*fv=fopen("/data/local/tmp/cmp_vu.bin","wb");
                if(fv){fwrite(vu,1,0x4400u+f[6]*h,fv);fclose(fv);}
                fprintf(stderr,"dumped cmp frame y=%x vu=%x stride=%u h=%u\n",f[2],f[7],f[6],h);
            }
        }
        if(d->frames==0){
            for(uint32_t i=0;i<96;i+=8){
                fprintf(stderr,"meta[%02u] %08x %08x %08x %08x %08x %08x %08x %08x\n",
                    i,f[i],f[i+1],f[i+2],f[i+3],f[i+4],f[i+5],f[i+6],f[i+7]);
            }
        }
        TICKS(2);
        const uint8_t *yp=NULL,*vup=NULL; 
        int rr=-1;
        if(okay)rr=nv21_from_frame(f,&sc,&ow,&oh,&yp,&vup);
        TICKS(3);
        if(okay&&rr==0){uint32_t head[6]={0x47524632u,ow,oh,ow*oh,ow*oh/2u,ow};
            if(write_full(d->fd,head,sizeof(head))||write_full(d->fd,yp,ow*oh)||write_full(d->fd,vup,ow*oh/2))d->stop=1;
            else d->frames++;
        }else d->fails++;
        TICKS(4);
        if(d->frames<8)fprintf(stderr,"t recv=%ld nv21=%ld send=%ld rr=%d\n",MSD(0,1),MSD(2,3),MSD(3,4),rr);
        a.release_frame(d->vdec,f);
    }
    free(rgba);free(sc.ylin);free(sc.vulin);free(sc.yscale);free(sc.vuscale);free(sc.argb);return NULL;
}
static int session(int fd,uint32_t codec){
    uint32_t cfg[256]={0},attr[256]={0},h=~0u,v=~0u,s=~0u;int opened=0,started=0,created=0,rc=-1;
    if(a.get_default(cfg,1))goto out;
    /* NOTE: cfg[2]=0x4000000 (64MB video pool) HANGS AVPLAY_Create on this box —
       mmz pool too small. Keep the 16MB default. */
    if(a.create(cfg,&h))goto out;created=1;
    if(a.open(h,2,NULL))goto out;opened=1;
    if(a.get_attr(h,2,attr))goto out;attr[0]=codec;
    if(a.set_attr(h,2,attr)||a.get_handles(h,&v,&s)||a.start(v))goto out;started=1;
    /* the API may write more than an int (struct); give it a generous buffer */
    int packbuf[64];
    memset(packbuf,0,sizeof(packbuf));
    if(a.get_pack_type){if(a.get_pack_type(v,packbuf))fprintf(stderr,"get_pack_type failed\n");else fprintf(stderr,"pack_type=%d/%d/%d/%d\n",packbuf[0],packbuf[1],packbuf[2],packbuf[3]);}
    {
        unsigned int cattr[128];
        memset(cattr,0,sizeof(cattr));
        if(!a.get_chan_attr(v,cattr)){
            fprintf(stderr,"chan_attr:");
            for(int i=0;i<40;i++)fprintf(stderr," %x",cattr[i]);
            fprintf(stderr,"\n");
        } else fprintf(stderr,"get_chan_attr failed\n");
    }
    FILE *pf=fopen("/data/local/tmp/gk_pack.cfg","r");
    if(pf){int pv=-999;fscanf(pf,"%d",&pv);fclose(pf);
        if(pv!=-999&&a.set_pack_type){int sr=a.set_pack_type(v,pv);fprintf(stderr,"set_pack_type(%d)=%d\n",pv,sr);
        memset(packbuf,0,sizeof(packbuf));
        if(!a.get_pack_type(v,packbuf))fprintf(stderr,"pack_type now %d/%d\n",packbuf[0],packbuf[1]);}}
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
        fprintf(stderr,"accepted fd=%d\n",fd);
        struct ucred peer;socklen_t n=sizeof(peer);
        if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&n)|| (peer.uid!=st.st_uid&&peer.uid!=0)){
            fprintf(stderr,"rejected uid=%u\n",peer.uid);close(fd);continue;
        }
        fprintf(stderr,"peer uid=%u\n",peer.uid);
        uint32_t hello[2]={0,0};
        struct timeval tv={3,0};
        setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
        int hr=read_full(fd,hello,sizeof(hello));
        tv=(struct timeval){0,0};
        setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv)); /* hello needs a timeout; the session must not (static screens idle >3s) */
        if(hr)fprintf(stderr,"hello read failed r=%d\n",hr);
        else if(hello[0]!=0x474b4849u||(hello[1]!=4&&hello[1]!=36))fprintf(stderr,"bad hello %08x %08x\n",hello[0],hello[1]);
        else session(fd,hello[1]);
        close(fd);
    }
    close(sock);unlink(PATH);a.av_deinit();return 0;
}
