#ifndef SELECTOR_H
#define SELECTOR_H

#include <tipplay.h>
#include <stdio.h>
#include <gme.h>

typedef struct _consoleselector* consoleselector;

struct _consoleselector {
  struct _tipselector sup;
  int i;
};

consoleselector consoleselector_new(tiptoi t);

typedef struct _mediaselector* mediaselector;

struct media_play {
  struct media_play* next;
  unsigned char* data;
  uint32_t len;
};

struct _mediaselector {
  struct _tipselector sup;
  /* Pipe to player */
  int fd;
  /* PID of player */
  pid_t pid;
  /* Here we get the signals from */
  int sigfd;
  /* Should playing be killed during next append? */
  int kill_on_append;
  struct media_play* current;
  struct media_play** append_pos;
  size_t last_play_len;
  size_t last_play_allocated;
  uint16_t* last_play;
};

void mediaselector_append(mediaselector ME,tiptoi t,uint16_t media_n);
void mediaselector_append_pl(mediaselector ME,tiptoi t,const struct gme_playlist* pl);
void mediaselector_append_pll(mediaselector,tiptoi,const struct gme_playlistlist*);
void mediaselector_repeat(mediaselector ME,tiptoi t);

mediaselector mediaselector_new();

typedef struct _acceptselector* acceptselector;

struct _acceptselector {
  struct _tipselector sup;
  int fd;
};

acceptselector acceptselector_new(tiptoi t,int port);

typedef struct _httpselector* httpselector;

struct _httpselector {
  struct _tipselector sup;
  int fd;
  /* 0 .. 5: next char should be "POST /" */
  /* 6 read oid */
  /* 7 .. 10 next char sould ber CRLFCRLF */
  int state;
  int i;
  char outbuffer[4096];
  /* Next byt to send */
  int send_pos;
  /* End of buffer */
  int buffer_end;
};

httpselector httpselector_new(tiptoi t,int fd);

#endif
