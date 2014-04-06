%{

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gme.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#define MYERROR(x,y...) fprintf(stderr,"%s:%d: " x "\n",filename,yylineno,##y)
#define BUFFER_BLOCK (1<<16)

/*
 * Reference yylex() and yytext from flex
 */
extern int yylex(void);
extern char *yytext;
extern int yylineno;
extern FILE* yyin;

 static const char* filename;
 static struct dirent** mediafiles;
 static int mediafiles_n;
 static int mediafiles_used;

 /* Playlist of the currently parse script line */
 struct gme_playlist* line_pl;

 static struct gme gme;
 static struct gme_registers *gme_registers;

 static const uint8_t known_xors[]={
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0x3b,0,0,0,0,0xad,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 };

 int yyparse();

/*
 * yyerror
 *
 * Used by bison to report error
 */
void yyerror(const char *s) {
  fprintf(stderr, "%s:%d: %s (%s)\n",filename,yylineno,s,yytext);
}

/* A linked list sorted by name */
 struct list_entry {
   struct list_entry *next;
 };

 typedef int (*cmpfunc)(const struct list_entry* entry,const void* value);
 typedef void (*freefunc)(struct list_entry* entry);

 static struct list_entry* list_lookup(struct list_entry *list,
                                       cmpfunc f,void *v)
 {
   while (list) {
     int i=f(list,v);
     if (!i) return list;
     if (i>0) return NULL;
     list=list->next;
   }
   return NULL;
 }

 /**
  * @brief Free memory of the list
  * @return @c NULL
  */
 static struct list_entry* list_free(struct list_entry *list,freefunc f) {
   while (list) {
     if (f) f(list);
     struct list_entry* n=list->next;
     free(list);
     list=n;
   }
   return list;
 }

 /**
  * @brief Insert into list
  *
  */
 static void list_insert(struct list_entry** root,cmpfunc f,
                         struct list_entry* entry,const void *value)
 {
   struct list_entry* list=*root;
   while (list) {
     int i=f(list,value);
     if (i>=0) break;
     root=&(list->next);
     list=list->next;
   }
   entry->next=list;
   *root=entry;
 }

 /* A linked list sorted by name */
 struct register_entry {
   struct list_entry list;
   char* name;
   int n;
 };

 static struct register_entry* global_registers;
 static struct register_entry* local_registers;
 
 static int registercmp(const struct list_entry* e,const void* name) {
   return strcmp(((struct register_entry*) e)->name,name);
 }

 static void registerfree(struct list_entry* e) {
   free(((struct register_entry*) e)->name);
 }

 static void create_register(struct register_entry** root,
                             char* name,uint16_t initial)
 {
   if (list_lookup((struct list_entry*) *root,registercmp,name)) {
     MYERROR("register %s already exists",name);
     free(name);
     return;
   }
   struct register_entry* entry=malloc(sizeof(struct register_entry));
   entry->list.next=NULL;
   entry->name=name;
   entry->n=gme_registers->len;
   list_insert((struct list_entry**) root,registercmp,
               (struct list_entry*) entry,name);
   gme_registers->regs[gme_registers->len]=initial;
   gme_registers->len++;
   if (!(gme_registers->len % 64)) {
     /* Allocate next chunk of memory */
     gme_registers=realloc(gme_registers,
                           sizeof(uint16_t)*(gme_registers->len+65));
   }
 }

 static int get_register(char* name) {
   int toReturn;
   struct register_entry* e=(struct register_entry*)
     list_lookup((struct list_entry*) local_registers,registercmp,name);
   if (e) {
     toReturn=e->n;
   } else {
     e=(struct register_entry*)
       list_lookup((struct list_entry*) global_registers,registercmp,name);
     if (e) {
       toReturn=e->n;
     } else {
       MYERROR("Unknown register %s",name);
       toReturn=0;
     }
   }
   free(name);
   return toReturn;
 }

 /* A linked list sorted by name */
 struct game_entry {
   struct list_entry list;
   char* name;
   int n;
 };

 static struct game_entry* games;
 static struct gme_games_table* gme_games;

 static int gamecmp(const struct list_entry* e,const void* name) {
   return strcmp(((struct game_entry*) e)->name,name);
 }

 static void gamefree(struct list_entry* e) {
   free(((struct game_entry*) e)->name);
 }

 static void create_game(char* name,uint32_t game_off)
 {
   uint32_t l=le32toh(gme_games->len);
   if (list_lookup((struct list_entry*) games,gamecmp,name)) {
     MYERROR("game %s already exists",name);
   } else {
     struct game_entry* entry=malloc(sizeof(struct game_entry));
     entry->list.next=NULL;
     entry->name=name;
     entry->n=l;
     list_insert((struct list_entry**) &games,gamecmp,
                 (struct list_entry*) entry,name);
   }
   gme_games=realloc(gme_games,8+4*l);
   gme_games->len=htole32(l+1);
   gme_games->game_offs[l]=game_off;
 }

 static int get_game(char* name) {
   struct game_entry* e=(struct game_entry*)
     list_lookup((struct list_entry*) games,gamecmp,name);
   if (e) {
     free(name);
     return e->n;
   }
   MYERROR("Unknown game %s",name);
   free(name);
   return 0;
 }

 struct gme_subgamelist {
   uint32_t len;
   uint32_t offs[];
 };

 static unsigned char* sbuffer;
 static size_t sbuffer_allocated,sbuffer_used;

 static uint32_t sbuffer_append(void* p,uint32_t len) {
   uint32_t toReturn=sizeof(struct gme_header)+sbuffer_used;
   if (sbuffer_used+len>sbuffer_allocated) {
     sbuffer_allocated+=(len/BUFFER_BLOCK+1)*BUFFER_BLOCK;
     sbuffer=realloc(sbuffer,sbuffer_allocated);
   }
   memcpy(sbuffer+sbuffer_used,p,len);
   sbuffer_used+=len;
   return toReturn;
 }

 static struct gme_oidlist* empty_oidlist() {
   struct gme_oidlist* ol=malloc(2);
   ol->len=0;
   return ol;
 }

 static uint32_t empty_list() {
   static uint32_t p=0;
   if (!p) {
     p=sbuffer_append(&p,2);
   }
   return p;
 }

 struct oid_entry {
   struct list_entry list;
   uint32_t oid;
   uint32_t offset; /* offset of gme_script */
 };

 static struct oid_entry* global_oids;

 static int oidcmp(const struct list_entry* entry,const void* value) {
   uint32_t v1=((struct oid_entry*) entry)->oid;
   uint32_t v2=*(uint32_t*) value;
   if (v1==v2) return 0;
   return v1<v2 ? -1 : 1;
 }

 static int get_media_index(const char* name) {
   int i;
   int l=strlen(name);
   for (i=0; i<mediafiles_n; i++) {
     struct dirent* d=mediafiles[i];
     if (!strncmp(name,d->d_name,l) && l+4==_D_EXACT_NAMLEN(d)) {
       /* Match found */
       if (i<mediafiles_used) {
         /* Already use => return index */
         return i;
       }
       /* Otherwise swap with first unused */
       mediafiles[i]=mediafiles[mediafiles_used];
       mediafiles[mediafiles_used]=d;
       return mediafiles_used++;
     }
   }
   /* Not found */
   MYERROR("media \"%s\" not found.",name);
   return -1;
 }

 /**
  * @return script offset
  */
 static uint32_t mkscriptline(struct gme_script_line* conditions,
                              struct gme_script_line* actions)
 {
   int conditions_size=2+conditions->conditions*8;
   uint32_t toReturn=sbuffer_append(conditions,conditions_size);
   free(conditions);
   sbuffer_append(actions,actions->conditions*7+2);
   free(actions);
   if (line_pl) {
     sbuffer_append(line_pl,line_pl->len*2+2);
     free(line_pl);
     line_pl=NULL;
   } else {
     static uint16_t zero=0;
     sbuffer_append(&zero,2);
   }
   return toReturn;
 }

 static int mediafilter(const struct dirent* dirent) {
   int i=strlen(dirent->d_name)-4;
   return i>0
     && (!strcasecmp(".ogg",dirent->d_name+i)
         || !strcasecmp(".wav",dirent->d_name+i));
 }

 static uint32_t checksum;

 /**
  * Write and update checksum.
  */
 static int cwrite(FILE* f,void* p,size_t len) {
   int toReturn=fwrite(p,len,1,f);
   unsigned char* c=p;
   while (len--) checksum+=*(c++);
   return toReturn!=1;
 }

 static void write32(FILE* f,uint32_t v) {
   v=htole32(v);
   cwrite(f,&v,sizeof(v));
 }

 static void copy(FILE* dest,FILE* src) {
   unsigned char buffer[4096];
   int r;
   while ((r=fread(buffer,1,sizeof(buffer),src))) {
     int i=r;
     unsigned char* p=buffer;
     while (i--) {
       *p=magic_xor(gme.magic_xor,p);
       p++;
     }
     cwrite(dest,buffer,r);
   };
 }

int tipparse(const char* fn) {

  int toReturn=3;

  filename=fn;
  char* mediadir;
  /* Scan media files */
  {
    int i=strlen(fn)-4;
    if (i<0 || strcasecmp(".tip",fn+i)) {
      fprintf(stderr,"Filename must end with '.tip'.");
      return 3;
    }
    mediadir=alloca(i+1);
    memcpy(mediadir,fn,i);
    mediadir[i]=0;
    mediafiles_n=scandir(mediadir,&mediafiles,mediafilter,alphasort);
    if (mediafiles_n<0) {
      perror("Cannot read media directory");
      return 3;
    }
  }

  yyin=fopen(fn,"r");
  if (!yyin) {
    perror("Cannot open input file");
    goto error1;
  }

  memset(&gme.u.header,0,sizeof(gme.u.header));
  gme.u.header.fixed238b=htole32(0x238b);

  /* create empty registers */
  gme_registers=malloc(sizeof(uint16_t)*65);
  gme_registers->len=0;
  line_pl=NULL;
  global_registers=NULL;
  local_registers=NULL;
  games=NULL;
  gme_games=malloc(4);
  gme_games->len=0;
  global_oids=NULL;

  sbuffer=malloc(BUFFER_BLOCK);
  sbuffer_allocated=BUFFER_BLOCK;
  sbuffer_used=0;

  toReturn=yyparse();

  list_free((struct list_entry*) global_registers,registerfree);
  list_free((struct list_entry*) games,gamefree);

  gme.u.header.regs_off=htole32(sbuffer_append
                                (gme_registers,
                                 le16toh(gme_registers->len)*2+2));
  free(gme_registers);

  uint32_t media_off=sbuffer_used+0x200;

  /* Do we need to write the scripts? */
  uint32_t first_oid=0,last_oid=0;
  if (global_oids) {
    gme.u.header.play_off=htole32(sbuffer_used+0x200);
    struct oid_entry* e=global_oids;
    first_oid=global_oids->oid;
    while (e->list.next) e=(struct oid_entry*) e->list.next;
    last_oid=e->oid;
    /* Move the media behind the scripts */
    media_off+=(last_oid-first_oid+3)*4;
  }
  
  gme.u.header.media_off=htole32(media_off);
  
  /* Now output */
  FILE* cout;
  {
    int l=strlen(fn);
    char* buff=alloca(l+1);
    strcpy(buff,fn);
    strcpy(buff+l-4,".gme");
    cout=fopen(buff,"w");
  }
  if (!cout) {
    perror("Cannot open output");
    goto error1;
  }

  /* Write header */
  if (cwrite(cout,&gme.u,sizeof (struct gme_header)))
  {
    perror("Write error");
    goto error2;
  }

  /* Write scripts */
  if (cwrite(cout,sbuffer,sbuffer_used)) {
    perror("Write error");
    goto error2;
  }

  /* Write script table */
  if (global_oids) {
    uint32_t oid=first_oid;
    write32(cout,last_oid);
    write32(cout,first_oid);
    while (global_oids) {
      while (oid<global_oids->oid) {
        /* Skip unused OIDs */
        write32(cout,0xffffffff);
        oid++;
      }
      write32(cout,global_oids->offset);
      oid++;
      struct oid_entry* n=(struct oid_entry*) global_oids->list.next;
      free(global_oids);
      global_oids=n;
    }
  }

  /* Write media table */
  {
    struct stat statbuf;
    char fnbuffer[256];
    int i;
    /* Skip space for media table itself */
    media_off+=mediafiles_used*8;
    /* First print offsets and lengths */
    for (i=0; i<mediafiles_used; i++) {
      struct dirent* dirent=mediafiles[i];
      snprintf(fnbuffer,sizeof(fnbuffer),"%s/%s",mediadir,dirent->d_name);
      stat(fnbuffer,&statbuf);
      uint32_t s=statbuf.st_size;
      write32(cout,media_off);
      write32(cout,s);
      media_off+=s;
    }
    /* And the files itself */
    for (i=0; i<mediafiles_used; i++) {
      struct dirent* dirent=mediafiles[i];
      snprintf(fnbuffer,sizeof(fnbuffer),"%s/%s",mediadir,dirent->d_name);
      FILE* m=fopen(fnbuffer,"r");
      copy(cout,m);
      fclose(m);
    }
  }

  write32(cout,checksum);
  
 error2:
  fclose(cout);
 error1:
  free(sbuffer);

  /* Free directory list */
  while (mediafiles_n--) free(mediafiles[mediafiles_n]);
  free(mediafiles);
  
  return toReturn;
}

%}

