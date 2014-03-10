#include <stdlib.h>
#include <selector.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <gme.h>
#include <assert.h>
#include <sys/wait.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <time.h>

#define ME ((consoleselector) s)
static int console_onselect(tipselector s,tiptoi t) {
  if (FD_ISSET(0,&t->listeners)) {
    char buffer[256];
    int i=read(0,buffer,sizeof(buffer));
    if (i<0) {
      if (errno!=EAGAIN && errno!=EINTR) {
        return TIP_CLOSE;
      }
    } else {
      char* p=buffer;
      while (i--) {
        char c=*(p++);
        if (c=='\n') {
          if (ME->i>0) {
            tiptoi_play_oid(t,ME->i);
          } else {
            fprintf(stderr,"Invalid input!\n");
          }
          ME->i=0;
          fprintf(stdout,"Next OID: ");
          fflush(stdout);
        } else if (ME->i>=0 && c>='0' && c<='9') {
          ME->i=ME->i*10+(c-'0');
        } else if (c==4) {
          fprintf(stdout,"Closed!\n");
          return TIP_CLOSE;
        } else {
          /* Invalid input found => drop input */
          ME->i=-1;
        }
      }
    }
  } else {
    FD_SET(0,&t->listeners);
  }
  return 0;
}
#undef ME

consoleselector consoleselector_new(tiptoi t) {
  consoleselector toReturn=malloc(sizeof(struct _consoleselector));
  tipselector_init(&toReturn->sup,t,console_onselect);
  toReturn->i=0;
  int flags=fcntl(STDIN_FILENO,F_GETFL);
  if (fcntl(STDIN_FILENO,F_SETFL,flags | O_NONBLOCK)) {
    perror("fcntl");
  }
  fprintf(stdout,"Next OID: ");
  fflush(stdout);
  FD_SET(STDIN_FILENO,&t->listeners);
  if (t->max_set_fd<=STDIN_FILENO) t->max_set_fd=STDIN_FILENO;
  return toReturn;
}

static void mediaselector_open(mediaselector ME,tiptoi t) {
  struct media_play* current=ME->current;
  const char* command;
  if (current->data[0]=='O') {
    command="ogg123";
  } else {
    command="aplay";
  }
  
  int pipeFd[2];
  if (pipe(pipeFd)) {
    perror("pipe");
    return;
  }
  
  pid_t cpid=fork();

  if (cpid==-1) {
    perror("fork");
    close(pipeFd[0]);
    close(pipeFd[1]);
    return;
  }

  if (cpid==0) {
    /* child process */
    close(pipeFd[1]); // No output to pipe
    dup2(pipeFd[0],STDIN_FILENO);
    close(pipeFd[0]);
    execlp(command,command,"-q","-",NULL);
    perror("execlp");
    exit(1);
  }

  /* parent process */
  close(pipeFd[0]); // No input from pipe
  ME->pid=cpid;
  int fd=ME->fd=pipeFd[1];
  int flags=fcntl(fd,F_GETFL);
  fcntl(fd,F_SETFL,flags | O_NONBLOCK);
  FD_SET(fd,&t->writers);
  if (fd>t->max_set_fd) t->max_set_fd=fd;
  return;
}

#define ME ((mediaselector) s)
static int media_onselect(tipselector s,tiptoi t) {
  struct media_play* current=ME->current;
  int toReturn=ME->sigfd;
  int fd=ME->fd;
  if (fd>=0) {
    if (FD_ISSET(fd,&t->writers)) {
      ssize_t i=write(fd,current->data,current->len);
      if (i>=0) {
        current->len-=i;
        current->data+=i;
      } else {
        current->len=0;
      }
      if (!current->len) {
        close(ME->fd);
        FD_CLR(fd,&t->writers);
        ME->fd=fd=TIP_SLEEP;
      }
    } else {
      /* Set bit for next select again */
      FD_SET(fd,&t->writers);
    }
    if (fd>toReturn) toReturn=fd;
  }
  
  if (FD_ISSET(ME->sigfd,&t->listeners)) {
    struct signalfd_siginfo si;
    /* Consume the signal */
    read(ME->sigfd,&si,sizeof(si));
    if (ME->pid) {
      int status;
      if (waitpid(ME->pid,&status,WNOHANG)) {
        ME->pid=0;
        struct media_play* next=ME->current->next;
        free(ME->current);
        if ((ME->current=next)) {
          mediaselector_open(ME,t);
        }
      }
    }
  } else {
    FD_SET(ME->sigfd,&t->listeners);
  }

  return toReturn;
}

