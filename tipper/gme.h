#ifndef GME_H
#define GME_H
#include <stdint.h>
#include <stdio.h>

#define GME_GAME_WELCOME 0
#define GME_GAME_NEXT_LEVEL 1
#define GME_GAME_BYE 2
#define GME_GAME_NEXT_ROUND 3
#define GME_GAME_LAST_ROUND 4
#define GME_SUBGAME_PLAY 0
#define GME_SUBGAME_INVALID 3
#define GME_SUBGAME_U4 4
#define GME_SUBGAME_U6 6
#define GME_SUBGAME_U7 7
#define GME_SUBGAME_U8 8
#define GME_SUBGAME_OK_OIDS 0
#define GME_SUBGAME_OK_PLAY 1
#define GME_SUBGAME_UNKNOWN_OIDS 1
#define GME_SUBGAME_UNKNOWN_PLAY 2
#define GME_SUBGAME_FALSE_OIDS 2
#define GME_SUBGAME_FALSE_PLAY 5

#pragma pack(push,1)
struct gme_header {
  uint32_t play_off;
  uint32_t media_off;
  uint32_t fixed238b;
  uint32_t extraScript_off;
  uint32_t games_off;
  uint32_t product_id;
  uint32_t regs_off;
  uint32_t raw_xor;
  char manufacturer[64]; /* CHOMPTECH DATA FORMAT CopyRight 2009 Ver2.x.yyy */
  uint32_t unknown_off;
  char fill[13];
  uint32_t welcome_off;
  char fill2[395];
};

struct gme_registers {
  uint16_t len;
  uint16_t regs[0]; // Dummy length
};

struct gme_oidlist {
  uint16_t len;
  uint16_t entries[0]; // Dummy length
};

struct gme_playlist {
  uint16_t len;
  uint16_t entries[0]; // Dummy length
};

struct gme_playlistlist {
  uint16_t len;
  uint32_t entries[0]; // Dummy length
};

struct gme_script_line {
  uint16_t conditions; /* Number of conditions */
  unsigned char raw[];
};

struct gme_script {
  uint16_t lines; /* Number of script lines */
  uint32_t pointers[]; /* Pointer to lines */
};

struct gme_script_table {
  uint32_t last_oid;
  uint32_t first_oid;
  uint32_t script_offs[];
};

struct gme_media_table {
  struct {
    uint32_t off;
    uint32_t len;
  } entries[0]; // Dummy length
};

struct gme_games_table {
  uint32_t len;
  uint32_t game_offs[]; /* points to game headers */
};

struct gme_game {
  uint16_t type;
  uint16_t subgames_len; /* Number of subgames */
  uint16_t rounds;
  uint16_t c; /* Number of bonus games */
  unsigned char raw[];
};

struct gme_scorelist {
  uint16_t scores[10]; /* Scores (in descending order) */
  uint32_t plls[10];   /* Pointer to playlistlists */
};

struct gme_subgame {
  unsigned char raw[0];
};

struct gme {
  uint32_t size;
  uint32_t magic_xor;
  union {
    struct gme_header header;
    unsigned char raw[0];
  } u;
};
#pragma pack(pop)

/**
 * @brief Loads a GME file.
 * @return whole file.
 */
struct gme*  gme_load(const char* path);

struct gme_script_table* gme_get_scripts(struct gme*);
struct gme_media_table* gme_get_media(struct gme*);
struct gme_registers* gme_get_registers(struct gme*);
struct gme_playlistlist* gme_get_welcome(struct gme*);
struct gme_games_table* gme_get_games(struct gme*);

struct gme_script*
gme_script_table_get(struct gme*,struct gme_script_table*,uint32_t oid);

struct gme_script_line*
gme_script_get(struct gme*,struct gme_script*,uint16_t line);
int gme_script_line_print(struct gme_script_line*,FILE*);
struct gme_playlist*
gme_script_line_playlist(struct gme_script_line*);
uint16_t gme_playlist_get(const struct gme_playlist*,uint16_t);
struct gme_playlist*
gme_playlistlist_get(struct gme*,const struct gme_playlistlist*,uint16_t);
uint32_t gme_media_table_count(struct gme*,struct gme_media_table*);
int gme_media_table_is_ogg(struct gme*,struct gme_media_table*,uint16_t);
int gme_media_table_write(struct gme*,struct gme_media_table*,uint16_t,FILE*);
int gme_media_table_play(struct gme*,struct gme_media_table*,uint16_t);
unsigned char magic_xor(unsigned char x,const unsigned char* p);
void* gme_get_ptr(struct gme* gme,uint32_t off);

struct gme_game* gme_games_get(struct gme_games_table*,struct gme*,uint16_t i);
uint16_t gme_game_get_pre_last_round_count(const struct gme_game* g);
uint16_t gme_game_get_repeat_oid(const struct gme_game* g);
/** Unknown array of length 3 */
uint16_t gme_game_get_u2(const struct gme_game* g,uint16_t i);
struct gme_playlistlist* gme_game_get_playlistlist(const struct gme_game* g,struct gme*,uint16_t i);
struct gme_subgame* gme_game_get_subgame(const struct gme_game* g,struct gme*,uint16_t i);
struct gme_subgame* gme_game_get_bonusgame(const struct gme_game* g,struct gme*,uint16_t i);
const struct gme_scorelist*
gme_game_get_scorelist(const struct gme_game*,struct gme*);

int gme_oidlist_contains(const struct gme_oidlist*,uint16_t oid);

const struct gme_oidlist*
gme_subgame_get_oids(const struct gme_subgame*,uint16_t);
const struct gme_playlistlist*
gme_subgame_get_playlistlist(const struct gme_subgame*,struct gme*,uint16_t);
#endif