%error_verbose

%code requires {
#include <stdint.h>

 struct score {
   uint16_t len;
   uint16_t value[10];
   uint32_t play[10];
 };

 }
%union {
  struct {
    uint16_t rounds;
    uint16_t entry_score;
    uint16_t u1;
    uint16_t u2;
  } bonus;
  uint32_t u32;
  uint16_t u16;
  char* name;
  unsigned char arr[20];
  struct gme_script_line* sl;
  struct gme_subgamelist* sgl;
  struct gme_playlistlist* pll;
  struct gme_playlist* pl;
  struct gme_oidlist* ol;
  struct gme_script* script;
  struct score score;
  struct {
    struct gme_oidlist* oidlist;
    uint32_t playlist;
  } oidplaylist;
}

%token FORMAT
%token PRODUCT LANGUAGE PUBLICATION
%token REGISTER
%token RAW_XOR MAGIC_XOR
%token WELCOME
%token AND ADD OPERATOR_EQ OPERATOR_GE OPERATOR_NEQ
%token NUM
%token ATTEMPTS BONUS ENTRY_SCORE BONUSGAME BYE GAME NEXT_BONUS_ROUND
       LAST_BONUS_ROUND ROUNDS TYPE PRE_LAST_ROUND_COUNT
%token NEXT_LEVEL NEXT_ROUND LAST_ROUND SCORE SUBGAME PLAY INVALID OK FALSE
%token OIDS REPEAT_OID U0 U1 U2 U3 U4 U6 U7 U8 UNKNOWN
%token <name> IDENTIFIER STRING
%type <arr> condition action command value u2
%type <u16> bonus_u2 operator media type rounds pre_last_round_count repeat_oid
            sg_u0 sg_u3 sg_attempts
