
//Chase NES game by Shiru (shiru@mail.ru) 01'12
//Feel free to do anything you want with this code, consider it Public Domain
//This game is an example for my article Programming NES games in C
//Ported to SMS for 8bitworkshop by @sehugg 08'19

#include <cv.h>
#include <cvu.h>
#include <string.h>
#include <stdlib.h>

#include "common.h"
//#link "common.c"

#include "PSGlib.h"
//#link "PSGlib.c"

///// pattern table data

extern const unsigned char TILESET[8192];
//#link "tileset.c"

///// nametable data

#include "data.h"
//#link "data.c"
//#resource "title_nam.h"
//#resource "level_nam.h"
//#resource "gameover_nam.h"
//#resource "welldone_nam.h"
//#resource "level1_nam.h"
//#resource "level2_nam.h"
//#resource "level3_nam.h"
//#resource "level4_nam.h"
//#resource "level5_nam.h"

///// music and SFX

//#link "musicdata.c"
extern unsigned char MUSIC_TITLE[0];
extern unsigned char SFX_START[0];

///// NES compatibility macros

#define NAMETABLE_A IMAGE
#define NAMETABLE_C IMAGE
#define BYTES_PER_ROW 64
#define BYTES_PER_CELL 2
#define NTADR(x,y) ((y)*BYTES_PER_ROW + (x)*BYTES_PER_CELL + IMAGE)
#define vram_adr(a) cv_set_write_vram_address(a)
#define ppu_wait_frame() wait_vsync()
#define scroll(x,y) cv_set_hscroll(x); cv_set_vscroll(y);
#define music_play(i)
#define music_stop()
#define music_pause(b)
#define sfx_play(i,j)
#define pal_bright(x)
#define ppu_off() cv_set_screen_active(false);
#define ppu_on_bg() cv_set_screen_active(true); oam_clear()
#define ppu_on_all() cv_set_screen_active(true);
#define NT_UPD_EOF 0xff
#define PAD_START CV_FIRE_0
#define PAD_LEFT CV_LEFT
#define PAD_RIGHT CV_RIGHT
#define PAD_UP CV_UP
#define PAD_DOWN CV_DOWN
#define bank_bg(b)
#define bank_spr(b)
#define rand8() (rand() & 0xff)
#define FALSE 0
#define TRUE 1
#define famitone_init(data)
#define sfx_init(data)
#define nmi_set_callback(cb)

void vram_put(unsigned char n) {
  cv_voutb(n);
  cv_voutb(0); // TODO
}
void vram_write(const unsigned char *src, unsigned int size) {
  while (size--) {
    vram_put(*src++);
  }
}
void vram_fill(byte ch, word len) {
  while (len--) {
    vram_put(ch);
  }
}
void vram_unrle(const byte* rle) {
  byte tag = *rle++;
  byte data = 0;
  byte count;
  while (1) {
    if (*rle == tag) {
      ++rle;
      count = *rle++;
      if (!count) break;
      vram_fill(data, count);
    } else {
      vram_put(data = *rle++);
    }
  }
  rle=rle;
}
unsigned char pad_state(unsigned char pad) {
  struct cv_controller_state state;
  cv_get_controller_state(&state, pad);
  return state.joystick;
}
unsigned char pad_trigger(unsigned char pad) {
  return pad_state(pad);
}
void oam_clear(void) {
  cvu_vmemset(SPRITES, 240, 64);
}
unsigned char oam_meta_spr(unsigned char x, unsigned char y,
                           unsigned char sprid, 
                           const unsigned char *data) {
  struct cvu_sprite4 spr;
  while (*data != 128) {
    spr.x = x + (sbyte)*data++;
    spr.y = y + (sbyte)*data++;
    spr.name = *data++;
    *data++; // skip attr?
    cvu_set_sprite4(SPRITES, sprid++, &spr);
  }
  return sprid;
}

unsigned char* update_ptr = NULL;

