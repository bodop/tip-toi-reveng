#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <gme.h>
#include <string.h>
#include <sys/stat.h>

static int usage() {
  fprintf(stderr,"usage: tipdecode [-b basename] <gmefile>\n"
          "\n"
          "Decodes a TipToi GME file.\n"
          "\n"
          "options:\n"
          "\t-b basename output script as <basename>.tip and media in\n"
          "\t            subdirectory <basename>.\n"
          );
  return 1;
}

static void print_oidlist(FILE* out,const char* prefix,
                          const struct gme_oidlist* ol)
{
  if (prefix) fputs(prefix,out);
  fputc('{',out);
  uint16_t i;
  for (i=0; i<le16toh(ol->len); i++) {
    if (i) fputc(',',out);
    uint16_t oid=le16toh(ol->entries[i]);
    fprintf(out,"%d",le16toh(ol->entries[i]));
    /* Check for interval */
    uint16_t j;
    for (j=i+1; j<le16toh(ol->len); j++) {
      uint16_t oidj=le16toh(ol->entries[j]);
      if (oid+1!=oidj) break;
      oid=oidj;
    }
    j--;
    if (j>i) {
      fprintf(out,"-%d",oid);
      i=j;
    }
  }
  fputs("}\n",out);
}

static void inline print_playlist(FILE* out,const char* prefix,
                                  const struct gme_playlist* pl)
{
  if (prefix) fputs(prefix,out);
  fputc('{',out);
  uint16_t i;
  for (i=0; i<le16toh(pl->len); i++) {
    if (i) fputc(',',out);
    fprintf(out,"%d",le16toh(pl->entries[i]));
  }
  fputs("}\n",out);
}

static void print_playlistlist(FILE* out,const char* prefix,
                               const char* indent,
                               const struct gme_playlistlist* pll,
                               struct gme* gme)
{
  if (!pll || pll->len==0) return;
  if (prefix) fputs(prefix,out);
  fprintf(out,"{\n");
  uint16_t i;
  for (i=0; i<le16toh(pll->len); i++) {
    const struct gme_playlist* pl=gme_playlistlist_get(gme,pll,i);
    fputs("  ",out);
    print_playlist(out,indent,pl);
  }
  if (indent) fputs(indent,out);
  fputs("}\n",out);
}

static void print_oidplaylistlist(FILE* out,const char* name,
                                  const struct gme_oidlist* ol,
                                  const struct gme_playlistlist* pll,
                                  struct gme* gme)
{
  if (ol->len==0 && pll->len==0) return;
  fprintf(out,"    %s {\n",name);
  print_oidlist(out,"      oids ",ol);
  print_playlistlist(out,"      play ","      ",pll,gme);
  fputs("    }\n",out);
}

static void print_subgame(FILE* out,const char* prefix,
                          const struct gme_subgame* subgame,
                          struct gme* gme)
{
  fprintf(out,"  %s {\n",prefix);
  fprintf(out,"    u1 {");
  uint16_t ui;
  for (ui=0; ui<20; ui++) {
    if (ui) fputc(',',out);
    fprintf(out,"%d",subgame->raw[ui]);
  }
  fprintf(out,"}\n");
  print_playlistlist(out,"    play ","    ",
                     gme_subgame_get_playlistlist(subgame,gme,0),gme);
  print_playlistlist(out,"    invalid ","    ",
                     gme_subgame_get_playlistlist(subgame,gme,3),gme);
  print_oidplaylistlist(out,"ok",gme_subgame_get_oids(subgame,0),
                        gme_subgame_get_playlistlist(subgame,gme,1),
                        gme);
  print_oidplaylistlist(out,"unknown",gme_subgame_get_oids(subgame,1),
                        gme_subgame_get_playlistlist(subgame,gme,2),
                        gme);
  print_oidplaylistlist(out,"false",gme_subgame_get_oids(subgame,2),
                        gme_subgame_get_playlistlist(subgame,gme,5),
                        gme);
  print_playlistlist(out,"    u4 ","    ",
                     gme_subgame_get_playlistlist(subgame,gme,4),gme);
  print_playlistlist(out,"    u6 ","    ",
                     gme_subgame_get_playlistlist(subgame,gme,6),gme);
  print_playlistlist(out,"    u7 ","    ",
                     gme_subgame_get_playlistlist(subgame,gme,7),gme);
  print_playlistlist(out,"    u8 ","    ",
                     gme_subgame_get_playlistlist(subgame,gme,8),gme);
  fprintf(out,"  }\n");
}