#undef ME

mediaselector mediaselector_new(tiptoi t) {
  mediaselector ME=malloc(sizeof(struct _mediaselector));
  tipselector_init(&ME->sup,t,media_onselect);
  ME->fd=TIP_SLEEP;
  ME->current=NULL;
  ME->append_pos=&ME->current;
  ME->pid=0;
  ME->kill_on_append=0;
  /* We want to get informed if a child exits */
  {
    sigset_t sigset;
    if (sigemptyset(&sigset)) {
      perror("sigemptyset");
    } else if (sigaddset(&sigset,SIGCHLD)) {
      perror("sigaddset");
    } else if (sigprocmask(SIG_BLOCK,&sigset,NULL)) {
      perror("sigprocmask");
    }
    ME->sigfd=signalfd(-1,&sigset,SFD_NONBLOCK);
  }
  FD_SET(ME->sigfd,&t->listeners);
  if (ME->sigfd>t->max_set_fd) t->max_set_fd=ME->sigfd;
  ME->last_play=malloc(sizeof(uint16_t)*(ME->last_play_allocated=10));
  ME->last_play_len=0;
  return ME;
}

void mediaselector_append(mediaselector ME,tiptoi t,uint16_t media_off)
{
  /* Kill current play queue? */
  if (ME->kill_on_append) {
    ME->last_play_len=0;
    ME->kill_on_append=0;
    if (ME->fd>=0) {
      close(ME->fd);
      FD_CLR(ME->fd,&t->writers);
      ME->fd=TIP_SLEEP;
    }
    if (ME->pid) {
      kill(ME->pid,SIGTERM);
      int status;
      waitpid(ME->pid,&status,0);
      ME->pid=0;
    }
    struct media_play* current=ME->current;
    while (current) {
      struct media_play* next=current->next;
      free(current);
      current=next;
    }
    ME->current=NULL;
  }

  /* append media to the repeat queue */
  if (ME->last_play_len==ME->last_play_allocated) {
    ME->last_play_allocated+=10;
    ME->last_play=realloc(ME->last_play,
                          sizeof(uint16_t)*ME->last_play_allocated);
  }
  ME->last_play[ME->last_play_len]=media_off;
  ME->last_play_len++;

  struct gme* gme=t->gme;
  struct gme_media_table* mt=gme_get_media(gme);
  assert(media_off<gme_media_table_count(gme,mt));
  struct media_play* mp=malloc(sizeof (struct media_play));
  mp->next=NULL;
  mp->data=gme_get_ptr(gme,mt->entries[media_off].off);
  mp->len=mt->entries[media_off].len;
  if (ME->current) {
    *ME->append_pos=mp;
  } else {
    ME->current=mp;
    mediaselector_open(ME,t);
  }
  ME->append_pos=&mp->next;
}

void mediaselector_append_pl(mediaselector ME,tiptoi t,const struct gme_playlist* pl)
{
  int j;
  for (j=0; j<pl->len; j++) {
    mediaselector_append(ME,t,gme_playlist_get(pl,j));
  }
}

void mediaselector_append_pll(mediaselector ME,tiptoi t,const struct gme_playlistlist* pll)
{
  if (!pll->len) return;
  int i=rand() % pll->len;
  const struct gme_playlist* pl=gme_playlistlist_get(t->gme,pll,i);
  mediaselector_append_pl(ME,t,pl);
}

void mediaselector_repeat(mediaselector ME,tiptoi t) {
  uint16_t l=ME->last_play_len;
  if (l) {
    uint16_t i;
    for (i=0; i<l; i++) {
      mediaselector_append(ME,t,ME->last_play[i]);
    }
  }
}

#define ME ((acceptselector) s)
static int acceptselector_onselect(tipselector s,tiptoi t) {
  if (FD_ISSET(ME->fd,&t->listeners)) {
    int fd=accept(ME->fd,NULL,NULL);
    if (fd>=0) {
      httpselector_new(t,fd);
    }
  } else {
    FD_SET(ME->fd,&t->listeners);
  }
  return ME->fd;
}
#undef ME

