#include <stdlib.h>
#include <stddef.h>
#include <gme.h>
#include <sys/stat.h>
#include <stdio.h>
#include <assert.h>
#include <endian.h>
#include <string.h>

void* gme_get_ptr(struct gme* gme,uint32_t off) {
  off=le32toh(off);
  assert(off+sizeof(uint32_t)<=gme->size);
  return gme->u.raw+off;
}

static inline uint16_t get_uint16(const unsigned char* p) {
  uint16_t i;
  memcpy(&i,p,2);
  return le16toh(i);
}

static inline uint32_t get_uint32(const unsigned char* p) {
  uint32_t i;
  memcpy(&i,p,4);
  return le32toh(i);
}

unsigned char magic_xor(unsigned char x,const unsigned char* p) {
  unsigned char r=*p;
  return (!r || r==0xff || r==x || r==(0xff^x)) ? r : r^x;
}

static int detect_xor(const char* orig,unsigned char* p) {
  unsigned char x=*p^*orig;
  if (magic_xor(x,p+1)==*(orig+1)) return x;
  return -1;
}

struct gme* gme_load(const char* path) {
  struct stat buf;
  if (stat(path,&buf)) return NULL;
  if (buf.st_size<sizeof(struct gme_header)) {
    fprintf(stderr,"File shorter than header\n");
    return NULL;
  }
  struct gme* gme=malloc(offsetof(struct gme,u)+buf.st_size);
  if (!gme) return NULL;
  gme->size=buf.st_size;
  FILE *f=fopen(path,"r");
  if (!f) goto error1;
  if (1!=fread(gme->u.raw,buf.st_size,1,f))
    goto error2;
  fclose(f);

  /* Try to detect real XOR */
  {
    struct gme_media_table* mt=gme_get_media(gme);
    unsigned char* m=gme_get_ptr(gme,mt->entries[0].off);
    int x=detect_xor("Og",m);
    if (x>=0) {
      gme->magic_xor=x;
    } else {
      x=detect_xor("RI",m);
      assert(x>=0);
      gme->magic_xor=x;
    }
  }
#ifndef NDEBUG
  assert(gme->u.header.fixed238b==0x238b);
  {
    struct gme_script_table* st=gme_get_scripts(gme);
    assert(st->first_oid<=st->last_oid);
    uint32_t i=st->first_oid;
    while (i<=st->last_oid) {
      struct gme_script* s=gme_script_table_get(gme,st,i);
      if (s) {
        /* TODO */
      }
      i++;
    }
  }
  
#ifndef N_DEBUG
  /* check games */
  {
    struct gme_games_table* games=gme_get_games(gme);
    uint16_t gi;
    for (gi=0; gi<le16toh(games->len); gi++) {
      struct gme_game* game=gme_games_get(games,gme,gi);
      if (gi+1==le16toh(games->len)) {
        assert(game->type==253);
        break;
      }
      assert(game->rounds<=game->subgames_len);
    }
  }
#endif
  /* Check checksum */
  {
    uint32_t sum=0;
    unsigned char* p=gme->u.raw;
    uint32_t i=gme->size-4;
    while (i--) {
      sum+=*(p++);
    }
    assert(sum==*(uint32_t*) p);
  }
#endif
  return gme;
 error2:
  fclose(f);
 error1:
  free(gme);
  return 0;
}

struct gme_script_table* gme_get_scripts(struct gme* gme) {
  return gme_get_ptr(gme,gme->u.header.play_off);
}

struct gme_media_table* gme_get_media(struct gme* gme) {
  return gme_get_ptr(gme,gme->u.header.media_off);
}

struct gme_games_table* gme_get_games(struct gme* gme) {
  return gme_get_ptr(gme,gme->u.header.games_off);
}

struct gme_registers* gme_get_registers(struct gme* gme) {
  return gme_get_ptr(gme,gme->u.header.regs_off);
}

struct gme_playlistlist* gme_get_welcome(struct gme* gme) {
  if (!gme->u.header.welcome_off) return NULL;
  return gme_get_ptr(gme,gme->u.header.welcome_off);
}

struct gme_script* gme_script_table_get(struct gme* gme,
                                        struct gme_script_table* st,
                                        uint32_t oid)
{
  assert(oid>=st->first_oid);
  assert(oid<=st->last_oid);
  uint32_t script_off=st->script_offs[oid-st->first_oid];
  if (script_off==0xffffffff) return NULL;
  return gme_get_ptr(gme,script_off);
}

struct gme_script_line*
gme_script_get(struct gme* gme,struct gme_script* s,uint16_t line) {
  assert(line<=s->lines);
  return gme_get_ptr(gme,s->pointers[line]);
}