%type <u32> NUM scriptline subgame bonusgame
 /* some playlistlists */
%type <u32> bye playlistlist welcome next_level next_round last_round
            next_bonus_round last_bonus_round play invalid sgu4 sgu6 sgu7 sgu8
%type <ol> oids oidlist oidlistmember
%type <sl> conditions actions actionlist
%type <pl> medias playlist
%type <pll> playlists
%type <score> score scores
%type <oidplaylist> oidplaylist sgok sgunknown sgfalse
%type <script> scriptlines
%type <name> gameid
%type <sgl> subgames bonusgames
%type <bonus> bonus
%destructor {
  free($$);
 } <name> <pll> <sl> <pl> <ol> <sgl>;
%destructor {
  free($$.oidlist);
 } <oidlist>;
%%

start: product xor format publication language global_registers welcome
games scripttable {
  gme.u.header.welcome_off=$7;
  /* Write type 253 game */
  static uint16_t c253=htole16(253);
  uint32_t lg=sbuffer_append(&c253,2);
  uint32_t l=le32toh(gme_games->len);
  gme_games->len=htole32(l+1);
  gme.u.header.games_off=sbuffer_append(gme_games,4+4*l);
  sbuffer_append(&lg,4);
 }

product : PRODUCT NUM ';' {
 gme.u.header.product_id=$2;
 };

