/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
Compile in rpi-rgb-led-matrix directory.
make  # compile library
g++ -Wall -O3 -g -Iinclude simple-udp.cc -o simple-udp -Llib -lrgbmatrix -lrt -lm -lpthread
*/

#define VALTAVAMATRIISI

#include <errno.h>
#include <unistd.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/syscall.h>
       #include <sys/resource.h>

#include <linux/sockios.h>
static inline pid_t gettid()
{
    return syscall(SYS_gettid);
}

#include <assert.h>


#include "led-matrix.h"
#include "graphics.h"
#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h> 

#include <math.h>

#define debugf(...) fprintf(stderr, __VA_ARGS__)


#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

const char *getip()
{
    struct ifaddrs *ifaddr, *ifa;
    int s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) 
    {
        perror("getifaddrs");
        return "";
    }

    const char *pop = "";

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) 
    {
        if (ifa->ifa_addr == NULL)
            continue;  

        s=getnameinfo(ifa->ifa_addr,sizeof(struct sockaddr_in),host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

        if((strcmp(ifa->ifa_name,"eth0")==0)&&(ifa->ifa_addr->sa_family==AF_INET))
        {
            if (s != 0)
            {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                exit(EXIT_FAILURE);
            }
            pop = strdup(host);
        }
    }

    freeifaddrs(ifaddr);

    return pop;
}


using rgb_matrix::RGBMatrix;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo)
{
  interrupt_received = true;
}



void centertext(rgb_matrix::FrameCanvas *swap_buffer, rgb_matrix::Font& font, int y, const char *txt)
{
  const int fx = 4;
  rgb_matrix::Color white = rgb_matrix::Color(200,200,200);

  rgb_matrix::DrawText(swap_buffer, font, (swap_buffer->width()-strlen(txt)*fx)/2, y + font.baseline(),
                       white, NULL, txt, 0);

}






RGBMatrix *matrix;
rgb_matrix::FrameCanvas *swap_buffer;
  rgb_matrix::Font font;

pthread_mutex_t sync_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t sync_cond = PTHREAD_COND_INITIALIZER;
uint16_t** sync_data;

void *frametuuperthread(void *x_void_ptr)
{
   pthread_setname_np(pthread_self(), "udp: frametuup");

  while(1)
  {
    pthread_mutex_lock (&sync_lock);

    struct timespec ts;
    struct timeval now;

    gettimeofday(&now,NULL);

    ts.tv_sec = now.tv_sec+3;
    ts.tv_nsec = now.tv_usec*1000;


    int condval = pthread_cond_timedwait (&sync_cond, &sync_lock, &ts);
    //int condval = pthread_cond_wait (&sync_cond, &sync_lock);

    if (condval == 0)
    {
      swap_buffer->SetTilePtrs((void**)sync_data);
      pthread_mutex_unlock (&sync_lock);

      swap_buffer = matrix->SwapOnVSync(swap_buffer);
    }
    else if (condval == ETIMEDOUT)
    {
 //     debugf("swap buf: %p", swap_buffer);
      swap_buffer->SetTilePtrs(0);
      pthread_mutex_unlock (&sync_lock);

      debugf("showing screen %i,%i\n", swap_buffer->width(), swap_buffer->height());

      swap_buffer->SetBrightness(30);
      swap_buffer->set_luminance_correct(true);
      swap_buffer->Fill(1,1,1);

      for (int y = 0; y < swap_buffer->height(); y++)
        for (int x = 0; x < swap_buffer->width(); x++)
        {
         int yy = swap_buffer->height()-y-1;
         swap_buffer->SetPixelHDR(x,y, yy,yy/2,yy/4);
         }

      static int pp = 0;

      pp++;
      pp %= 64;
      swap_buffer->SetPixelHDR(pp, 0, 3000,3000,3000);

#if 1
      centertext(swap_buffer, font, 1, "^^^");

      int centrow = swap_buffer->height() / 2;
      centertext(swap_buffer, font, centrow - 6, "Hacklab");
      centertext(swap_buffer, font, centrow, "LED System");

      const char *myip = getip();
      centertext(swap_buffer, font, swap_buffer->height() - 8, myip);
#endif

      swap_buffer = matrix->SwapOnVSync(swap_buffer);
    }
  }

  return NULL;

}