int main(int argc,const char** argv) {

  const char* basename=NULL;

  /* Skip program name */
  argc--;
  argv++;

  if (argc>1) {
    if (argc==3 && !strcmp(argv[0],"-b")) {
      basename=argv[1];
      argc-=2;
      argv+=2;
    }
  }

  if (argc!=1) return usage();

  struct gme* gme=gme_load(argv[0]);

  if (!gme) {
    perror("Cannot load");
    return 1;
  }

  struct gme_script_table* st=gme_get_scripts(gme);

  FILE* out=stdout;
  if (basename) {
    int l=strlen(basename);
    char* buffer=alloca(l+5);
    memcpy(buffer,basename,l);
    strcpy(buffer+l,".tip");
    out=fopen(buffer,"w");
    if (!out) {
      perror("Cannot write script");
      return 1;
    }
  }

  fprintf(out,"product   %d;\n",gme->u.header.product_id);
  fprintf(out,"magic_xor 0x%2x;\n",gme->magic_xor);
  fprintf(out,"raw_xor   0x%2x;\n",gme->u.header.raw_xor);
  fprintf(out,"format    \"%.*s\";\n",gme->u.header.manufacturer[0],
          gme->u.header.manufacturer+1);
  char *d=gme->u.header.manufacturer+gme->u.header.manufacturer[0]+1;
  fprintf(out,"publication   \"%.8s\";\n",d);
  if (*(d+8)) fprintf(out,"language  \"%s\";\n",d+8);
  /* Registers */
  {
    const struct gme_registers* regs=gme_get_registers(gme);
    if (regs->len) {
      fprintf(out, "register ");
      int i;
      for (i=0; i<regs->len; i++) {
        if (i) fprintf(out,",");
        fprintf(out,"$%d",i);
        if (regs->regs[i]) {
          fprintf(out,"=%d",regs->regs[i]);
        }
      }
      fprintf(out,";\n");
    }
  }

  print_playlistlist(out,"welcome ",NULL,gme_get_welcome(gme),gme);

#if 1
  {
    uint16_t i;
    struct gme_games_table* games=gme_get_games(gme);
    fprintf(out,"# %d games\n",games->len);
    for (i=0; i<games->len; i++) {
      struct gme_game* game=gme_games_get(games,gme,i);
      if (le16toh(game->type)==253) {
        fprintf(out,"# game %d if of type 253\n",i);
        continue;
      }
      fprintf(out,"game %d {\n",i);
      fprintf(out,"  type %d\n",game->type);
      if (game->type==253) {
        fprintf(out,"}\n");
        continue;
      }
      fprintf(out,"  rounds %d\n",game->rounds);
      if (game->type==6) {
        fprintf(out,"  u1 {");
        uint16_t n=0;
        for (n=0; n<4; n++) {
          if (n) fputc(',',out);
          fprintf(out,"%d",game->raw[n]);
        };
        fprintf(out,"}\n");
      }
      fprintf(out,"  pre_last_round_count %d\n",gme_game_get_pre_last_round_count(game));
      fprintf(out,"  repeat_oid %d\n",gme_game_get_repeat_oid(game));
      {
        fprintf(out,"  u2 {%d",gme_game_get_u2(game,0));
        uint16_t n;
        for (n=1; n<3; n++) {
          fprintf(out,",%d",gme_game_get_u2(game,n));
        }
        fprintf(out,"}\n");
      }
      /* Print game playlistlists */
      {
        uint16_t pi;
        static const char* pllnames[]={
          "  welcome ",
          "  next_level ",
          "  bye ",
          "  next_round ",
          "  last_round ",
          "  ignored1 ",
          "  ignored2 "
        };
        for (pi=0; pi<((game->type==6) ? 7 : 5); pi++) {
          print_playlistlist(out,pllnames[pi],"  ",
                             gme_game_get_playlistlist(game,gme,pi),gme);
        }
      }
      /* Print subgames */
      {
        uint16_t si;
        for (si=0; si<le16toh(game->subgames_len); si++) {
          struct gme_subgame* subgame=gme_game_get_subgame(game,gme,si);
          print_subgame(out,"subgame",subgame,gme);
        }
        for (si=0; si<le16toh(game->c); si++) {
          struct gme_subgame* bonusgame=gme_game_get_bonusgame(game,gme,si);
          print_subgame(out,"bonusgame",bonusgame,gme);
        }
      }
      /* Print scores */
      {
        uint16_t si;
        const struct gme_scorelist* sl;
        char buffer[20];
        for (si=0; si<10; si++) {
          sl=gme_game_get_scorelist(game,gme);
          snprintf(buffer,sizeof(buffer),"  score %d ",sl->scores[si]);
          print_playlistlist(out,buffer,"  ",(struct gme_playlistlist*)
                             gme_get_ptr(gme,sl->plls[si]),gme);
        }
      }
      fprintf(out,"}\n");
    }
  }
#endif

  uint32_t i;
  for (i=st->first_oid; i<=st->last_oid; i++) {
    struct gme_script* s=gme_script_table_get(gme,st,i);
    if (!s) continue;
    fprintf(out,"%d {\n",i);
    int j;
    for (j=0; j<s->lines; j++) {
      struct gme_script_line* sl=gme_script_get(gme,s,j);
      gme_script_line_print(sl,out);
    }
    fprintf(out,"}\n");
  }

  /* Export media */
  struct stat buf;
  if (basename && stat(basename,&buf) && errno==ENOENT) {
    if (mkdir(basename,0777)) {
      perror("Cannot create media directory");
      exit(1);
    }
    struct gme_media_table* mt=gme_get_media(gme);
    if (!mt) {
      fprintf(stderr,"No media table");
      return 1;
    }
    int i=gme_media_table_count(gme,mt);
    char* buffer=malloc(strlen(basename)+20);
   
    while (i--) {
      snprintf(buffer,strlen(basename)+20,"%s/%d.%s",
               basename,i,gme_media_table_is_ogg(gme,mt,i) ? "ogg" : "wav");
      FILE* f=fopen(buffer,"w");
      if (!f) {
        perror("Cannot open file");
        exit(1);
      }
      if (gme_media_table_write(gme,mt,i,f)) {
        perror("Output failed");
        exit(1);
      }
      if (fclose(f)) {
        perror("Closing file");
        return 1;
      }
    }
  }

  return 0;
}