format: /* no format string */ | FORMAT STRING ';' {
  int l=strlen($2);
  char *p=gme.u.header.manufacturer+1;
  if (l>54) {
    /* data and language would hit the unknown_off */
    MYERROR("format to long");
  } else {
    gme.u.header.manufacturer[0]=l;
    memcpy(p,$2,l);
    p+=l;
  }
  free($2);
  /* We add a default publication date here */
  {
    time_t t;
    time(&t);
    struct tm* tm=localtime(&t);
    strftime(p,9,"%Y%m%d",tm);
  }
 };

xor: /* empty */ {
  gme.u.header.raw_xor=0x34;
  gme.magic_xor=0x3b;
} 
| magicxor rawxor 
| rawxor {
  if (!(gme.magic_xor=known_xors[gme.u.header.raw_xor])) {
    fprintf(stderr,"%s:%d: Unknown raw xor\n",filename,yylineno);
  }
 };

rawxor: RAW_XOR NUM ';' {
  if ($2>255) {
    fprintf(stderr,"%s:%d: xor too big.\n",filename,yylineno);
  } else {
    gme.u.header.raw_xor=$2;
  }
 };

magicxor: MAGIC_XOR NUM ';' {
  if ($2>255) {
    fprintf(stderr,"%s:%d: xor too big.\n",filename,yylineno);
  } else {
    gme.magic_xor=$2;
  }
 };

language: /* no language */ | LANGUAGE STRING ';' {
  char* p=gme.u.header.manufacturer;
  int l=strlen($2);
  if (*p+10+l>64) {
    MYERROR("language too long");
  } else {
    p+=*p+9;
    memcpy(p,$2,l);
  }
  free($2);
 };

publication: /* no publication date */ | PUBLICATION STRING ';' {
  if (strlen($2)!=8) {
    fprintf(stderr,"%s:%d: date must be 8 characters\n",filename,yylineno);
  } else {
    char* p=gme.u.header.manufacturer;
    p+=*p+1;
    memcpy(p,$2,8);
  }
  free($2);
 };

global_registers: global_registerdef | global_registers global_registerdef;

global_registerdef: REGISTER global_registerlist ';';

global_registerlist: global_registerlist ',' global_oneregister | global_oneregister;