void set_vram_update(unsigned char *buf) {
  update_ptr = buf;
}
void flush_vram_update() {
  byte hi,lo,data;
  unsigned char* buf = update_ptr;
  if (!buf) return;
  // TODO: bug in compiler?
  while (1) { // (*buf != NT_UPD_EOF) {
    hi = *buf++;
    if (hi == NT_UPD_EOF) break;
    lo = *buf++;
    data = *buf++;
    vram_adr(lo + hi*256);
    vram_put(data);
  }
}

// NES -> SMS palette conversion table
/*{pal:222,n:64}*/
const char NES_SMS_PALETTE[64] = {
0x15, 0x20, 0x20, 0x20, 0x11, 0x01, 0x01, 0x01, 0x00, 0x04, 0x04, 0x04, 0x10, 0x00, 0x00, 0x00, 0x3f, 0x34, 0x30, 0x35, 0x32, 0x13, 0x02, 0x06, 0x05, 0x08, 0x08, 0x08, 0x24, 0x00, 0x00, 0x00, 0x3f, 0x38, 0x36, 0x37, 0x37, 0x37, 0x17, 0x0b, 0x0f, 0x0d, 0x0c, 0x18, 0x3c, 0x15, 0x00, 0x00, 0x3f, 0x3e, 0x3a, 0x3b, 0x3b, 0x3f, 0x3f, 0x2f, 0x2f, 0x2f, 0x2e, 0x3e, 0x3e, 0x3f, 0x00, 0x00,
};

void pal_col(unsigned char index, unsigned char color) {
  byte smspal = NES_SMS_PALETTE[color & 63];
  cvu_memtocmemcpy(0xc000 + index, &smspal, 1);
}
void pal_bg(const char *data) {
  byte i;
  for (i=0; i<16; i++) {
    pal_col(i, data[i]);
  }
}
void pal_spr(const char *data) {
  byte i;
  for (i=0; i<16; i++) {
    pal_col(i+16, data[i]);
  }
}

void expand_nesbitmap(const char* src, int ntiles, byte p) {
  byte i;
  while (ntiles--) {
    for (i=0; i<8; i++) {
      byte b1 = src[0];
      byte b2 = src[8];
      cv_voutb(p&0x01?b1:p&0x10?b2:0);
      cv_voutb(p&0x02?b1:p&0x20?b2:0);
      cv_voutb(p&0x04?b1:p&0x40?b2:0);
      cv_voutb(p&0x08?b1:p&0x80?b2:0);
      src++;
    }
    src += 8;
  }
}

void vblank_handler(void) {
  flush_vram_update();
  PSGFrame();
  //PSGSFXFrame();
}

void setup_graphics() {
  cv_set_screen_mode(CV_SCREENMODE_4);
  cv_set_character_pattern_t(PATTERN);
  cv_set_image_table(IMAGE);
  cv_set_sprite_attribute_table(SPRITES);
  cv_set_vint_handler(vblank_handler);
  // convert NES bitmap to SMS format
  cv_set_write_vram_address(PATTERN);
  expand_nesbitmap(TILESET, 256, 0x21);
  // different palette for player character
  cv_set_write_vram_address(PATTERN + 0x51*32);
  expand_nesbitmap(TILESET+0x49*16, 8, 0x61);
}

//game uses 12:4 fixed point calculations for enemy movements

#define FP_BITS	4

//max size of the game map

#define MAP_WDT			16
#define MAP_WDT_BIT		4
#define MAP_HGT			13

//macro for calculating map offset from screen space, as
//the map size is smaller than screen to save some memory

#define MAP_ADR(x,y)	((((y)-2)<<MAP_WDT_BIT)|(x))

//size of a map tile

#define TILE_SIZE		16
#define TILE_SIZE_BIT	4

//movement directions, match to the gamepad buttons bits to
//simplify some things

#define DIR_NONE		0
#define DIR_LEFT		PAD_LEFT
#define DIR_RIGHT		PAD_RIGHT
#define DIR_UP			PAD_UP
#define DIR_DOWN		PAD_DOWN

//tile numbers for certain things in the game map
//i.e. which object corresponds to a character in the tileset

#define TILE_PLAYER		0x10
#define TILE_ENEMY1		0x11
#define TILE_ENEMY2		0x12
#define TILE_ENEMY3		0x13
#define TILE_WALL		0x40
#define TILE_EMPTY		0x44
#define TILE_ITEM		0x45

