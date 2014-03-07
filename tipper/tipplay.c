#include <stdlib.h>
#include <stdio.h>
#include <gme.h>
#include <errno.h>
#include <assert.h>
#include <selector.h>
#include <string.h>
#include <time.h>

static tiptoi t;

static uint16_t get_operand(unsigned char**p,const struct gme_registers* regs) {
  unsigned char r=*((*p)++);
  uint16_t m=*(uint16_t*) (*p);
  *p+=2;
  if (r) return m;
  assert(m<regs->len);
  return regs->regs[m];
}

static int gme_script_line_match(struct gme_script_line* line,
                                 const struct gme_registers* r)
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

/**
 * @return error?
 */
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
    case 0xfd00: /* G(m) */
      {
        struct gme_games_table* games=gme_get_games(gme);
        assert(m<le16toh(games->len));
        struct gme_game* game=gme_games_get(games,gme,m);
        struct gme_playlistlist* pll=
          gme_game_get_playlistlist(game,gme,GME_GAME_WELCOME);
        mediaselector_append_pll(t->m,t,pll);

        /* Shuffle subgames */
        t->current_game=game;
        t->score=0;
        t->round=0;
        switch (le16toh(game->type)) {
        case 1:
          if (le16toh(game->subgames_len)>t->subgames_len) {
            t->subgames_len=le16toh(game->subgames_len);
            t->sg_shuff=realloc(t->sg_shuff,sizeof(uint16_t)*t->subgames_len);
          }
          uint16_t sgi;
          for (sgi=0; sgi<t->subgames_len; sgi++) {
            uint16_t shi=rand() % (sgi+1);
            t->sg_shuff[sgi]=t->sg_shuff[shi];
            t->sg_shuff[shi]=sgi;
          }
          struct gme_subgame* sg=gme_game_get_subgame(game,gme,t->sg_shuff[0]);
          t->m->last_play_len=0;
          mediaselector_append_pll
            (t->m,t,gme_subgame_get_playlistlist(sg,gme,GME_SUBGAME_PLAY));
          break;
        default:
          fprintf(stderr,"Game type %d not supported.\n",le16toh(game->type));
          t->current_game=NULL;
        }
      }
      return 0;
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

static void tiptoi_reset(tiptoi ME) {
  const struct gme_registers* regs=gme_get_registers(ME->gme);
  int i=ME->regs->len;
  uint16_t* pdest=ME->regs->regs;
  const uint16_t* psrc=regs->regs;
  while (i--) {
    *(pdest++)=le16toh(*(psrc++));
  }
  ME->current_game=NULL;
}

static tiptoi tiptoi_new(struct gme* gme) {
  tiptoi ME=malloc(sizeof(struct _tiptoi));
  ME->max_set_fd=-1;
  FD_ZERO(&ME->listeners);
  FD_ZERO(&ME->writers);
  ME->gme=gme;
  ME->selectors=NULL;
  ME->m=mediaselector_new(ME);
  ME->subgames_len=0;
  ME->sg_shuff=NULL;
  const struct gme_registers* regs=gme_get_registers(gme);
  ME->regs=malloc((le16toh(regs->len)+1)*2);
  ME->regs->len=le16toh(regs->len);
  tiptoi_reset(ME);
  /* Play welcome */
  mediaselector_append_pll(ME->m,ME,gme_get_welcome(gme));
  return ME;
}

void tiptoi_play_oid(tiptoi ME,uint32_t oid) {
  fprintf(stdout,"Playing %d\n",oid);
  ME->m->kill_on_append=1;
  struct gme* gme=ME->gme;
  struct gme_script_table* st=gme_get_scripts(gme);

  if (oid<st->first_oid || oid>st->last_oid) return;
  struct gme_script* s=gme_script_table_get(gme,st,oid);
  if (s) {
    uint16_t i;
    for (i=0; i<s->lines; i++) {
      struct gme_script_line* sl=gme_script_get(gme,s,i);
      if (gme_script_line_match(sl,ME->regs)) {
        gme_script_line_print(sl,stdout);
        gme_script_line_execute(gme,sl,ME->regs);
        return;
      }
    }
  }
  
  /* No match found look at current game */
  if (ME->current_game) {
    const struct gme_game* g=ME->current_game;
    if (gme_game_get_repeat_oid(g)==oid) {
      mediaselector_repeat(ME->m,ME);
      return;
    }
    const struct gme_subgame* sg=
      gme_game_get_subgame(g,t->gme,ME->sg_shuff[ME->round]);
    switch (le16toh(ME->current_game->type)) {
    case 1:
      if (gme_oidlist_contains(gme_subgame_get_oids(sg,GME_SUBGAME_OK_OIDS),oid)) {
        mediaselector_append_pll(ME->m,t,gme_subgame_get_playlistlist(sg,t->gme,GME_SUBGAME_OK_PLAY));
        ME->score++;
      } else if (gme_oidlist_contains(gme_subgame_get_oids(sg,GME_SUBGAME_UNKNOWN_OIDS),oid)) {
        mediaselector_append_pll(ME->m,t,gme_subgame_get_playlistlist(sg,t->gme,GME_SUBGAME_UNKNOWN_PLAY));
      } else if (gme_oidlist_contains(gme_subgame_get_oids(sg,GME_SUBGAME_FALSE_OIDS),oid)) {
        mediaselector_append_pll(ME->m,t,gme_subgame_get_playlistlist(sg,t->gme,GME_SUBGAME_FALSE_PLAY));
      } else {
        mediaselector_append_pll(t->m,t,gme_subgame_get_playlistlist(sg,t->gme,GME_SUBGAME_INVALID));
        return;
      }
      ME->round++;
      if (ME->round<le16toh(g->rounds)) {
        const struct gme_playlistlist* pll;
        if (ME->round>=le16toh(gme_game_get_pre_last_round_count(g))) {
          pll=gme_game_get_playlistlist(g,ME->gme,GME_GAME_LAST_ROUND);
        } else {
          pll=gme_game_get_playlistlist(g,ME->gme,GME_GAME_NEXT_ROUND);
        }
        mediaselector_append_pll(t->m,t,pll);
        const struct gme_subgame* sg=
          gme_game_get_subgame(g,t->gme,ME->sg_shuff[ME->round]);
        mediaselector_append_pll(t->m,t,gme_subgame_get_playlistlist(sg,t->gme,GME_SUBGAME_PLAY));
        return;
      }
      /* Evaluate score */
      {
        uint16_t i;
        const struct gme_scorelist* sl=gme_game_get_scorelist(g,ME->gme);
        for (i=0; i<10; i++) {
          if (ME->score>=sl->scores[i]) {
            mediaselector_append_pll(ME->m,ME,(const struct gme_playlistlist*)
                                     gme_get_ptr(ME->gme,sl->plls[i]));
            break;
          }
        }
      }
      mediaselector_append_pll(ME->m,ME,gme_game_get_playlistlist(g,ME->gme,GME_GAME_BYE));
      /* Game over */
      tiptoi_reset(ME);
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

  /* Seed random generator */
  srand(time(NULL));

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