global_oneregister: IDENTIFIER {
  create_register(&global_registers,$1,0);
 }
| IDENTIFIER '=' NUM {
  create_register(&global_registers,$1,$3);
  };
             

local_registers: /* empty */ | local_registerdef | local_registers local_registerdef;

local_registerdef: REGISTER local_registerlist ';';

local_registerlist: local_registerlist ',' local_oneregister | local_oneregister;

local_oneregister: IDENTIFIER {
  create_register(&local_registers,$1,0);
 }
| IDENTIFIER '=' NUM {
  create_register(&local_registers,$1,$3);
  };
             

welcome: /* empty */ {
  $$=empty_list();
} | WELCOME playlistlist {
  $$=$2;
 };

next_level: /* empty */ {
  $$=empty_list();
}
| NEXT_LEVEL playlistlist {
  $$=$2;
 }

bye: /* empty */ {
  $$=empty_list();
}
| BYE playlistlist {
  $$=$2;
 }

next_round: /* empty */ {
  $$=empty_list();
}
| NEXT_ROUND playlistlist {
  $$=$2;
 }

last_round: /* empty */ {
  $$=empty_list();
}
| LAST_ROUND playlistlist {
  $$=$2;
 }

next_bonus_round: /* empty */ {
  $$=empty_list();
}
| NEXT_BONUS_ROUND playlistlist {
  $$=$2;
 }

last_bonus_round: /* empty */ {
  $$=empty_list();
}
| LAST_BONUS_ROUND playlistlist {
  $$=$2;
 }

play: /* empty */ {
  $$=empty_list();
}
| PLAY playlistlist {
  $$=$2;
 }

invalid: /* empty */ {
  $$=empty_list();
}
| INVALID playlistlist {
  $$=$2;
 }

games: /* empty */ | games game | game;

game: GAME gameid '{' type rounds bonus pre_last_round_count repeat_oid u2
    welcome next_level bye next_round last_round next_bonus_round last_bonus_round
    subgames bonusgames scores
'}' {
  struct gme_game g;
  g.type=htole16($4);
  g.subgames_len=$17->len;
  g.rounds=htole16($5);
  g.c=$18->len;
  uint32_t g_off=sbuffer_append(&g,8);
  if ($4==6) {
    sbuffer_append(&$6,8);
  }
  sbuffer_append(&$7,2); /* pre_last_round_count */
  sbuffer_append(&$8,2); /* repeat oid */
  sbuffer_append($9,6);  /* u2 */
  sbuffer_append(&$10,4); /* welcome */
  sbuffer_append(&$11,4); /* next level */
  sbuffer_append(&$12,4); /* bye */
  sbuffer_append(&$13,4); /* next_round */
  sbuffer_append(&$14,4); /* last_round */
  if ($4==6) {
    sbuffer_append(&$15,4); /* ignored1 */
    sbuffer_append(&$16,4); /* ignored2 */
  }
  uint32_t* p=$17->offs; /* subgames */
  uint16_t l=le16toh($17->len);
  while (l--) {
    sbuffer_append(p,4);
    p++;
  }
  p=$18->offs; /* bonusgames */
  l=le16toh($18->len);
  while (l--) {
    sbuffer_append(p,4);
    p++;
  }
  uint16_t scores[10];
  uint32_t playlists[10];
  /* TODO use real scores */
  for (l=0; l<$19.len; l++) {
    scores[l]=$19.value[l];
    playlists[l]=$19.play[l];
  }
  while (l<10) {
    scores[l]=0;
    playlists[l]=empty_list();
    l++;
  }
  uint16_t* p1=scores;
  for (l=0; l<10; l++) {
    sbuffer_append(p1,2);
    p1++;
  }
  p=playlists;
  for (l=0; l<10; l++) {
    sbuffer_append(p,4);
    p++;
  }
  create_game($2,g_off);
  free($17);
  free($18);
 };

/* TODO */
scores: /* empty */ {
  $$.len=0;
}
| scores score {
  $$=$1;
  if ($$.len==10) {
    MYERROR("Only ten scores allowed per game.");
  } else {
    $$.value[$$.len]=$2.value[0];
    $$.play[$$.len]=$2.play[0];
    $$.len++;
  }
 }
| score {
  $$=$1;
  };

score: SCORE NUM playlistlist {
  $$.len=1;
  $$.value[0]=$2;
  $$.play[0]=$3;
 }


subgames: /* empty */ {
  $$=malloc(4);
  $$->len=0;
}
| subgames subgame {
  uint32_t l=le32toh($1->len);
  $$=realloc($1,8+4*l);
  $$->len=htole32(l+1);
  $$->offs[l]=$2;
 }
| subgame {
  $$=malloc(8);
  $$->len=htole32(1);
  $$->offs[0]=$1;
  };