//number of levels in the game

#define LEVELS_ALL		5

//numbers for screens that are displayed by the same function as the
//level number

#define SCREEN_GAMEOVER	(LEVELS_ALL+0)
#define SCREEN_WELLDONE	(LEVELS_ALL+1)

//total number of moving characters on the screen

#define PLAYER_MAX	4

//sound effect numbers, it is easier to use meaningful defines than
//remember actual numbers of the effects

#define SFX_START		0
#define SFX_ITEM		1
#define SFX_RESPAWN1	2
#define SFX_RESPAWN2	3

//sub songs numbers in the music.ftm

#define MUSIC_LEVEL			0
#define MUSIC_GAME			1
#define MUSIC_CLEAR			2
#define MUSIC_GAME_OVER		3
#define MUSIC_WELL_DONE		4
#define MUSIC_LOSE			5

//palettes data
//all the palettes are designed in NES Screen Tool, then copy/pasted here
//with Palettes/Put C data to clipboard function

/*{pal:"nes",layout:"nes"}*/
const unsigned char palGame1[16]={ 0x0f,0x11,0x32,0x30,0x0f,0x1c,0x2c,0x3c,0x0f,0x09,0x27,0x38,0x0f,0x11,0x21,0x31 };

/*{pal:"nes",layout:"nes"}*/
const unsigned char palGame2[16]={ 0x0f,0x11,0x32,0x30,0x0f,0x11,0x21,0x31,0x0f,0x07,0x27,0x38,0x0f,0x13,0x23,0x33 };

/*{pal:"nes",layout:"nes"}*/
const unsigned char palGame3[16]={ 0x0f,0x11,0x32,0x30,0x0f,0x15,0x25,0x35,0x0f,0x05,0x27,0x38,0x0f,0x13,0x23,0x33 };

/*{pal:"nes",layout:"nes"}*/
const unsigned char palGame4[16]={ 0x0f,0x11,0x32,0x30,0x0f,0x19,0x29,0x39,0x0f,0x0b,0x27,0x38,0x0f,0x17,0x27,0x37 };

/*{pal:"nes",layout:"nes"}*/
const unsigned char palGame5[16]={ 0x0f,0x11,0x32,0x30,0x0f,0x16,0x26,0x36,0x0f,0x07,0x27,0x38,0x0f,0x18,0x28,0x38 };

/*{pal:"nes",layout:"nes"}*/
const unsigned char palGameSpr[16]={ 0x0f,0x0f,0x29,0x30,0x0f,0x0f,0x26,0x30,0x0f,0x0f,0x24,0x30,0x0f,0x0f,0x21,0x30 };

/*{pal:"nes",layout:"nes"}*/
const unsigned char palTitle[16]={ 0x0F,0x11,0x21,0x31,0x0F,0x1C,0x2C,0x3C,0x0F,0x12,0x22,0x32,0x0F,0x14,0x24,0x34 };


//metasprites

const unsigned char sprPlayer[]={
	0,-1,0x49,0,
	8,-1,0x4a,0,
	0, 7,0x4b,0,
	8, 7,0x4c,0,
	128
};

const unsigned char sprEnemy1[]={
	0,-1,0x4d+8,1,
	8,-1,0x4e+8,1,
	0, 7,0x4f+8,1,
	8, 7,0x50+8,1,
	128
};

const unsigned char sprEnemy2[]={
	0,-1,0x4d+8,2,
	8,-1,0x4e+8,2,
	0, 7,0x4f+8,2,
	8, 7,0x50+8,2,
	128
};

const unsigned char sprEnemy3[]={
	0,-1,0x4d+8,3,
	8,-1,0x4e+8,3,
	0, 7,0x4f+8,3,
	8, 7,0x50+8,3,
	128
};


//list of metasprites

const unsigned char* const sprListPlayer[]={ sprPlayer,sprEnemy1,sprEnemy2,sprEnemy3 };


//list of the levels, include pointer to the packed nametable of the level,
//and pointer to the associated palette