rgb_matrix::FrameCanvas *creatematrix(int argc, char **argv)
{
  RGBMatrix::Options defaults;
  defaults.hardware_mapping = "regular";  // or e.g. "adafruit-hat"
#ifdef VALTAVAMATRIISI
  defaults.rows = 16;
  defaults.cols = 64;
  defaults.chain_length = 1;
  defaults.multiplexing = 7;
  defaults.parallel = 3;
#else
  defaults.rows = 32;
  defaults.cols = 64;
  defaults.chain_length = 3;
  defaults.multiplexing = 0;
  defaults.parallel = 3;
#endif

  defaults.show_refresh_rate = true;
  //defaults.pwm_lsb_nanoseconds = 50;


 // --led-multiplexing=7 --led-cols=64 --led-rows=16 --led-parallel=3 --led-slowdown-gpio=2 

  rgb_matrix::RuntimeOptions runtime_defaults;
  runtime_defaults.drop_privileges = 1;
  runtime_defaults.gpio_slowdown = 3;

  matrix = rgb_matrix::CreateMatrixFromFlags(&argc, &argv,
                                                        &defaults,
                                                        &runtime_defaults);
  if (matrix == NULL) {
    PrintMatrixFlags(stderr, defaults, runtime_defaults);
    return NULL;
  }

//  matrix->ApplyStaticTransformer(rgb_matrix::DoubleAbsenTransformer());

  matrix->Clear();

  return matrix->CreateFrameCanvas();

 
}

void setsignal()
{
  struct sigaction sa;
  sa.sa_handler = InterruptHandler;
  sa.sa_flags = SA_RESETHAND | SA_NODEFER;
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT,  &sa, NULL);
}


 

typedef struct
{
  union
  {
  struct
  {
    uint8_t type;
    uint8_t frame;
    union
    {
      struct
      {
        uint16_t xpos;
        uint16_t ypos;
      };
      struct
      {
      };
    };
  };
    char siz[8];
  };
} packethdr_t;


  int m_s = 0;


uint16_t** frameptrs;

#ifdef VALTAVAMATRIISI
  const int screentiles_x = 4;
  const int screentiles_y = 3;
#else
  const int screentiles_x = 12;
  const int screentiles_y = 6;
#endif

const int tilesize_x = 16;
const int tilesize_y = 16;

const int framebuffers_count = 16;
const size_t framesize = tilesize_x*tilesize_y*6;
const size_t mempoolcount = screentiles_x*screentiles_y * framebuffers_count;
typedef struct { char data[framesize]; } framemem_t;