subgame: SUBGAME '{' sg_attempts play invalid sgok sgunknown sgfalse sgu4 sgu6 sgu7 sgu8 '}' {
  uint16_t zero=0;
  $$=sbuffer_append(&zero,2); // u0
  sbuffer_append(&zero,2); // u1
  sbuffer_append(&zero,2); // u2
  sbuffer_append(&zero,2); // u3
  sbuffer_append(&$3,2); // attempts
  sbuffer_append(&zero,2); // u5
  sbuffer_append(&zero,2); // u6
  sbuffer_append(&zero,2); // u7
  sbuffer_append(&zero,2); // u8
  sbuffer_append(&zero,2); // u9
  sbuffer_append($6.oidlist,2+2*(le16toh($6.oidlist->len))); /* OK oids */
  free($6.oidlist);
  sbuffer_append($7.oidlist,2+2*(le16toh($7.oidlist->len))); /* unknown oids */
  free($7.oidlist);
  sbuffer_append($8.oidlist,2+2*(le16toh($8.oidlist->len))); /* false oids */
  free($8.oidlist);
  sbuffer_append(&$4,4);
  sbuffer_append(&$6.playlist,4);
  sbuffer_append(&$7.playlist,4);
  sbuffer_append(&$5,4);
  sbuffer_append(&$9,4);
  sbuffer_append(&$8.playlist,4);
  sbuffer_append(&$10,4);
  sbuffer_append(&$11,4);
  sbuffer_append(&$12,4);
 }

bonusgames: /* empty */ {
  $$=malloc(4);
  $$->len=0;
}
| bonusgames bonusgame {
  uint32_t l=le32toh($1->len);
  $$=realloc($1,8+4*l);
  $$->len=htole32(l+1);
  $$->offs[l]=$2;
 }
| bonusgame {
  $$=malloc(8);
  $$->len=htole32(1);
  $$->offs[0]=$1;
  };

bonusgame: BONUSGAME '{' sg_u0 sg_u3 sg_attempts play invalid sgok sgunknown sgfalse sgu4 sgu6 sgu7 sgu8 '}'
 {
  uint16_t zero=0;
  $$=sbuffer_append(&$3,2); // u0
  sbuffer_append(&zero,2); // u1
  sbuffer_append(&zero,2); // u2
  sbuffer_append(&$4,2); // u3
  sbuffer_append(&$5,2); // attempts
  sbuffer_append(&zero,2); // u5
  sbuffer_append(&zero,2); // u6
  sbuffer_append(&zero,2); // u7
  sbuffer_append(&zero,2); // u8
  sbuffer_append(&zero,2); // u9
  sbuffer_append($8.oidlist,2+2*(le16toh($8.oidlist->len))); /* OK oids */
  free($8.oidlist);
  sbuffer_append($9.oidlist,2+2*(le16toh($9.oidlist->len))); /* unknown oids */
  free($9.oidlist);
  sbuffer_append($10.oidlist,2+2*(le16toh($10.oidlist->len))); /* false oids */
  free($10.oidlist);
  sbuffer_append(&$6,4);
  sbuffer_append(&$8.playlist,4);
  sbuffer_append(&$9.playlist,4);
  sbuffer_append(&$7,4);
  sbuffer_append(&$11,4);
  sbuffer_append(&$10.playlist,4);
  sbuffer_append(&$12,4);
  sbuffer_append(&$13,4);
  sbuffer_append(&$14,4);
 };

sg_u0: /* EMPTY */ {
  $$=0;
} 
| U0 NUM {
  $$=htole16($2);
 }

sg_u3: /* EMPTY */ {
  $$=0;
} 
| U3 NUM {
  $$=htole16($2);
 }

sg_attempts: /* EMPTY */ {
  $$=1;
} 
| ATTEMPTS NUM {
  $$=htole16($2);
 }

sgok: /* empty */ {
  $$.oidlist=empty_oidlist();
  $$.playlist=empty_list();
}
| OK oidplaylist {
  $$=$2;
 };
sgunknown: /* empty */ {
  $$.oidlist=empty_oidlist();
  $$.playlist=empty_list();
}
| UNKNOWN oidplaylist {
  $$=$2;
 };
sgfalse: FALSE oidplaylist {
  $$=$2;
 };

sgu4: /* empty */ {
  $$=empty_list();
}
| U4 playlistlist {
  $$=$2;
 }

sgu6: /* empty */ {
  $$=empty_list();
}
| U6 playlistlist {
  $$=$2;
 }

sgu7: /* empty */ {
  $$=empty_list();
}
| U7 playlistlist {
  $$=$2;
 }

sgu8: /* empty */ {
  $$=empty_list();
}
| U8 playlistlist {
  $$=$2;
 }