const unsigned char* const levelList[LEVELS_ALL*2]={
level1_nam,palGame1,
level2_nam,palGame2,
level3_nam,palGame3,
level4_nam,palGame3,
level5_nam,palGame3
};


//preinitialized update list used during the gameplay

const unsigned char updateListData[]={
0x28,0x00,TILE_EMPTY,	//these four entries are used to clear
0x28,0x00,TILE_EMPTY,	//the level tile after an item is collected
0x28,0x00,TILE_EMPTY,
0x28,0x00,TILE_EMPTY,
(IMAGE>>8),0x4f*2,0x10,	//these three entires are used to display
(IMAGE>>8),0x50*2,0x10,	//number of the collected items
(IMAGE>>8),0x51*2,0x10,
NT_UPD_EOF
};


//a nametable string with the game stats, created in NES Screen Tool
//and copy/pasted here with Shift+C

const unsigned char statsStr[27]={
	0x2c,0x25,0x36,0x25,0x2c,0x1a,0x00,0x00,0x27,
	0x25,0x2d,0x33,0x1a,0x00,0x00,0x00,0x0f,0x00,
	0x00,0x00,0x00,0x2c,0x29,0x36,0x25,0x33,0x1a
};


//list of screens such as level number, game over, and well done
//contains pointers to the packed nametables

const unsigned char* screenList[3]={ level_nam,gameover_nam,welldone_nam };

//list of sub songs for corresponding screens

const unsigned char screenMusicList[3]={MUSIC_LEVEL,MUSIC_GAME_OVER,MUSIC_WELL_DONE};

//large 1-5 numbers nametable definitons, 2x3 tiles each
//numbers were drawn one next to the other in NES Screen Tool,
//then that part of nametable was copy/pasted here with Shift+C
//here they are orderded as
//   top row of 11 22 33 44 55
//middle row of 11 22 33 44 55
//bottom row of 11 22 33 44 55

const unsigned char largeNums[10*3]={
	0x7a,0x7b,0x7c,0x7d,0x7c,0x7d,0x7e,0x7f,0x80,0x81,
	0x86,0x7b,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x7d,
	0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x7f,0x94,0x95
};


//array for game map, contains walls, empty spaces, and items

unsigned char map[MAP_WDT*MAP_HGT];

//global variables (TODO: convert to local?)

unsigned char ptr,spr;
unsigned char px,py;
unsigned char wait;
unsigned int i16;
int iy,dy;

//this array is used to determine movement directions for enemies

unsigned char dir[4];

//this array is used to convert nametable into game map, row by row

unsigned char nameRow[BYTES_PER_ROW];

//number of moving characters on current level

unsigned char player_all;

//character variables

unsigned int  player_x    [PLAYER_MAX];
unsigned int  player_y    [PLAYER_MAX];
unsigned char player_dir  [PLAYER_MAX];
int           player_cnt  [PLAYER_MAX];
unsigned int  player_speed[PLAYER_MAX];
unsigned char player_wait [PLAYER_MAX];

//number of items on current level, total and collected

unsigned char items_count;
unsigned char items_collected;

//game state variables

unsigned char game_level;
unsigned char game_lives;

//game state flags, they are 0 or 1

unsigned char game_done;
unsigned char game_paused;
unsigned char game_clear;

//system vars used everywhere as well

unsigned char frame_cnt;
unsigned char bright;

//update list

unsigned char update_list[7*3+1];

// current scroll offset
unsigned char scroll_y;


//smoothly fade current bright to the given value
//in case when to=0 it also stops the music,
//turns the display off, reset vram update and scroll

void pal_fade_to(unsigned to)
{
  if(!to) music_stop();

  while(bright!=to)
  {
    delay(4);
    if(bright<to) ++bright; else --bright;
    pal_bright(bright);
  }

  if(!bright)
  {
    ppu_off();
    set_vram_update(NULL);
    scroll(0,0);
  }
}



//show title screen

