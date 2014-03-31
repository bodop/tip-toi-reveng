#ifndef TIPPLAY_H
#define TIPPLAY_H

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdint.h>

typedef struct _tiptoi* tiptoi;
typedef struct _tipselector* tipselector;
typedef struct _mediaselector* mediaselector;

struct _tiptoi {
  fd_set listeners;
  fd_set writers;
  int max_set_fd;
  struct gme* gme;
  mediaselector m;
  tipselector selectors;
  struct gme_game* current_game;
  uint16_t round;
  uint16_t score; /* type 1 */
  uint16_t bonus; /* type 6 (type 1) */
  uint16_t match_i; /* type 40 */
  uint16_t subgames_len; /* Sizeof subgame_len */
  uint16_t* sg_shuff; /* Shuffled indexes */
  struct gme_registers* regs;
};

void tiptoi_play_oid(tiptoi t,uint32_t oid);

/**
 * @brief selector wants to sleep.
 */
#define TIP_SLEEP -1

/**
 * @brief selector want to be closed.
 */
#define TIP_CLOSE -2

typedef int (*onselectfunc)(tipselector t,tiptoi tip);

struct _tipselector {

  tipselector next;

  /**
   * @brief Executes code for this object
   * @return maxfd Maximum file descriptor or @c TIP_SLEEP or @c TIP_CLOSE.
   *-2 close me.
   * 
   */
  onselectfunc onselect;

};

void tipselector_init(tipselector ts,tiptoi t,onselectfunc onselect);

#endif
