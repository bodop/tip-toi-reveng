%{

#include <stdlib.h>
#include <stdio.h>
#include <parser.h>
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
   0,0,0,0,0x3b,0,0,0,0,0,0,0,0,0,0,0,
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
   struct register_entry* e=(struct register_entry*)
     list_lookup((struct list_entry*) global_registers,registercmp,name);
   if (e) {
     free(name);
     return e->n;
   }
   MYERROR("Unknown register %s",name);
   free(name);
   return 0;
 }

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
  global_oids=NULL;

  sbuffer=malloc(BUFFER_BLOCK);
  sbuffer_allocated=BUFFER_BLOCK;
  sbuffer_used=0;

  toReturn=yyparse();

  list_free((struct list_entry*) global_registers,registerfree);

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
  FILE* cout=fopen("/tmp/out","w");
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
    media_off+=mediafiles_n*8;
    /* First print offsets and lengths */
    for (i=0; i<mediafiles_n; i++) {
      struct dirent* dirent=mediafiles[i];
      snprintf(fnbuffer,sizeof(fnbuffer),"%s/%s",mediadir,dirent->d_name);
      stat(fnbuffer,&statbuf);
      uint32_t s=statbuf.st_size;
      write32(cout,media_off);
      write32(cout,s);
      media_off+=s;
    }
    /* And the files itself */
    for (i=0; i<mediafiles_n; i++) {
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
 }
%union {
  uint32_t u32;
  uint16_t u16;
  char* name;
  unsigned char arr[8];
  struct gme_script_line* sl;
  struct gme_playlistlist* pll;
  struct gme_playlist* pl;
  struct gme_script* script;
}

%token FORMAT
%token PRODUCT LANGUAGE PUBLICATION
%token REGISTER
%token RAW_XOR MAGIC_XOR
%token WELCOME
%token AND ADD OPERATOR_EQ OPERATOR_GE OPERATOR_NEQ
%token NUM
%token <name> IDENTIFIER STRING
%type <arr> condition action command value
%type <u16> operator media
%type <u32> NUM scriptline
%type <sl> conditions actions actionlist
%type <pl> medias playlist
%type <pll> playlists playlistlist
%type <script> scriptlines
%destructor {
  free($$);
   } <name> <pll> <sl> <pl>
%%

start: product xor format publication language registers welcome
       scripttable;

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

registers: registerdef | registerdef registers;

registerdef: REGISTER registerlist ';';

registerlist: oneregister ',' registerlist | oneregister;

oneregister: IDENTIFIER {
  create_register(&global_registers,$1,0);
 }
| IDENTIFIER '=' NUM {
  create_register(&global_registers,$1,$3);
  };
             

welcome: /* empty */ | WELCOME playlistlist {
  gme.u.header.welcome_off=sbuffer_append($2,$2->len*4+2);
  free($2);
 };

playlistlist: '{' playlists '}' {
  $$=$2;
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

scripttable: onescript scripttable | onescript;

onescript: NUM '{' scriptlines '}' {
  if (list_lookup((struct list_entry*) global_oids,oidcmp,&$1)) {
    MYERROR("Duplicate definition of OID %d",$1);
  } else {
   struct oid_entry* entry=malloc(sizeof(struct oid_entry));
   entry->list.next=NULL;
   entry->oid=$1;
   entry->offset=sbuffer_append($3,le16toh($3->lines)*4+2);
   list_insert((struct list_entry**) &global_oids,oidcmp,
               (struct list_entry*) entry,&$1);
   free($3);
  }
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
| 'G' '(' value ')' {
  unsigned char* p=$$;
  *(uint16_t*) p=0;
  p+=2;
  *(uint16_t*) p=htole16(0xfd00);
  p+=2;
  memcpy(p,$3,3);
  }
;