void title_screen(void)
{
  byte i;
  
  scroll(0,240);

  vram_adr(NAMETABLE_A);
  vram_unrle(title_nam);

  //vram_adr(NAMETABLE_C);//clear second nametable, as it is visible in the jumping effect
  //vram_fill(0,1024);

  pal_bg(palTitle);
  pal_bright(4);
  ppu_on_bg();
  delay(20);//delay just to make it look better
  
  iy=240<<FP_BITS;
  dy=-8<<FP_BITS;
  frame_cnt=0;
  wait=160;
  bright=4;

  while(1)
  {
    ppu_wait_frame();

    scroll(0,(iy>>FP_BITS)+16);

    if(pad_trigger(0)&PAD_START) break;

    iy+=dy;

    if(iy<0)
    {
      iy=0;
      dy=-dy>>1;
    }

    if(dy>(-8<<FP_BITS)) dy-=2;

    if(wait)
    {
      --wait;
    }
    else
    {
      pal_col(2,(frame_cnt&32)?0x0f:0x20);//blinking press start text
      ++frame_cnt;
    }
  }

  scroll(0,16);//if start is pressed, show the title at whole

  for(i=0;i<16;++i)//and blink the text faster
  {
    pal_col(2,i&1?0x0f:0x20);
    delay(4);
  }

  pal_fade_to(0);
}



//show level intro, game over, or well done screen

void show_screen(unsigned char num)
{
  byte i,j;

  PSGPlayNoRepeat(MUSIC_TITLE);
  
  scroll(0,0);

  if(num<LEVELS_ALL) spr=0; else spr=num-LEVELS_ALL+1;//get offset in the screens list

  vram_adr(NAMETABLE_A);
  vram_unrle(screenList[spr]);

  if(!spr)//if it is the level screen, print large number
  {
    j=num<<1;
    i16=NTADR(20,12);//position of the number in the nametable

    for(i=0;i<3;++i)
    {
      vram_adr(i16);
      vram_put(largeNums[j]);
      vram_put(largeNums[j+1]);
      j+=10;
      i16+=BYTES_PER_ROW;
    }
  }

  i16=(num==SCREEN_GAMEOVER)?0x1525:0x1121;//two colors for flashing text in LSB and MSB

  frame_cnt=0;

  pal_col(2,i16&0xff);//this palette entry is used for flashing text
  pal_col(3,0x30);
  pal_col(6,0x30);
  ppu_on_bg();

  pal_fade_to(4);
  //music_play(screenMusicList[spr]);

  if(!spr)//if it is the level screen, just wait one second
  {
    delay(100);
  }
  else//otherwise wait for Start button and display flashing text
  {
    while(1)
    {
      ppu_wait_frame();

      pal_col(2,frame_cnt&2?i16&0xff:i16>>8);

      if(pad_trigger(0)&PAD_START) break;

      ++frame_cnt;
    }
  }

  pal_fade_to(0);
}



//set up a move in the specified direction if there is no wall

void player_move(unsigned char id,unsigned char dir)
{
  px=player_x[id]>>(TILE_SIZE_BIT+FP_BITS);
  py=player_y[id]>>(TILE_SIZE_BIT+FP_BITS);

  switch(dir)
  {
    case DIR_LEFT:  --px; break;
    case DIR_RIGHT: ++px; break;
    case DIR_UP:    --py; break;
    case DIR_DOWN:  ++py; break;
  }

  if(map[MAP_ADR(px,py)]==TILE_WALL) return;

  player_cnt[id]=TILE_SIZE<<FP_BITS;
  player_dir[id]=dir;
}



//print a 1-3 digit decimal number into VRAM

void put_num(unsigned int adr,unsigned int num,unsigned char len)
{
  vram_adr(adr);

  if(len>2) vram_put(0x10+num/100);
  if(len>1) vram_put(0x10+num/10%10);
  vram_put(0x10+num%10);
}