static unsigned char* print_operand(unsigned char* p,FILE *f) {
  if (!*p) {
    if (fputc('$',f)==EOF) return NULL;
  }
  p++;
  if (fprintf(f,"%d",le16toh(*(uint16_t*) p))<0) return NULL;
  return p+2;
}

uint16_t gme_playlist_get(const struct gme_playlist* pl,uint16_t i) {
  assert(i<=pl->len);
  return pl->entries[i];
}

struct gme_playlist*
gme_playlistlist_get(struct gme* gme,const struct gme_playlistlist* pll,
                     uint16_t i) {
  assert(i<=pll->len);
  return gme_get_ptr(gme,pll->entries[i]);
}

struct gme_playlist*
gme_script_line_playlist(struct gme_script_line* line) {
  unsigned char* p=line->raw;
  p+=line->conditions*8;
  uint16_t actions=*(uint16_t*) p;
  p+=2+actions*7;
  return (struct gme_playlist*) p;
};

int gme_script_line_print(struct gme_script_line* line,FILE *f) {
  int i=line->conditions;
  unsigned char* p=line->raw;
  int err;
  fprintf(f,"  ");
  while (i--) {
    p=print_operand(p,f);
    if (!p) return 1;
    uint16_t operator=*(uint16_t*) p;
    p+=2;
    switch (operator) {
    case 0xfff9:
      err=fprintf(f,"==");
      break;
    case 0xfffb:
      err=fprintf(f,"<");
      break;
    case 0xfffd:
      err=fprintf(f,">=");
      break;
    case 0xffff:
      err=fprintf(f,"!=");
      break;
    default:
      fprintf(stderr,"Unkown operator 0x%x\n",operator);
      return 1;
    }
    if (err<0) return -err;
    p=print_operand(p,f);
    if (!p) return 1;
    if (i) {
      err=fprintf(f," && ");
      if (err<0) return -err;
    }
  }
  if (line->conditions) err=fputs(" ? ",f);
  fputc('{',f);
  if (err<0) return -err;
  
  struct gme_playlist* pl=gme_script_line_playlist(line);

  /* Number of actions */
  i=*(uint16_t*) p;
  p+=2;
  int has_raw=0;
  while (i--) {
    uint16_t r=*(uint16_t*) p;
    p+=2;
    uint16_t c=*(uint16_t*) p;
    p+=2;
    unsigned char t=*(p++);
    uint16_t m=*(uint16_t*) p;
    p+=2;
    switch (c) {
    case 0xfaff:
      fprintf(f," C;");
      break;
    case 0xfc00:
      {
        uint16_t from=m>>8;
        uint16_t to=m&0xff;
        assert(from<=to);
        fprintf(f," P(");
        while (from<=to) {
          fprintf(f,"%d",gme_playlist_get(pl,from));
          from++;
          if (from<=to) fprintf(f,",");
        }
        fprintf(f,");");
      }
      break;
    case 0xfd00:
      fprintf(f," G(%s%d);",t ? "" : "$",m);
      break;
    case 0xffe8:
      fprintf(f," P(%d);",gme_playlist_get(pl,m));
      break;
    case 0xfff0:
      fprintf(f," $%d+=%s%d;",r,t ? "" : "$",m);
      break;
    case 0xfff9:
      fprintf(f," $%d=%s%d;",r,t ? "" : "$",m);
      break;
    default:
      /* Unknown command => output raw */
      fprintf(f," R(0x%x,%d,%d,%d);",c,r,t,m);
      has_raw=1;
    }
  }
  if (has_raw) {
    /* print full playlist for raw command */
    fprintf(f," PL(");
    uint16_t i;
    for (i=0; i<pl->len; i++) {
      if (i) fprintf(f,",");
      fprintf(f,"%d",gme_playlist_get(pl,i));
    }
    fprintf(f,");");
  }
  err=fprintf(f," }\n");

  if (err<0) return -err;
  return 0;
}

uint32_t gme_media_table_count(struct gme* gme,struct gme_media_table* mt) {
  uint32_t myoff=((intptr_t) mt)-((intptr_t) gme->u.raw);
  return (mt->entries[0].off-myoff)/8;
}

int gme_media_table_is_ogg(struct gme* gme,struct gme_media_table* mt,uint16_t i)
{
  assert(i<gme_media_table_count(gme,mt));
  unsigned char* p=gme_get_ptr(gme,mt->entries[i].off);
  uint32_t len=mt->entries[i].len;
  unsigned char x=gme->magic_xor;
  return len && magic_xor(x,p)=='O';
}