oidplaylist: '{' oids play '}' {
  $$.oidlist=$2;
  $$.playlist=$3;
 };

oids: /* empty */ {
  $$=empty_oidlist();
}
| OIDS '{' oidlist '}' {
  $$=$3;
  };

oidlist: /* empty */ {
  $$=empty_oidlist();
}
| oidlist ',' oidlistmember {
  uint16_t l1=le16toh($1->len);
  uint16_t l3=le16toh($3->len);
  $$=(struct gme_oidlist*) realloc($1,2+2*(l1+l3));
  $$->len=htole16(l1+l3);
  memcpy($$->entries+l1,$3->entries,l3*2);
  free($3);
  }
| oidlistmember {
  $$=$1;
  };

oidlistmember: NUM {
  $$=(struct gme_oidlist*) malloc(4);
  $$->len=htole16(1);
  $$->entries[0]=htole16($1);
 }
| NUM '-' NUM {
  if ($3<$1) MYERROR("OIDs not increasing");
  uint16_t l=$3-$1+1;
  $$=(struct gme_oidlist*) malloc(2+l*2);
  $$->len=htole16(l);
  uint16_t v=$1;
  uint16_t* p=$$->entries;
  while (l--) {
    *(p++)=v++;
  }
  };

type: TYPE NUM { $$=$2; }

rounds: ROUNDS NUM { $$=$2; }

gameid: NUM {
  char buffer[16];
  snprintf(buffer,sizeof(buffer),"%d",$1);
  $$=strdup(buffer);
 }
| STRING {
  $$=$1;
  };

pre_last_round_count: PRE_LAST_ROUND_COUNT NUM { $$=$2; };

repeat_oid: REPEAT_OID NUM { $$=$2; };

bonus : /* EMPTY */ {
  $$.rounds=0;
  $$.entry_score=0;
  $$.u1=0;
  $$.u2=0;
}
| BONUS '{' ROUNDS NUM ENTRY_SCORE NUM bonus_u2 '}' {
  $$.rounds=htole16($4);
  $$.entry_score=htole16($6);
  $$.u1=htole16(0);
  $$.u2=htole16($7);
}

bonus_u2 : /* EMPTY */ {
  $$=0;
} | U2 NUM {
  $$=$2;
 }

u2: U2 '{' NUM ',' NUM ',' NUM '}' {
  unsigned char* p=$$;
  *(p++)=$3;
  *(p++)=$5;
  *(p++)=$7;
 };

playlistlist: '{' playlists '}' {
  $$=sbuffer_append($2,$2->len*4+2);
  free($2);
 };

playlists: playlists playlist {
  uint16_t l=le16toh($1->len);
  $$=realloc($1,l*4+6);
  $$->entries[l]=sbuffer_append($2,le16toh($2->len)*2+2);
  $$->len=htole16(l+1);
  free($2);
 }
| playlist {
  $$=malloc(6);
  $$->entries[0]=sbuffer_append($1,le16toh($1->len)*2+2);
  $$->len=htole16(1);
  free($1);
  };

playlist: '{' medias '}' {
  $$=$2;
 };

medias: medias ',' media {
  uint16_t l=le16toh($1->len);
  $1->len=htole16(l+1);
  $$=realloc($1,l*2+4);
  $$->entries[l]=$3;
 }
| media {
  $$=malloc(4);
  $$->len=htole16(1);
  $$->entries[0]=htole16($1);
 }

media: STRING {
  $$=get_media_index($1);
  free($1);
 }
| NUM {
  char buffer[10];
  snprintf(buffer,sizeof(buffer),"%d",$1);
  $$=get_media_index(buffer);
  };

scripttable: scripttable onescript | onescript;

onescript: NUM '{' local_registers scriptlines '}' {
  if (list_lookup((struct list_entry*) global_oids,oidcmp,&$1)) {
    MYERROR("Duplicate definition of OID %d",$1);
  } else {
   struct oid_entry* entry=malloc(sizeof(struct oid_entry));
   entry->list.next=NULL;
   entry->oid=$1;
   entry->offset=sbuffer_append($4,le16toh($4->lines)*4+2);
   list_insert((struct list_entry**) &global_oids,oidcmp,
               (struct list_entry*) entry,&$1);
   free($4);
  }
  list_free((struct list_entry*) local_registers,registerfree);
  local_registers=NULL;
 };

scriptlines: scriptlines scriptline {
  uint16_t n=le16toh($1->lines);
  $$=realloc($1,n*4+6);
  $$->pointers[n]=htole32($2);
  $$->lines=htole16(n+1);
 } 
| scriptline {
  $$=malloc(6);
  $$->lines=htole16(1);
  $$->pointers[0]=htole32($1);
  };

scriptline: conditions '?' actionlist {
  $$=mkscriptline($1,$3);
 }
