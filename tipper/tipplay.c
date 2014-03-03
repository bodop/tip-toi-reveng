#include <stdlib.h>
#include <stdio.h>
#include <gme.h>
#include <errno.h>
#include <assert.h>
#include <selector.h>
#include <string.h>

static tiptoi t;

static uint16_t get_operand(unsigned char**p,struct gme_registers* regs) {
  unsigned char r=*((*p)++);
  uint16_t m=*(uint16_t*) (*p);
  *p+=2;
  if (r) return m;
  assert(m<regs->len);
  return regs->regs[m];
}

static int gme_script_line_match(struct gme_script_line* line,
                                 struct gme_registers* r)
{
  int i=le16toh(line->conditions);
  unsigned char* p=line->raw;
  while (i--) {
    uint16_t a=get_operand(&p,r);
    uint16_t o=*(uint16_t*) p;
    p+=2;
    uint16_t b=get_operand(&p,r);
    switch(o) {
    case 0xfff9: /* == */
      if (a!=b) return 0;
      break;
    case 0xfffb: /* < */
      if (a>=b) return 0;
      break;
    case 0xfffd: /* >= */
      if (a<b) return 0;
      break;
    case 0xffff: /* != */
      if (a==b) return 0;
      break;
    default:
      fprintf(stderr,"Unhandled operator 0x%x\n",o);
      return 0;
    }
  }
  return 1;
}

static int gme_script_line_execute(struct gme* gme,struct gme_script_line* sl,
                                   struct gme_registers* regs)
{
  unsigned char* p=sl->raw+le16toh(sl->conditions)*8;

  /* Number of actions */
  uint16_t i=le16toh(*(uint16_t*) p);
  p+=2;
  while (i--) {
    uint16_t r=le16toh(*(uint16_t*) p);
    p+=2;
    uint16_t c=le16toh(*(uint16_t*) p);
    p+=2;
    uint16_t m=get_operand(&p,regs);
    switch (c) {
    case 0xfc00:
      {
        uint16_t from=m>>8;
        uint16_t to=m&0xff;
        assert(from<=to);
        m=rand()%(to-from+1)+from;
      }
      /* Play it */
    case 0xffe8: /* P(m) */
      {
        struct gme_playlist* pl=gme_script_line_playlist(sl);
        mediaselector_append(t->m,t,gme_playlist_get(pl,m));
      }
      break;
    case 0xfff0:
      assert(r<regs->len);
      regs->regs[r]+=m;
      break;
    case 0xfff9:
      assert(r<regs->len);
      regs->regs[r]=m;
      break;
    default:
      fprintf(stderr,"Unhandled command 0x%x\n",c);
    }
  }
  /* TODO error handling */
  return 0;
}

void tipselector_init(tipselector ts,tiptoi t,onselectfunc onselect) {
  ts->onselect=onselect;
  /* Add selector to tiptoi */
  ts->next=t->selectors;
  t->selectors=ts;
}

static tiptoi tiptoi_new(struct gme* gme) {
  tiptoi ME=malloc(sizeof(struct _tiptoi));
  ME->max_set_fd=-1;
  FD_ZERO(&ME->listeners);
  FD_ZERO(&ME->writers);
  ME->gme=gme;
  ME->selectors=NULL;
  ME->m=mediaselector_new(ME);

  /* Play welcome */
  {
    struct gme_playlistlist* pll=gme_get_welcome(gme);
    if (pll) {
      int i,j;
      for (i=0; i<pll->len; i++) {
        struct gme_playlist* pl=gme_playlistlist_get(gme,pll,i);
        for (j=0; j<pl->len; j++) {
          mediaselector_append(ME->m,ME,gme_playlist_get(pl,j));
        }
      }
      ME->m->kill_on_append=1;
    }
  }

  return ME;
}

void tiptoi_play_oid(tiptoi ME,uint32_t oid) {
  struct gme* gme=ME->gme;
  /* Init regs */
  struct gme_registers* regs=gme_get_registers(gme);
  struct gme_script_table* st=gme_get_scripts(gme);

  if (oid<st->first_oid || oid>st->last_oid) return;
  struct gme_script* s=gme_script_table_get(gme,st,oid);
  if (!s) return;
  uint16_t i;
  for (i=0; i<s->lines; i++) {
    struct gme_script_line* sl=gme_script_get(gme,s,i);
    if (gme_script_line_match(sl,regs)) {
      gme_script_line_print(sl,stdout);
      gme_script_line_execute(gme,sl,regs);
      ME->m->kill_on_append=1;
      break;
    }
  }
}

int usage() {
  fprintf(stderr,"usage: tipplay [-p <port>] <gme file>\n");
  return 1;
}

int main(int argc,char** argv) {

  /* Skip program name */
  argc--;
  argv++;
  int port=0;

  while (argc>1) {
    if (!strcmp("-p",argv[0]) && argc>2) {
      port=atoi(argv[1]);
      argv+=2;
      argc-=2;
    } else {
      return usage();
    }
  }

  struct gme* gme=gme_load(argv[0]);

  if (!gme) {
    perror("Cannot load");
    return 1;
  }

  /* Perform magic xor on load */
  {
    struct gme_media_table* mt=gme_get_media(gme);
    uint32_t x=gme->magic_xor;
    uint16_t i;
    for (i=gme_media_table_count(gme,mt); i--; ) {
      unsigned char *p=gme_get_ptr(gme,mt->entries[i].off);
      uint32_t len=mt->entries[i].len;
      while (len--) {
        *p=magic_xor(x,p);
        p++;
      }
    }
  }

  t=tiptoi_new(gme);
  /* Listen for browser? */
  if (port) acceptselector_new(t,port);
  /* Connect to console */
  consoleselector_new(t);

  while (t->max_set_fd>=0) {
#define SELECT_DEBUG 0
    if (SELECT_DEBUG) {
      /* Print select info */
      printf("select(%d,",t->max_set_fd+1);
      int i;
      for (i=0; i<=t->max_set_fd; i++)
        putchar(FD_ISSET(i,&t->listeners) ? '1' : '0');
      putchar(',');
      for (i=0; i<=t->max_set_fd; i++)
        putchar(FD_ISSET(i,&t->writers) ? '1' : '0');
      printf(")");
    }
    int i=select(t->max_set_fd+1,&t->listeners,&t->writers,NULL,NULL);
    if (SELECT_DEBUG) {
      /* Print select info */
      printf("=>(%d,%d,",i,errno);
      int i;
      for (i=0; i<=t->max_set_fd; i++)
        putchar(FD_ISSET(i,&t->listeners) ? '1' : '0');
      putchar(',');
      for (i=0; i<=t->max_set_fd; i++)
        putchar(FD_ISSET(i,&t->writers) ? '1' : '0');
      printf(")\n");
    }
    t->max_set_fd=-1;
    tipselector ts=t->selectors;
    tipselector* lastSrc=&t->selectors;
    while (ts) {
      int tmf=ts->onselect(ts,t);
      if (tmf==-2) {
        *lastSrc=ts->next;
        free(ts);
        ts=*lastSrc;
        continue;
      } else {
        if (tmf>t->max_set_fd) t->max_set_fd=tmf;
      }
      ts=ts->next;
    }
  };
  
  return 0;
}