void rewrite_level_vram() {
  byte i,j;
  
  //this loop reads the level nametable back from VRAM, row by row,
  //constructs game map, removes spawn points from the nametable,
  //and writes back to the VRAM

  i16=NTADR(0,4);
  ptr=0;
  wait=0;

  for(i=2;i<MAP_HGT+2;++i)
  {
    cvu_vmemtomemcpy(nameRow, i16, sizeof(nameRow));
    vram_adr(i16);

    for(j=0;j<MAP_WDT<<1;j+=2)
    {
      spr=nameRow[j*2];

      switch(spr)
      {
        case TILE_PLAYER://player
        case TILE_ENEMY1://enemies
        case TILE_ENEMY2:
        case TILE_ENEMY3:
          player_dir  [player_all]=DIR_NONE;
          player_x    [player_all]=(j<<3)<<FP_BITS;
          player_y    [player_all]=(i<<4)<<FP_BITS;
          player_cnt  [player_all]=0;
          player_wait [player_all]=16+((spr-TILE_PLAYER)<<4);
          player_speed[player_all]=(spr==TILE_PLAYER)?2<<FP_BITS:10+((spr-TILE_ENEMY1)<<1);
          ++player_all;
          wait+=16;
          spr=TILE_EMPTY;
          break;

        case TILE_ITEM:
          ++items_count;
          break;
      }

      map[ptr++]=spr;

      vram_put(spr);
      vram_put(nameRow[j*2+2]);
    }

    i16+=BYTES_PER_ROW*2;
  }

}

void build_oam() {
  byte i;

  spr=(player_all-1)<<4;

  for(i=0;i<player_all;++i)
  {
    py=(player_y[i]>>FP_BITS) - scroll_y;

    if(player_wait[i])
    {
      if(player_wait[i]>=16||player_wait[i]&2) py=240;
    }

    oam_meta_spr(player_x[i]>>FP_BITS,py,spr,sprListPlayer[i]);
    spr-=16;
  }

}

void move_player(char i) {
  if(player_cnt[i])
  {
    switch(player_dir[i])
    {
      case DIR_RIGHT: player_x[i]+=player_speed[i]; break;
      case DIR_LEFT:  player_x[i]-=player_speed[i]; break;
      case DIR_DOWN:  player_y[i]+=player_speed[i]; break;
      case DIR_UP:    player_y[i]-=player_speed[i]; break;
    }

    player_cnt[i]-=player_speed[i];

    //if move from one tile to another is over, realign the object to tile grid
    //it is needed because when it moves with non-integer speed, it could
    //overrun the destination tile a little bit, and thus can't take a turn properly

    if(player_cnt[i]<=0)
    {
      if(player_cnt[i]<0) //overrun
      {
        player_cnt[i]=0;

        //0xff is a coordinate mask that leaves only integer tile offeset
        //it is 8:4:4 here, where 8 is integer tile coordinate,
        //first 4 is offset in the tile, which is 16 pixels wide,
        //and second 4 is fixed point resolution

        player_x[i]=(player_x[i]&0xff00)+(player_dir[i]==DIR_LEFT?0x100:0);
        player_y[i]=(player_y[i]&0xff00)+(player_dir[i]==DIR_UP  ?0x100:0);
      }

      //it is is the player object, check if there is an item in the new tile
      if(!i)
      {
        i16=MAP_ADR((player_x[i]>>(TILE_SIZE_BIT+FP_BITS)),
                    (player_y[i]>>(TILE_SIZE_BIT+FP_BITS)));

        if(map[i16]==TILE_ITEM)
        {
          map[i16]=TILE_EMPTY; //mark as collected in the game map

          sfx_play(SFX_ITEM,2);
          ++items_collected;

          //get address of the tile in the nametable

          i16 = NTADR(
            ((player_x[i]>>(TILE_SIZE_BIT+FP_BITS))<<1),
            ((player_y[i]>>(TILE_SIZE_BIT+FP_BITS))<<1)
           );

          //replace it with empty tile through the update list

          update_list[0]=i16>>8;
          update_list[1]=i16&255;
          update_list[3]=update_list[0];
          update_list[4]=update_list[1]+2;
          i16+=BYTES_PER_ROW;
          update_list[6]=i16>>8;
          update_list[7]=i16&255;
          update_list[9]=update_list[6];
          update_list[10]=update_list[7]+2;

          //update number of collected items in the game stats

          update_list[14]=0x10+items_collected/100;
          update_list[17]=0x10+items_collected/10%10;
          update_list[20]=0x10+items_collected%10;
        }
      }
    }
  }

}