| actionlist {
  /* Construct empty conditions */
  struct gme_script_line* conditions=malloc(2);
  memset(conditions,0,2);
  $$=mkscriptline(conditions,$1);
  };

conditions: conditions AND condition {
  int i=($1->conditions*8); 
  $$=realloc($1,i+10);
  $$->conditions++;
  memcpy($$->raw+i,$3,8);
 }
| condition {
  $$=malloc(10);
  $$->conditions=1;
  memcpy($$->raw,$1,8);
  };
condition: IDENTIFIER operator value {
  unsigned char* p=$$;
  *(p++)=0;
  *(uint16_t*) p=htole16(get_register($1));
  p+=2;
  *(uint16_t*) p=htole16($2);
  p+=2;
  memcpy(p,$3,3);
 };

value: IDENTIFIER {
  uint16_t i=htole16(get_register($1));
  $$[0]=0;
  memcpy($$+1,&i,2);
 }
| NUM {
  uint16_t i=htole16($1);
  $$[0]=1;
  memcpy($$+1,&i,2);
  };

operator: OPERATOR_EQ {
  $$=0xfff9;
 }
| '<' {
  $$=0xfffb;
  }
| OPERATOR_GE {
  $$=0xfffd;
  }
| OPERATOR_NEQ {
  $$=0xffff;
  };

actionlist: '{' actions '}' {
  $$=$2;
 } 
| '{' '}' {
  $$=malloc(2);
  memset($$,0,2);
  };

actions: actions action {
  int i=($1->conditions*7); 
  $$=realloc($1,i+9);
  $$->conditions++;
  memcpy($$->raw+i,$2,7);
 }
| action {
  $$=malloc(9);
  $$->conditions=1;
  memcpy($$->raw,$1,7);
  };
action: command ';' {
  memcpy($$,$1,7);
 };
command: 'P' '(' medias ')' {
  int l3=le16toh($3->len);
  if (l3==1) {
    uint16_t i;
    if (!line_pl) {
      /* first media in this script line */
      line_pl=$3;
      i=0;
    } else {
      /* Already present? */
      uint16_t m=$3->entries[0];
      uint16_t ll=le16toh(line_pl->len);
      for (i=0; i<ll; i++) {
        if (m==line_pl->entries[i]) break;
      }
      if (i==ll) {
        /* Not found => append */
        line_pl=realloc(line_pl,(ll+2)*2);
        line_pl->entries[ll]=m;
        line_pl->len=htole16(ll+1);
      }
      free($3);
    }
    unsigned char* p=$$;
    *(uint16_t*) p=0;
    p+=2;
    *(uint16_t*) p=htole16(0xffe8);
    p+=2;
    *(p++)=1;
    *(uint16_t*) p=htole16(i);
  } else {
    uint16_t from;
    if (!line_pl) {
      /* first media in this script line */
      line_pl=$3;
      from=0;
    } else {
      /* Already present? */
      uint16_t ll=le16toh(line_pl->len);
      for (from=0; from+l3<ll; from++) {
        if (!memcmp($3->entries,line_pl->entries+from,l3*2)) break;
      }
      if (from+l3>=ll) {
        /* Not found => append */
        line_pl=realloc(line_pl,(ll+l3+1)*2);
        memcpy(line_pl->entries+ll,$3->entries,l3*2);
        line_pl->len=htole16(ll+l3);
        from=ll;
      }
      free($3);
    }
    unsigned char* p=$$;
    *(uint16_t*) p=0;
    p+=2;
    *(uint16_t*) p=htole16(0xfc00);
    p+=2;
    *(p++)=1;
    *(p++)=from+l3-1;
    *(p++)=from;
  }
 }
| IDENTIFIER '=' value {
  unsigned char* p=$$;
  *(uint16_t*) p=htole16(get_register($1));
  p+=2;
  *(uint16_t*) p=htole16(0xfff9);
  p+=2;
  memcpy(p,$3,3);
  }
| IDENTIFIER ADD value {
  unsigned char* p=$$;
  *(uint16_t*) p=htole16(get_register($1));
  p+=2;
  *(uint16_t*) p=htole16(0xfff0);
  p+=2;
  memcpy(p,$3,3);
 }
| 'C' {
  unsigned char* p=$$;
  *(uint16_t*) p=0;
  p+=2;
  *(uint16_t*) p=htole16(0xfaff);
  p+=2;
  memset(p,0,3);
  }
| 'G' '(' gameid ')' {
  unsigned char* p=$$;
  *(uint16_t*) p=0;
  p+=2;
  *(uint16_t*) p=htole16(0xfd00);
  p+=2;
  uint16_t g=htole16(get_game($3));
  *(p++)=1;
  memcpy(p,&g,2);
  }
;