acceptselector acceptselector_new(tiptoi t,int port) {
  acceptselector ME=malloc(sizeof(struct _acceptselector));
  ME->fd=socket(AF_INET,SOCK_STREAM | SOCK_NONBLOCK,0);
  if (ME->fd<0) {
    perror("socket");
    goto error1;
  }
  struct sockaddr_in sockaddr;
  sockaddr.sin_family=AF_INET;
  sockaddr.sin_port=htons(port);
  sockaddr.sin_addr.s_addr=INADDR_ANY;
  if (bind(ME->fd,(struct sockaddr*) &sockaddr,sizeof(sockaddr))) {
    perror("bind");
    goto error1;
  }
  if (listen(ME->fd,2)) {
    perror("listen");
    goto error1;
  }
  tipselector_init(&ME->sup,t,acceptselector_onselect);
  if (ME->fd>t->max_set_fd) t->max_set_fd=ME->fd;
  printf("Listening on port %d\n",port);
  return ME;
 error1:
  free(ME);
  return NULL;
}

#define ME ((httpselector) s)

static const char* POST="POST /";
static const char* CRLFCRLF="\r\n\r\n";

static int httpselector_onselect(tipselector s,tiptoi t) {
  if (FD_ISSET(ME->fd,&t->listeners)) {
    char buffer[4096];
    char *p=buffer;
    int i=read(ME->fd,buffer,sizeof(buffer));
    if (i<0) {
      perror("read");
    }
    int state=ME->state;
    if (i==0) {
      fprintf(stderr,"Closed connection.\n");
      close(ME->fd);
      FD_CLR(ME->fd,&t->listeners);
      return TIP_CLOSE;
    }
    while (i-->0) {
      char ch=*(p++);
      if (state<6) {
        if (ch==POST[state]) {
          state++;
        } else {
          fprintf(stderr,"POST not found %d: %c.\n",state,ch);
          state=7;
        }
      } else if (state==6) {
        if (ch>='0' && ch<='9') {
          ME->i=ME->i*10+ch-'0';
        } else if (ch==' ') {
          tiptoi_play_oid(t,ME->i);
          ME->i=0;
          state=7;
        } else {
          fprintf(stderr,"Invalid number\n");
          state=7;
        }
      }
      if (state>=7) {
        if (ch==CRLFCRLF[state-7]) {
          state++;
          if (state==11) {
            time_t t;
            time(&t);
            char date[64];
            char resp[1024];
            strftime(date,sizeof(date),"%a, %d %b %Y %T %z",localtime(&t));
            static const char* RESP="HTTP/1.1 404 Not found\r\nDate: %s\r\nContent-Length: 0\r\n\r\n";
            int l=snprintf(resp,sizeof(resp),RESP,date);
            /* We assume we can always send the response without blocking */
            if (l!=write(ME->fd,resp,l)) {
              fprintf(stderr,"Write incomplete!\n");
            }
            state=0;
          }
        } else {
          state=7;
        }
      }
    }
    ME->state=state;
  } else {
    FD_SET(ME->fd,&t->listeners);
  }

#if 0
  if (ME->send_pos!=ME->buffer_end) {
    if (FD_ISSET(ME->fd,&t->writers)) {
      int i=sizeof(ME->buffer)-ME->send_pos;
      if (ME->buffer_end>ME->send_pos) {
        i=ME->buffer_end-ME->send_pos;
      }
      i=write(ME->fd,outbuffer,i);
      if (i>0) {
        ME->send_pos+=i;
        if (ME->send_pos==sizeof(ME->buffer)) ME->send_pos=0;
      }
      
    } else {
      /* Set flag for next round */
      FD_SET(ME->fd,&t->writers);
    }
  }
#endif

  return ME->fd;
}
#undef ME

httpselector httpselector_new(tiptoi t,int fd) {
  httpselector ME=malloc(sizeof(struct _httpselector));
  ME->fd=fd;
  ME->state=0;
  ME->i=0;
  ME->send_pos=ME->buffer_end=0;
  tipselector_init(&ME->sup,t,httpselector_onselect);
  FD_SET(fd,&t->listeners);
  if (fd>t->max_set_fd) t->max_set_fd=fd;
  return ME;
}