bool enemy_collides_with_player(byte i) {
  return !((player_x[i]+(4 <<FP_BITS))>=(player_x[0]+(12<<FP_BITS))||
           (player_x[i]+(12<<FP_BITS))< (player_x[0]+(4 <<FP_BITS))||
           (player_y[i]+(4 <<FP_BITS))>=(player_y[0]+(12<<FP_BITS))||
           (player_y[i]+(12<<FP_BITS))< (player_y[0]+(4 <<FP_BITS)));
}

void process_player_controls(byte i) {
  byte j;
  //get gamepad state, it was previously polled with pad_trigger

  j=pad_state(0);

  //this is a tricky part to make controls more predictable
  //when you press two directions at once, sliding by a wall
  //to take turn into a passage on the side
  //this piece of code gives current direction lower priority
  //through testing it first
  //bits in player_dir var are matching to the buttons bits

  if(j&player_dir[0])
  {
    j&=~player_dir[0]; //remove the direction from further check
    player_move(i,player_dir[0]); //change the direction
  }

  //now continue control processing as usual

  if(j&PAD_LEFT)  player_move(i,DIR_LEFT);
  if(j&PAD_RIGHT) player_move(i,DIR_RIGHT);
  if(j&PAD_UP)    player_move(i,DIR_UP);
  if(j&PAD_DOWN)  player_move(i,DIR_DOWN);
}

void process_enemy(byte i) {
  byte j;
  
  //the AI is very simple
  //first we create list of all directions that are possible to take
  //excluding the direction that is opposite to previous one

  i16=MAP_ADR((player_x[i]>>8),(player_y[i]>>8));
  ptr=player_dir[i];
  j=0;

  if(ptr!=DIR_RIGHT&&map[i16-1]!=TILE_WALL) dir[j++]=DIR_LEFT;
  if(ptr!=DIR_LEFT &&map[i16+1]!=TILE_WALL) dir[j++]=DIR_RIGHT;
  if(ptr!=DIR_DOWN &&map[i16-MAP_WDT]!=TILE_WALL) dir[j++]=DIR_UP;
  if(ptr!=DIR_UP   &&map[i16+MAP_WDT]!=TILE_WALL) dir[j++]=DIR_DOWN;

  //randomly select a possible direction

  player_move(i,dir[rand8()%j]);

  //if there was more than one possible direction,
  //i.e. it is a branch and not a corridor,
  //attempt to move towards the player

  if(j>1)
  {
    if(ptr!=DIR_DOWN &&player_y[0]<player_y[i]) player_move(i,DIR_UP);
    if(ptr!=DIR_UP   &&player_y[0]>player_y[i]) player_move(i,DIR_DOWN);
    if(ptr!=DIR_RIGHT&&player_x[0]<player_x[i]) player_move(i,DIR_LEFT);
    if(ptr!=DIR_LEFT &&player_x[0]>player_x[i]) player_move(i,DIR_RIGHT);
  }
}

//the main gameplay code

void setup_level(void) {
  byte i;
  
  i = game_level<<1;

  oam_clear();

  vram_adr(NAMETABLE_A);
  vram_unrle(levelList[i]);					//unpack level nametable

  vram_adr(NTADR(2,2));
  vram_write((unsigned char*)statsStr,27); 	//add game stats string

  pal_bg(levelList[i+1]); 						//set up background palette
  pal_spr(palGameSpr); 							//set up sprites palette

  player_all=0;
  items_count=0;
  items_collected=0;

  rewrite_level_vram();

  //setup update list

  memcpy(update_list,updateListData,sizeof(updateListData));

  set_vram_update(update_list);

  //put constant game stats numbers, that aren't updated during level

  put_num(NTADR(8,2),game_level+1,1);
  put_num(NTADR(19,2),items_count,3);
  put_num(NTADR(29,2),game_lives-1,1);

  //enable display

  ppu_on_all();

  game_done=FALSE;
  game_paused=FALSE;
  game_clear=FALSE;

  bright=0;
  frame_cnt=0;

}

