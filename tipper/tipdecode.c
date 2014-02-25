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
    struct gme_registers* regs=gme_get_registers(gme);
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
  struct gme_playlistlist* pll=gme_get_welcome(gme);
  if (pll) {
    fprintf(out,"welcome {\n");
    int i,j;
    for (i=0; i<pll->len; i++) {
      struct gme_playlist* pl=gme_playlistlist_get(gme,pll,i);
      fprintf(out,"  {");
      for (j=0; j<pl->len; j++) {
        if (j) fputc(',',out);
        fprintf(out," %d",gme_playlist_get(pl,j));
      }
      fprintf(out," }\n");
    }
    fprintf(out,"}\n");
  }

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