int gme_media_table_write(struct gme* gme,struct gme_media_table* mt,
                          uint16_t i,FILE *dest)
{
  assert(i<gme_media_table_count(gme,mt));
  unsigned char* p=gme_get_ptr(gme,mt->entries[i].off);
  uint32_t len=mt->entries[i].len;
  unsigned char x=gme->magic_xor;
  while (len--) {
    int r=fputc(magic_xor(x,p++),dest);
    if (r==EOF) return 1;
  }
  return 0;
}

int gme_media_table_play(struct gme* gme,struct gme_media_table* mt,uint16_t i)
{
  assert(i<gme_media_table_count(gme,mt));
  unsigned char* p=gme_get_ptr(gme,mt->entries[i].off);
  uint32_t len=mt->entries[i].len;
  unsigned char x=gme->magic_xor;
  FILE *f;
  if (magic_xor(x,p)=='O') {
    f=popen("ogg123 -q -","w");
  } else {
    f=popen("aplay -q -","w");
  }
  if (!f) return 1;
  while (len--) {
    int r=fputc(magic_xor(x,p++),f);
    if (r==EOF) goto error1;
  }
  fclose(f);
  return 0;
 error1:
  fclose(f);
  return 1;
}

struct gme_game*
gme_games_get(struct gme_games_table* gt,struct gme* gme,uint16_t i) {
  assert(i<gt->len);
  return gme_get_ptr(gme,gt->game_offs[i]);
}

const unsigned char* gme_game_get_last_round_p(const struct gme_game* g) {
  const unsigned char* p=g->raw;
  if (le16toh(g->type)==6) return p+8;
  return p;
}

uint16_t gme_game_get_pre_last_round_count(const struct gme_game* g) {
  return get_uint16(gme_game_get_last_round_p(g));
}

uint16_t gme_game_get_repeat_oid(const struct gme_game* g) {
  return get_uint16(gme_game_get_last_round_p(g)+2);
}

uint16_t gme_game_get_u2(const struct gme_game* g,uint16_t i) {
  assert(i<3);
  return get_uint16(gme_game_get_last_round_p(g)+4+i*2);
}

struct gme_playlistlist* gme_game_get_playlistlist(const struct gme_game* g,struct gme* gme,uint16_t i) {
  assert(i<((g->type==6) ? 7 : 5));
  return (struct gme_playlistlist*)
    gme_get_ptr(gme,get_uint32(gme_game_get_last_round_p(g)+10+i*4));
}

struct gme_subgame*
gme_game_get_subgame(const struct gme_game* g,struct gme* gme,uint16_t i)
{
  assert(i<le16toh(g->subgames_len));
  const unsigned char* p=gme_game_get_last_round_p(g)+30+i*4;
  if (g->type==6) p+=8;
  return (struct gme_subgame*) gme_get_ptr(gme,get_uint32(p));
}

struct gme_subgame*
gme_game_get_bonusgame(const struct gme_game* g,struct gme* gme,uint16_t i)
{
  assert(i<le16toh(g->c));
  const unsigned char* p=gme_game_get_last_round_p(g)+30+
    (le16toh(g->subgames_len)+i)*4;
  if (g->type==6) p+=8;
  return (struct gme_subgame*) gme_get_ptr(gme,get_uint32(p));
}

const struct gme_scorelist*
gme_game_get_scorelist(const struct gme_game* g,struct gme* gme) {
  const unsigned char* p=gme_game_get_last_round_p(g)+30+
    (le16toh(g->subgames_len)+le16toh(g->c))*4;
  if (le16toh(g->type)==6) p+=8;
  return (struct gme_scorelist*) p;
}

int gme_oidlist_contains(const struct gme_oidlist* ol,uint16_t oid) {
  oid=htole16(oid);
  int i=ol->len;
  const uint16_t* p=ol->entries;
  while (i) {
    if (*p==oid) return 1;
    p++;
    i--;
  }
  return 0;
}

const struct gme_oidlist*
gme_subgame_get_oids(const struct gme_subgame* sb,uint16_t i)
{
  assert(i<3);
  const unsigned char* p=sb->raw+20;
  while (i--) p+=get_uint16(p)*2+2;
  return (const struct gme_oidlist*) p;
}

const struct gme_playlistlist*
gme_subgame_get_playlistlist(const struct gme_subgame* sb,
                             struct gme* gme,uint16_t i)
{
  assert(i<9);
  const unsigned char* p=sb->raw+20;
  /* Skip oidlists */
  p+=get_uint16(p)*2+2;
  p+=get_uint16(p)*2+2;
  p+=get_uint16(p)*2+2;
  return (const struct gme_playlistlist*) gme_get_ptr(gme,get_uint32(p+i*4));
}