void initrecv()
{



  frameptrs = (uint16_t**)malloc(framebuffers_count*screentiles_x*screentiles_y * sizeof(uint16_t*));
  for (int i = 0; i < framebuffers_count*screentiles_x*screentiles_y; i++)
  {
    frameptrs[i] = NULL;
  }


  int port = 9998;



  if ((m_s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
  {
    printf("no socket created\n");
    m_s = 0;
  }


  struct sockaddr_in myaddr;

  memset((char *)&myaddr, 0, sizeof(myaddr));
  myaddr.sin_family = AF_INET;
  myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  myaddr.sin_port = htons(port);

  if (bind(m_s, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0)
  {
    printf("shokki, ei onnistu bind\n");
  }

//  int rcvbufsiz = 16777216;
  int rcvbufsiz = 1024*1024;
  socklen_t rcvbufsiz_siz = sizeof(rcvbufsiz);
  setsockopt(m_s, SOL_SOCKET, SO_RCVBUF, &rcvbufsiz, rcvbufsiz_siz);
  getsockopt(m_s, SOL_SOCKET, SO_RCVBUF, &rcvbufsiz, &rcvbufsiz_siz);
  printf("rcvbufsiz_siz %i, rcvbufsiz %i\n", rcvbufsiz_siz, rcvbufsiz);

  assert(sizeof(packethdr_t) == 8);
}

void *recvloop(void *x_void_ptr)
{
   pthread_setname_np(pthread_self(), (const char *)x_void_ptr);

   pthread_t self = pthread_self();

#if 1
   {
    int err;

#if 0
  struct rlimit rl;
  rl.rlim_cur = RLIM_INFINITY;
  rl.rlim_max = RLIM_INFINITY;

    if (err = setrlimit(RLIMIT_RTPRIO, &rl)) {
      fprintf(stderr, "error in rlimit: %s\n", strerror(err));
    }
#endif
    int priority = 99;
    struct sched_param p;
    p.sched_priority = priority;
    if ((err = pthread_setschedparam(self, SCHED_FIFO, &p))) {
      fprintf(stderr, "FYI: Can't set realtime thread priority=%d %s\n",
              priority, strerror(err));
    }

   }
#endif

   #if 1
   {
    int err;
    cpu_set_t cpu_mask;
    CPU_ZERO(&cpu_mask);
    CPU_SET(0, &cpu_mask);


    if ((err=pthread_setaffinity_np(self, sizeof(cpu_mask), &cpu_mask))) {
      fprintf(stderr, "FYI: Couldn't set affinity: %s\n",
              strerror(err));
    }
  }
  #endif


   framemem_t* mempool = (framemem_t*)malloc(mempoolcount * framesize);
   int mempoolidx = 0;

  while(!interrupt_received)
  {
    bool showcrap = false;

    //packet_t* p;
    //int typ = diod.openpacket(&p);

#if 0
    static int buf[192*96];
#endif

#if 0
    uint16_t* tileptrs[6*12] =
    {
        rb,nb,rb,rb, rb,rb,rb,rb, rb,rb,rb,rb, 
        gb,gb,gb,gb, gb,gb,gb,gb, nb,gb,gb,gb,
        bb,nb,bb,bb, bb,bb,bb,bb, bb,bb,nb,bb,
        rb,rb,rb,rb, nb,rb,rb,rb, rb,rb,rb,rb, 
        gb,gb,gb,gb, gb,gb,gb,nb, gb,gb,gb,gb,
        bb,bb,bb,bb, bb,bb,bb,bb, bb,bb,bb,bb,
    };

  //    swap_buffer->SetTilePtrs((void**)tileptrs);
//    swap_buffer = matrix->SwapOnVSync(swap_buffer);

    showcrap = true;
#endif

    showcrap = false;

    packethdr_t vidhdr;
    vidhdr.type = 0;

//    char* payload = (char*)malloc(16*16*6);
    char* payload = (char*)&mempool[mempoolidx];

     struct iovec vec[2] =
     {
      {
        &vidhdr,
        8,
      },
      {
        payload,
        16*16*6
      }
     };

    struct sockaddr_in srcaddr;
    struct msghdr hdr =
    {
      .msg_name = &srcaddr,
      .msg_namelen = sizeof(struct sockaddr_in),
      .msg_iov = vec,
      .msg_iovlen = 2,
      .msg_control = NULL,
      .msg_controllen = 0,
      .msg_flags = 0,
    };



  struct timeval tv;
  tv.tv_sec = 1;
  tv.tv_usec = 0;

  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(m_s, &rfds);

  fd_set efds;
  FD_ZERO(&efds);
  FD_SET(m_s, &efds);

  int retval = select(m_s+1, &rfds, NULL, &efds, &tv);

  if (retval == -1)
    return NULL;


  if (FD_ISSET(m_s, &efds))
  {
    printf("PROBLEM'S IN SOCKET!\n");
  }



     if (FD_ISSET(m_s, &rfds))
    {
      ssize_t len = recvmsg(m_s, &hdr, 0);
      
        
      if (len < (ssize_t)sizeof(packethdr_t))
      {
        printf("%lX: got %d bytes (hdr %u)\n", self, len, sizeof(packethdr_t));
        printf("INVALID\n");
        continue;
      }
    }
    else
      continue;


#if 0
              {
              int bufleft;
              (void)ioctl(m_s, SIOCINQ, &bufleft);
              if (bufleft > 0)
              {
                skippedcunt++;
                continue;
              }
            }

            if (skippedcunt > 0)
              printf("skipped: %i\n", skippedcunt);
            skippedcunt = 0;

#endif
    //printf("packet from %X:%i, siz %ld, type: %i\n", srcaddr.sin_addr.s_addr, srcaddr.sin_port, len, vidhdr.type);

    //free(payload);


    int fr = vidhdr.frame & 15;

    int offs = fr * screentiles_x * screentiles_y;

    if (vidhdr.type == 1)
    {
      //printf("pack to %i,%i\n", vidhdr.xpos, vidhdr.ypos);
      int xt = vidhdr.xpos / tilesize_x;
      int yt = vidhdr.ypos / tilesize_y;

      if (xt < 0 || yt < 0 || xt > screentiles_x || yt > screentiles_y)
        goto invalidframe;

      if (frameptrs[offs + yt * screentiles_x + xt])
      {
//        printf("huh, frame already got\n");
        //free(frameptrs[offs + yt * screentiles_x + xt]);
        //return 1;
      }
      frameptrs[offs + yt * screentiles_x + xt] = (uint16_t*)payload;

      mempoolidx++;
      mempoolidx %= mempoolcount;

      invalidframe:;
    }
    else if (vidhdr.type == 2)
    {
      //printf("pageflip to %i\n", fr);


#if 1
      int oktiles = 0;
      for (int i = 0; i < screentiles_x * screentiles_y; i++)
      {
        if (frameptrs[offs + i])
          oktiles++;
      }


              int bufleft;
              (void)ioctl(m_s, SIOCINQ, &bufleft);
      printf("     %lX: fr %i, left %i, ok tiles: %.2f%%\n", self, fr, bufleft, (float)oktiles*100.f / (screentiles_x*screentiles_y));
#endif

//      swap_buffer->SetTilePtrs((void**)&frameptrs[offs]);
//    swap_buffer = matrix->SwapOnVSync(swap_buffer);


#if 1
      pthread_mutex_lock(&sync_lock);
      sync_data = &frameptrs[offs];
      pthread_cond_signal(&sync_cond);
      pthread_mutex_unlock(&sync_lock);
#endif
    }
  }

  return NULL;
}

int main(int argc, char **argv)
{
  swap_buffer = creatematrix(argc, argv);
  setsignal();

#if 1
  const char* bdf_font_file = "../fonts/4x6.bdf";
  if (!font.LoadFont(bdf_font_file)) {
    fprintf(stderr, "Couldn't load font '%s'\n", bdf_font_file);
  }
#endif

//  dadadiode_t diod;
//  diod.openread(9999);

#if 0
    uint16_t rb[16*16*3];
    uint16_t gb[16*16*3];
    uint16_t bb[16*16*3];
    uint16_t* nb = NULL;

    memset(rb, 0, 16*16*3*2);
    memset(gb, 0, 16*16*3*2);
    memset(bb, 0, 16*16*3*2);

    for (int i = 0; i < 16*16; i++)
    {
      rb[i*3+0] = 1000;
      gb[i*3+1] = 1000;
      bb[i*3+2] = 1000;
    }
#endif

    pthread_t sync_thread;
    pthread_create(&sync_thread, NULL, frametuuperthread, 0);

    pthread_t recv1_thread;
    pthread_t recv2_thread;
    pthread_t recv3_thread;
//    pthread_create(&recv_thread, NULL, living_receiver, 0);

    initrecv();

    pthread_create(&recv1_thread, NULL, recvloop, (void*)"udp: recv1");
    pthread_create(&recv2_thread, NULL, recvloop, (void*)"udp: recv2");
   //pthread_create(&recv3_thread, NULL, recvloop, (void*)"udp: recv3");

   pthread_setname_np(pthread_self(), "main thread");
   while(1)
   {
      sleep(10);
   }

//    recvloop(NULL);
  //diod.shutdown();

  delete matrix;   // Make sure to delete it in the end.
}