void scroll_with_player() {
  sbyte y;
  // see if the level is large vertically
  if (map[0xa8] == TILE_WALL || map[0xb8] == TILE_WALL) {
    y = (player_y[0] >> 5) - 32;
    if (y < 0) y = 0;
    if (y > 0x28) y = 0x28;
  } else {
    y = 0;
  }
  // scroll Y direction if needed
  cv_set_vscroll(y);
  scroll_y = y;
}

void game_loop(void)
{
  unsigned char i;
  
  setup_level();
  
  scroll_with_player(); // reset scrolling
  
  while(!game_done)
  {
    //construct OAM from object parameters
    build_oam();

    //wait for next frame
    //it is here and not at beginning of the loop because you need
    //to update OAM for the very first frame, and you also need to do that
    //right after object parameters were changed, so either OAM update should
    //be in a function that called before the loop and at the end of the loop,
    //or wait for NMI should be placed there
    //otherwise you would have situation update-wait-display, i.e.
    //one frame delay between action and display of its result

    ppu_wait_frame();

    ++frame_cnt;

    //slowly fade virtual brightness to needed value,
    //which is max for gameplay or half for pause

    if(!(frame_cnt&3))
    {
      if(!game_paused&&bright<4) ++bright;
      if( game_paused&&bright>2) --bright;

      pal_bright(bright);
    }

    //poll the gamepad in the trigger mode

    i=pad_trigger(0);

    //it start was released and then pressed, toggle pause mode

    if(i&PAD_START)
    {
      game_paused^=TRUE;
      music_pause(game_paused);
    }

    //don't process anything in pause mode, just display latest game state

    if(game_paused) continue;

    //CHR bank switching animation with different speed for background and sprites

    bank_bg((frame_cnt>>4)&1);
    bank_spr((frame_cnt>>3)&1);

    //a counter that does not allow objects to move while spawn animation plays

    if(wait)
    {
      --wait;

      if(!wait) music_play(MUSIC_GAME);//start the music when all the objects spawned
    }

    //check for level completion condition

    if(items_collected==items_count)
    {
      music_play(MUSIC_CLEAR);
      game_done=TRUE;
      game_clear=TRUE;
    }

    //process all the objects
    //player and enemies are the same type of object in this game,
    //to make code simpler and shorter, but generally they need to be
    //different kind of objects

    for(i=0;i<player_all;++i)
    {
      //per-object spawn animation counter, it counts fron N to 16 to 0
      //needed because objects spawn in sequence, not all at once

      if(player_wait[i])
      {
        if(player_wait[i]==16) sfx_play(i?SFX_RESPAWN2:SFX_RESPAWN1,i);
        --player_wait[i];
        continue;
      }

      if(wait) continue; //don't process object movements if spawn animation is running

      //check collision of an enemy object with player object
      //NOT logic is used here, check http://gendev.spritesmind.net/page-collide.html

      if(i)
      {
        if(enemy_collides_with_player(i))
        {
          //if an enemy touch the player, quit the game loop

          if(!game_clear)
          {
            music_play(MUSIC_LOSE);
            game_done=TRUE;
            break;
          }
        }
      } else {
        scroll_with_player();
      }

      move_player(i);

      //if movement counter is not zero, process the movement

      if(!player_cnt[i]) //movement to the next tile is done, set up new movement
      {
        if(!i) //this is the player, process controls
        {
          process_player_controls(i);
        }
        else //this is an enemy, run AI
        {
          process_enemy(i);
        }
      }
    }
  }

  delay(100);
  pal_fade_to(0);
}


//this is where the program starts

void main(void)
{
  setup_graphics();

  while(1)//infinite loop, title-gameplay
  {
    title_screen();

    game_level=0;
    game_lives=4;

    while(game_lives&&game_level<LEVELS_ALL)//loop for gameplay
    {
      show_screen(game_level);

      game_loop();

      if(game_clear) ++game_level; else --game_lives;
    }

    show_screen(!game_lives?SCREEN_GAMEOVER:SCREEN_WELLDONE);//show game results
  }
}
