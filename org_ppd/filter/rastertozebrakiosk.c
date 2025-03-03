/*
 * "$Id: rastertozebrakiosk.c,v 1.4 2009/07/14 13:27:07 pgrenning Exp $"
 *
 *   Label printer filter for the Common UNIX Printing System (CUPS).
 *
 *   Copyright 2007-2008 by Apple Inc.
 *   Copyright 2001-2007 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 *   which should have been included with this file.  If this file is
 *   file is missing or damaged, see the license at "http://www.cups.org/".
 *
 *   This file is subject to the Apple OS-Developed Software exception.
 *
 * Contents:
 *
 *   set_bm_params();   - set black mark parameters
 *   set_sys_params();  - set system parameters
 *   set_partial_cut(); - set partial cut
 *   cancel_job();	- cancel job
 *   clean_up();		- clean up
 *   output_command()    - sends command to the printer
 *   output_ascii_encoded_length() - outputs ascii encoded length
 *   output_null_terminator() - sends null terminator to the printer
 *   get_option_choice_index() - gets settings from the UI
 *   do_reverse()	- reverse
 *   do_advance()  	- advance
 *   do_eject()  	- eject receipt
 *   set_loop_length() 	- sets presenter loop length
 *   get_pagewidth_pageheight() - get page width and page height
 *   initialize_settings() - initialize printer settings
 *   job_setup()  	- job setup
 *   page_setup() 	- page setup
 *   main()             - Main entry and processing of driver.
 */

/*
 * Include necessary headers...
 */

#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/raster.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>

#define FALSE 0
#define TRUE  (!FALSE)

/*
 * This driver filter currently supports Zebra TTP 20x0,TTP 21x0, TTP 7030, TTP 8200 and Zebra KR203
 */

/* definitions for printable width - not used right now, though.*/
#define mm58		432		/* 54mm printable, same as 60mm on 20x0 */
#define mm80		576		/* 72mm printable, same as 82.5mm on 20x0 */
#define mm112		832		/* 104mm printable */
#define mm216		1728	/* 203dpi */
#define mm216_300	2560	/* 300dpi */
#define FOCUS_LEFT      0
#define FOCUS_CENTER    1
#define FOCUS_RIGHT     2

#define GET_LIB_FN_OR_EXIT_FAILURE(fn_ptr,lib,fn_name)                                      \
{                                                                                           \
    fn_ptr = dlsym(lib, fn_name);                                                           \
    if ((dlerror()) != NULL)                                                                \
    {                                                                                       \
        fputs("ERROR: required fn not exported from dynamically loaded libary\n", stderr);  \
        if (libCupsImage != 0) dlclose(libCupsImage);                                       \
        if (libCups      != 0) dlclose(libCups);                                            \
        return EXIT_FAILURE;                                                                \
    }                                                                                       \
}

#define CLEANUP                                                         \
{                                                                       \
	if (original_raster_data_ptr   != NULL) free(original_raster_data_ptr);   \
	cupsRasterClose(ras);                                               \
	if (fd != 0)                                                        \
	{                                                                   \
		close(fd);                                                      \
	}									\
}

/*****************************************
 *	Structures used throughout       *
 *****************************************/

struct cups_settings_s /* This structure is for Zebra Kiosk printers. */
{
  int model_number; /* Supports: 8200, 7030, 2000, 2100, 203 */
  /* This allows for using the same driver with multiple models */
  int bidirectional; /* not used right now will be used for custom backend */
  float page_width; /* current page width in pixels */
  float page_height; /* current page height in pixels */
  int page_mode; /* 0 = roll mode, 1 = page mode, 2 = black mark mode */
  int black_mark_min; /* black mark minimum length (param 40) */
  int black_mark_max; /* black mark maximum (actual) length (param 39) */
  int black_mark_cut_pos; /* black mark cut offset position (params 41/42) */
  int reverse; /* AKA Top margin control */
  int eject; /* Eject length */
  int print_speed; /* max allowable: 19 (limited by ppd per model) */
  int burn_time; /* max allowable: 15 */
  int retract_behavior; /* wastebasket control (n45) */
  int focus_area; /* for zoom/oversized print control */
  /* not used right now */
  int page_cut_type; /* per doc (0), per page (1), black mark (2) */
  int clear_presenter; /* 0 = no, 1=yes */
  int partial_cut; /* partial cut supported on 2000/2100 only */
  /* 0 = off */
  int loop_length; /* presenter loop length (n9) */
  int vert_mode; /* System parameter 57 on 20x0 only. Bit2, value 4 */
  int pull_detect; /* System parameter 57 on 20x0 only. Bit1, value 2 */
  int clr_pres; /* System parameter 57 on 20x0 only. Bit0, value 1. */
  /* Don't know what it does yet, so hardcoding it enabled. */
  int resolution; /* always 203 except for 8300, which is not supported */
  int resolution_x; /* non-square pixels on the 8300 */
  int resolution_y; /* non-square pixels on the 8300 */
  int bytes_per_scanline; /* see definitions in printer manuals */
  int bytes_per_scanline_std; /* these two are the same */
  int last_page; /* if we're on the last page of a job, don't do a partial cut */
};

struct cups_command_s /* This structure is for label commands */
{
  int length; /* length of command*/
  char* command; /* command */
};

/*
 * Static command structs are used where
 * No additional processing is needed.
 * If additional processing is needed,
 * like to calculate lengths or validate data,
 * a separate inline void is used.
 */
static const struct cups_command_s endPageCommand =
  { 2, (char[2]
)        { 0x1b,0x1e}};

static const struct cups_command_s endJobCommand =
  { 2, (char[2]
)        { 0x1b,0x1e}};

static const struct cups_command_s print_speedCommand[20] =
  {
    { 5, (char[5]
)          { 0x1b,'&','P',0x08,0x01}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x02}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x03}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x04}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x05}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x06}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x07}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x08}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x09}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x0A}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x0B}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x0C}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x0D}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x0E}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x0F}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x10}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x11}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x12}},
        { 5, (char[5])
          { 0x1b,'&','P',0x08,0x13}},
      };

static const struct cups_command_s kr_print_speedCommand[16] =
  {
    { 5, (char[5]
)          { 0x1b,'&','p',0x08,0x48}},
        { 5, (char[5])
          { 0x1b,'&','p',0x08,0x50}},
        { 5, (char[5])
          { 0x1b,'&','p',0x08,0x55}},
        { 5, (char[5])
          { 0x1b,'&','p',0x08,0x5A}},
        { 5, (char[5])
          { 0x1b,'&','p',0x08,0x5F}},
        { 5, (char[5])
          { 0x1b,'&','p',0x08,0x64}},
        { 5, (char[5])
          { 0x1b,'&','p',0x08,0x69}},
        { 5, (char[5])
          { 0x1b,'&','p',0x08,0x6E}},
        { 5, (char[5])
          { 0x1b,'&','p',0x08,0x73}},
        { 5, (char[5])
          { 0x1b,'&','p',0x08,0x78}},
        { 5, (char[5])
          { 0x1b,'&','p',0x08,0x7D}},
        { 5, (char[5])
          { 0x1b,'&','p',0x08,0x82}},
        { 5, (char[5])
          { 0x1b,'&','p',0x08,0x87}},
        { 5, (char[5])
          { 0x1b,'&','p',0x08,0x8C}},
        { 5, (char[5])
          { 0x1b,'&','p',0x08,0x91}},
        { 5, (char[5])
          { 0x1b,'&','p',0x08,0x98}},
      };

static const struct cups_command_s burn_timeCommand[17] =
  {
    { 5, (char[5]
)          { 0x1b,'&','P',0x07,0x00}},
        { 5, (char[5])
          { 0x1b,'&','P',0x07,0x01}},
        { 5, (char[5])
          { 0x1b,'&','P',0x07,0x02}},
        { 5, (char[5])
          { 0x1b,'&','P',0x07,0x03}},
        { 5, (char[5])
          { 0x1b,'&','P',0x07,0x04}},
        { 5, (char[5])
          { 0x1b,'&','P',0x07,0x05}},
        { 5, (char[5])
          { 0x1b,'&','P',0x07,0x06}},
        { 5, (char[5])
          { 0x1b,'&','P',0x07,0x07}},
        { 5, (char[5])
          { 0x1b,'&','P',0x07,0x08}},
        { 5, (char[5])
          { 0x1b,'&','P',0x07,0x09}},
        { 5, (char[5])
          { 0x1b,'&','P',0x07,0x0A}},
        { 5, (char[5])
          { 0x1b,'&','P',0x07,0x0B}},
        { 5, (char[5])
          { 0x1b,'&','P',0x07,0x0C}},
        { 5, (char[5])
          { 0x1b,'&','P',0x07,0x0D}},
        { 5, (char[5])
          { 0x1b,'&','P',0x07,0x0E}},
        { 5, (char[5])
          { 0x1b,'&','P',0x07,0x0F}},
        { 5, (char[5])
          { 0x1b,'&','P',0x07,0x10}}
      };

double kr_speedCommand[16] =
  {
    75.,
    80.,
    85. ,
    90. ,
    95. ,
    100.,
    105.,
    110.,
    115.,
    120.,
    125.,
    130.,
    135.,
    140.,
    145.,
    152.
 };

/*
 * Page Mode
 * page_modeCommand[0] = Variable 	(n36=1)
 * page_modeCommand[1] = Fixed		(n36=0)
 * page_modeCommand[2] = Black Mark	(n36=2)
 */
static const struct cups_command_s page_modeCommand[3] =
  {
    { 5, (char[5]
)          { 0x1b,'&','P',0x24,0x01}},
        { 5, (char[5])
          { 0x1b,'&','P',0x24,0x00}},
        { 5, (char[5])
          { 0x1b,'&','P',0x24,0x02}}
      };

static const struct cups_command_s kr_page_modeCommand[3] =
  {
    { 5, (char[5])
      { 0x1b,'&','p',0x23,0x00}},
        { 5, (char[5])
          { 0x1b,'&','p',0x23,0x00}},
        { 5, (char[5])
          { 0x1b,'&','p',0x23,0x01}}
      };

static const struct cups_command_s clear_presenterCommand =
  { 1, (char[1]
)        { 0x05}};

static const struct cups_command_s wasteBasketCommand[12] =
  {
    { 5, (char[5]
)          { 0x1b,'&','P',0x2d,0x00}},
        { 5, (char[5])
          { 0x1b,'&','P',0x2d,0x01}},
        { 5, (char[5])
          { 0x1b,'&','P',0x2d,0x02}},
        { 5, (char[5])
          { 0x1b,'&','P',0x2d,0x03}},
        { 5, (char[5])
          { 0x1b,'&','P',0x2d,0x06}},
        { 5, (char[5])
          { 0x1b,'&','P',0x2d,0x0C}},
        { 5, (char[5])
          { 0x1b,'&','P',0x2d,0x64}},
        { 5, (char[5])
          { 0x1b,'&','P',0x2d,0x65}},
        { 5, (char[5])
          { 0x1b,'&','P',0x2d,0x66}},
        { 5, (char[5])
          { 0x1b,'&','P',0x2d,0x67}},
        { 5, (char[5])
          { 0x1b,'&','P',0x2d,0x6A}},
        { 5, (char[5])
          { 0x1b,'&','P',0x2d,0x70}}
      };

static const struct cups_command_s ejectTimeoutCommand[31] =
  {
    { 6, (char[6]
)          { 0x1b,'&','p',0x2d,0x00, 0}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,10}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,20}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,30}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,40}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,50}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,60}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,70}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,80}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,90}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,100}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,110}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,120}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,130}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,140}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,150}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,160}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,170}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,180}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,190}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,200}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,210}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,220}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,230}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0,240}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,0, 250}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,1, 5}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,1, 15}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,1, 25}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,1, 35}},
        { 6, (char[6])
          { 0x1b,'&','p',0x2d,1, 45}},
      };

static const struct cups_command_s loopLength[13] =
  {
    { 6, (char[6])
          { 0x1b,'&','p',0x09,0,0x00}},
        { 6, (char[6])
          { 0x1b,'&','p',0x09,0,0x50}},
        { 6, (char[6])
          { 0x1b,'&','p',0x09,0,0x64}},
        { 6, (char[6])
          { 0x1b,'&','p',0x09,0,0x96}},
        { 6, (char[6])
          { 0x1b,'&','p',0x09,0,0xC8}},
        { 6, (char[6])
          { 0x1b,'&','p',0x09,0,0xFA}},
        { 6, (char[6])
          { 0x1b,'&','p',0x09,1,0x2C}},
        { 6, (char[6])
          { 0x1b,'&','p',0x09,1,0x5E}},
        { 6, (char[6])
          { 0x1b,'&','p',0x09,1,0x90}},
        { 6, (char[6])
          { 0x1b,'&','p',0x09,1,0xC2}},
        { 6, (char[6])
          { 0x1b,'&','p',0x09,1,0xF4}},
        { 6, (char[6])
          { 0x1b,'&','p',0x09,2,0x26}},
        { 6, (char[6])
          { 0x1b,'&','p',0x09,2,0x58}}
      };


/*
 * Globals
 */

struct cups_settings_s settings;

/*
 * Some prototypes where needed
 */

inline void
set_bm_params(struct cups_settings_s *); 
inline void
set_sys_params(struct cups_settings_s *);
inline void
set_partial_cut(struct cups_settings_s *);
void
cancel_job(int);
void
clean_up();

/*
 * Definitions for inline functions
 */

inline void
output_command(struct cups_command_s output)
{
  int i = 0;
  for (; i < output.length; i++)
  {
    putchar(output.command[i]);
  }
}

/*
 * outputs ascii encoded length
 */

inline void
output_ascii_encoded_length(int length)
{
  printf("%d", length);
}

/*
 * sends null terminating character
 */

inline void
output_null_terminator()
{
  putchar(0x00);
}

/*
 * Obtains options values
 */

inline int
get_option_choice_index(const char * choiceName, ppd_file_t * ppd)
{
  ppd_choice_t * choice;
  ppd_option_t * option;
  choice = ppdFindMarkedChoice(ppd, choiceName);
  if (choice == NULL)
  {
    if ((option = ppdFindOption(ppd, choiceName)) == NULL)
      return -1;
    if ((choice = ppdFindChoice(option, option->defchoice)) == NULL)
      return -1;
  }
  return atoi(choice->choice);
}

/*
 * reverse
 */

inline void
do_reverse(struct cups_settings_s * settings)
{
  if (settings->reverse != 0) /* No sense filling the print data with ESC j 0! */
  {
    int reverse = settings->reverse * 8;
    /* fprintf(stderr, "***Reverse = %d\n", reverse); */
    printf("\x1bj%c", reverse);
  }
}

/*
 * advanced settings
 */

inline void
do_advance(struct cups_settings_s * settings)
{
  int advance = 19* 8 ; /* default advance of 152 */
  int J1 = 0; /* Advance Highbyte */
  int J2 = 0; /* Advance Lowbyte */
  int i; /* count var */
  switch (settings->model_number)
  {
    case 8200:
        advance = 19* 8 ;
	break;
    case 2000:
    case 2100:
    case 203:
        advance = 12*8;
        break;
    case 7030: advance = 17*8;
        break;
  }

    J1 = advance / 256;
    J2 = advance % 256;
    for (i=J1; i>0; i--) /* highbyte = number of full esc j's we need */

    {
      printf("\x1bJ%c", 255);
    }
    printf("\033J%c", J2); /* lowbyte remainder = single esc j < 255 */
  }

/*
 * Ejects Receipt
 */

inline void
do_eject(struct cups_settings_s * settings)
{
  int eject = settings->eject;
  if (eject < 0 && settings->model_number != 2100)
  {
    /* 
     * we don't support negative eject on printers other than 2100
     * so we will use the default 30mm eject
     * however this should not occur, since no other ppd has negative choices
     */
    printf("\033\014\036");
  }
  else
  {
    /* use the eject specified */
    printf("\033\014%c", eject);
  }
}

/*
 * Sets black mark parameters
 */

inline void
set_bm_params(struct cups_settings_s * settings)
{
  int black_mark_min = settings->black_mark_min * 8; /* sets the black mark minimum */
  int black_mark_max = settings->black_mark_max * 8; /* sets the black mark maxium */
  int black_mark_cut_pos = settings->black_mark_cut_pos * 8; /* sets the black cut position */
  int q1CutPos = black_mark_cut_pos / 256; /* sets the black cut position */
  int q2CutPos = black_mark_cut_pos % 256; /* sets the black cut position */
  /*
   * set the appropriate parameters
   * n39 = BM length (black_mark_max)
   * n40 = Min BM length (black_mark_min)
   * n41 + 42 = BM cut offset (black_mark_cut_pos, high/low byte)
   */
  printf("\033&P\050%c\033&P\047%c\033&P\051%c\033&P\052%c", black_mark_min,
      black_mark_max, q1CutPos, q2CutPos);

}

/*
 * Sets system parameters
 */

inline void
set_sys_params(struct cups_settings_s * settings)
{
  /* 
   * On the TTP2000, Parameter 57 is called System.
   * It is a bitfield that controls pull detection,
   * Vertical/Horizontal presentation, and
   * Presenter clearing behavior. Values have been
   * assigned to each option in the ppd, so simply
   * adding these together will create a valid setting.
   */ 
  if (settings->model_number == 2000)
  {
    int param57 = 248; /* Base value. This field is made of the 3 LSBs. */
    param57 += settings->pull_detect + settings->vert_mode + settings->clr_pres;
    printf("\033&P\071%c", param57);
  }
}

/*
 * Sets partial cut
 */

inline void
set_partial_cut(struct cups_settings_s * settings)
{
  /* Partial cut is supported only on the 2000 and 2100 printers. */

  if (settings->model_number == 2000 || settings->model_number == 2100)
  {
    printf("\033&P\074%c", settings->partial_cut);
  }
  else
  {
    /* do nothing */
  }
}

/*
 * Sets presenter loop length
 */

inline void
set_loop_length(struct cups_settings_s * settings)
{
  /* parameter n9 */
  if (settings->model_number == 203) {
    output_command(loopLength[settings->loop_length]);
  } else {
    printf("\033&P\011%c", settings->loop_length);
  }
}

//////////////////////////////////
//	End Inline Functions	//
//////////////////////////////////

/*
 * Gets the page width and page height from UI
 */

void
get_pagewidth_pageheight(ppd_file_t * ppd, struct cups_settings_s * settings)
{

  ppd_choice_t * choice; /* choice from UI */
  ppd_option_t * option; /* option from UI */

  char width[20]; 	/* width */
  int width_index;		/* width index */

  char height[20];	/* height */
  int height_index;	/* height index */

  char * page_size;	/* page size */
  int idx;		/* page size index */

  int state;		/* current state */

  choice = ppdFindMarkedChoice(ppd, "PageSize");
  if (choice == NULL)
  {
    option = ppdFindOption(ppd, "PageSize");
    choice = ppdFindChoice(option, option->defchoice);
  }

  width_index = 0;
  memset(width, 0x00, sizeof(width));
  height_index = 0;
  memset(height, 0x00, sizeof(height));

  page_size = choice->choice;
  idx = 0;

  state = 0; /* 0 = init, 1 = width, 2 = height, 3 = complete, 4 = fail */

  if (page_size[idx] == 0x00)
  {
  }

  while (page_size[idx] != 0x00)
  {
    if (state == 0)
    {
      if (page_size[idx] == 'X')
      {
        state = 1;
        idx++;
        continue;
      }
    }
    else if (state == 1)
    {
      if ((page_size[idx] >= '0') && (page_size[idx] <= '9'))
      {
        width[width_index++] = page_size[idx];

        idx++;
        continue;
      }
      else if (page_size[idx] == 'D')
      {
        width[width_index++] = '.';

        idx++;
        continue;
      }
      else if (page_size[idx] == 'M')
      {
        idx++;
        continue;
      }
      else if (page_size[idx] == 'Y')
      {
        state = 2;
        idx++;
        continue;
      }
    }
    else if (state == 2)
    {
      if ((page_size[idx] >= '0') && (page_size[idx] <= '9'))
      {
        height[height_index++] = page_size[idx];

        idx++;
        continue;
      }
      else if (page_size[idx] == 'D')
      {
        height[height_index++] = '.';

        idx++;
        continue;
      }
      else if (page_size[idx] == 'M')
      {
        state = 3;
        break;
      }
    }

    state = 4;
    break;
  }

  if (state == 3)
  {
    settings->page_width = atof(width);
    settings->page_height = atof(height);
  }
  else
  {
    settings->page_width = 0;
    settings->page_height = 0;
  }

  fprintf(stderr, "***Page width = %f ***\n", settings->page_width);
  fprintf(stderr, "***Page height = %f ***\n", settings->page_height);

}

/*
 * initialize_settings reads the ppd and
 * extracts the config options and assigns
 * them to the variables used in the
 * rastertozebrakiosk filter
 */

void
initialize_settings(char * commandLineOptionSettings,
    struct cups_settings_s * settings)
{
  ppd_file_t * ppd = NULL; 		/* ppd file */
  cups_option_t * options = NULL;	/* printer options */
  int num_options = 0;			/* number of options */
  int a_model_number = 0;			/* printer model number */

  char * buffer;			/* buffer */
  buffer = getenv("PPD");
  fprintf(stderr, "ppd = %s", buffer);

  ppd = ppdOpenFile(getenv("PPD"));
  ppdMarkDefaults(ppd);

  num_options = cupsParseOptions(commandLineOptionSettings, 0, &options);
  if ((num_options != 0) && (options != NULL))
  {
    cupsMarkOptions(ppd, num_options, options);
    cupsFreeOptions(num_options, options);
  }
  memset(settings, 0x00, sizeof(struct cups_settings_s));

  a_model_number = settings->model_number = ppd->model_number;

  settings->page_mode = get_option_choice_index("pageMode", ppd);
  settings->bidirectional = get_option_choice_index("BidiPrinting", ppd);
  settings->page_cut_type = get_option_choice_index("PageCutType", ppd);
  settings->resolution = get_option_choice_index("Resolution", ppd);
  if (a_model_number == 203) {
      settings->print_speed = get_option_choice_index("PrintSpeed", ppd);
      settings->burn_time = get_option_choice_index("Darkness", ppd);
      settings->retract_behavior = get_option_choice_index("EjectOptions", ppd);
      settings->black_mark_min = 0; 
      settings->black_mark_max = 0;
      settings->black_mark_cut_pos = 0;
      settings->vert_mode = 0;
      settings->pull_detect = 0;
      
  } else {
      settings->print_speed = get_option_choice_index("PrintSpeed", ppd);
      settings->burn_time = get_option_choice_index("BurnTime", ppd);
      settings->black_mark_min = get_option_choice_index("BMMin", ppd); 
      settings->black_mark_max = get_option_choice_index("BMMax", ppd);
      settings->black_mark_cut_pos = get_option_choice_index("BMCutPos", ppd);
      settings->retract_behavior = get_option_choice_index("RetractOptions", ppd);
      settings->vert_mode = get_option_choice_index("VertMode", ppd);
      settings->pull_detect = get_option_choice_index("PullDetect", ppd);
  }
  settings->clear_presenter = get_option_choice_index("ClearPresenter", ppd);
  settings->reverse = get_option_choice_index("Reverse", ppd);
  settings->eject = get_option_choice_index("Eject", ppd);

  settings->clr_pres = 1;

  settings->loop_length = get_option_choice_index("LoopLength", ppd);

  settings->partial_cut = get_option_choice_index("PartialCut", ppd);

  switch (a_model_number) /* Model specific settings */
  {
    case 8200:
        settings->bytes_per_scanline = 216;
        settings->bytes_per_scanline_std = 216;
        break;
    case 2000:
    case 2100:
    case 203:
        settings->bytes_per_scanline = 80;
        settings->bytes_per_scanline_std = 80;
        break;
    case 7030: 	/* 7030, max width 112mm. */
        	/* Max width is 104mm on 112mm printer! */
        	/* When using smaller page widths, you must ensure you */
	        /* do not try to print off the page! I can't do this for you! */
        settings->bytes_per_scanline = 104;
        settings->bytes_per_scanline_std = 104;
        break;
  }

  get_pagewidth_pageheight(ppd, settings);
  ppdClose(ppd);
}

typedef union _B2L {
    unsigned int iVal;
    char bVal[2];
} B2L;

unsigned int GetPrimaryPulseTime(double speed, double darkness)
{
	//Use a reciprocal polynominal 3d model to return the primary pulse time in uS based on darkness.
	// z = 1.0 / (a + bx^0y^1 + cx^1y^0 + dx^1y^1 + ex^2y^0 + fx^2y^1)

	double temp = 0.0;

	// coefficients
	double a = -8.9711338385694013E-05;
	double b = 8.2848156288666030E-06;
	double c = 5.3012213320783523E-05;
	double d = -1.2905074453851221E-06;
	double e = -2.0772087117514322E-07;
	double f = 5.3615293231061911E-09;

	temp += a;
	temp += b * (darkness+1);
	temp += c * speed;
	temp += d * speed * (darkness+1);
	temp += e * speed*speed;
	temp += f * (speed*speed) * (darkness+1);
	temp = 1.0 / temp;
	return (unsigned int)temp;
}

unsigned int GetSecondaryPulseTime(unsigned long speed)
/*
Set secondary pulse to:

120 between 4.5 and 6ips     => 150 mm
130 between 3.5 and 4.5 ips  => 114 mm
150 between 2.5 and 3.4 ips. => 86 mm
*/
{
	if (speed<=86) {
		return 150;
	} else {
		if ((speed>86)&&(speed<=114)) {
			return 130;
		} else {
			if (speed>114) {
				return 120;
			} else
				return 120;
		}
	}
}

void SetBurntime(unsigned int primaryPulse, unsigned int secondaryPulse)
{
    B2L Val;
    
    Val.iVal = secondaryPulse;
    
    printf("\x1b&p\006%c%c",Val.bVal[1],Val.bVal[0]);

    Val.iVal = primaryPulse;
    printf("\x1b&p\x07%c%c", Val.bVal[1], Val.bVal[0]);
}

/*
 * Job Setup
 */

void
job_setup(struct cups_settings_s * settings)
{
  /*
   * initializers
   * These are unique to a print job and
   * apply to all pages within that job
   */	

#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action; /* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */

  /*
   * Register a signal handler to eject the current page if the
   * job is cancelled.
   */

#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
  sigset(SIGTERM, cancel_job);
#elif defined(HAVE_SIGACTION)
  memset(&action, 0, sizeof(action));

  sigemptyset(&action.sa_mask);
  action.sa_handler = cancel_job;
  sigaction(SIGTERM, &action, NULL);
#else
  signal(SIGTERM, cancel_job);
#endif /* HAVE_SIGSET */

  if (settings->model_number == 203) {  
      SetBurntime(GetPrimaryPulseTime(kr_speedCommand[(settings->print_speed - 1)], (settings->burn_time - 1)), GetSecondaryPulseTime(kr_speedCommand[(settings->print_speed - 1)]));
      output_command(kr_print_speedCommand[(settings->print_speed - 1)]);
      output_command(kr_page_modeCommand[settings->page_mode]);
      output_command(ejectTimeoutCommand[settings->retract_behavior]);
  } else {
      output_command(print_speedCommand[(settings->print_speed - 1)]);
      output_command(burn_timeCommand[(settings->burn_time - 1)]);
      output_command(wasteBasketCommand[settings->retract_behavior]);
      output_command(page_modeCommand[settings->page_mode]);
      set_bm_params(settings);
  }

  set_loop_length(settings);

  if (settings->model_number == 2000 || settings->model_number == 2100)
  {
    /*
     * The 2000 and 2100 have a System Parameter
     * which is a bitfield controlling a few different things
     */
    set_sys_params(settings);
    set_partial_cut(settings);
  }

  else {
    if (settings->model_number != 203)
        settings->partial_cut = 0; /* not supported on other printers */
  }
}

/*
 * Page Setup
 */


/* void page_setup(struct cups_settings_s * settings, cups_page_header_t header) */

void
page_setup(struct cups_settings_s * settings, cups_page_header2_t header)
{
  /*
   * Set page mode
   * NOTE: This is done here rather than in job_setup
   * because we don't have the page header info until now!
   * If page mode is variable, set var + page size of 1 (high 0, low 1)
   * If page mode is fixed, set fixed mode + page size
   * If page mode is Black Mark, set page size (distance between marks)
   */
  /* printf("\n\nPage mode %d\n\n",settings->page_mode);  /* for debugging only */
  if (settings->page_mode == 1 || settings->page_mode == 2)
  {
    int page_high = header.cupsHeight / 256; /* Page High */
    int page_low = header.cupsHeight  % 256;  /* Page Low */
    fprintf(stderr, "***Page Height = %d\n", header.cupsHeight);

    if (settings->model_number == 203) {
        page_high = header.cupsHeight / 8 / 256; /* Page High */
        page_low = header.cupsHeight / 8 % 256;  /* Page Low */
        printf("\x1b&p%%%c%c",(char) page_high, (char) page_low);
    } else {
        printf("\x1b&P%%%c",(char) page_high);
        printf("\x1b&P\x26%c", (char) page_low);
    }

  }
  else
  {
    if (settings->model_number == 203) {
        printf("\033&p%%");
        putchar(0x00);
        putchar(0x5C);
    } else {
        printf("\033&P%%");
        putchar(0x00);
        printf("\033&P\046\001");
    }
  }

  do_reverse(settings); /* if reverse is needed */

}

/*
 * End Page
 */

void
end_page(struct cups_settings_s * settings)
{
  do_advance(settings);
/*  printf("\n\nPage Cut Type %d\n",settings->page_cut_type);  */ /* for debugging only */
/*  printf("Partial Cut %d\n",settings->partial_cut);
  printf("Last Page %d\n",settings->last_page);
  printf("Model Number %d\n\n",settings->model_number);
*/
  
  if (settings->page_cut_type == 1) /* only send a cut if we're in Cut Per Page mode */
  {
    if (settings->partial_cut == 0) /* partial cut is not enabled, just finish the page */
    { 
      output_command(endPageCommand);
    }
    else /* partial cut is enabled AND we're in cut-per-page mode */
    {
        if (settings->model_number == 203) {
            printf("\037%c",settings->partial_cut);
        } else {
            printf("\037");
        }    
    }
  }
  else
  {
  } 	/* not cut per page, so nothing is needed here. */
  	/* the end of page advance has already been done above */

}

/*
 * End Job
 */

void
end_job(struct cups_settings_s * settings)
{
  if ((settings->page_cut_type == 0) || (settings->partial_cut != 0))
  {
    /* 
     * If we're in Cut Per Document mode or partial cut is enabled,
     * no cut was done yet. Send it here.
     */
    output_command(endPageCommand);
  }

  if (settings->clear_presenter == 1)
  {
    output_command(clear_presenterCommand);
  }
  else
  {
    do_eject(settings);
  }
}

/*
 * 'cancel_job()' - Cancel the current job...
 */

void
cancel_job(int sig) /* I - Signal */
{
  int i; /* Looping var */

  (void) sig;

  /*
   * Send out lots of NUL bytes to clear out any pending raster data...
   */

  for (i = 0; i < 310; i++)
    putchar(0);

  /*
   * End the current page and exit...
   */

  end_page(&settings);
  end_job(&settings);

  exit(0);
}

void
clean_up()
{
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action; /* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */

#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
  sigset(SIGTERM, SIG_IGN);
#elif defined(HAVE_SIGACTION)
  memset(&action, 0, sizeof(action));

  sigemptyset(&action.sa_mask);
  action.sa_handler = SIG_IGN;
  sigaction(SIGTERM, &action, NULL);
#else
  signal(SIGTERM, SIG_IGN);
#endif /* HAVE_SIGSET */
}

int
main(int argc, char *argv[])
{

  fprintf(
      stderr,
      "rastertozebrakiosk\n\nZEBRA TECHNOLOGIES KIOSK RASTER DRIVER\nv2010.0.1\nZebra Technologies assumes NO LIABILITY\nresulting from the use of this software.\n\n20 GOTO 10\n\n");

  int fd = 0; /* File descriptor providing CUPS raster data */
  cups_raster_t * ras = NULL; /* Raster stream for printing */
  cups_page_header2_t header;  /* CUPS Page header */
/*  cups_page_header_t header; /* CUPS Page header */
  int page = 0; /* Current page */

  int y = 0; /* Vertical position in page 0 <= y <= header.cupsHeight */
  int i = 0; /* index */

  unsigned char * raster_data = NULL; /* Pointer to raster data buffer */
  unsigned char * original_raster_data_ptr = NULL; /* Copy of original pointer for freeing buffer */

  int left_byte_diff = 0; /* Bytes on left to discard */
  int scan_line_blank = 0; /* Set to TRUE if the entire scan line is blank (no black pixels) */
  int last_black_pixel = 0; /* Position of the last byte containing one or more black pixels in the scan line */
  int num_blank_scan_lines = 0; /* Number of scanlines that were entirely black */

  /* Configuration settings */

  if (argc < 6 || argc > 7)
  {
    fputs(
        "ERROR: rastertozebrakiosk job-id user title copies options [file]\n",
        stderr);

    return EXIT_FAILURE;
  }

  if (argc == 7)
  {
    if ((fd = open(argv[6], O_RDONLY)) == -1)
    {
      perror("ERROR: Unable to open raster file - ");
      sleep(1);

      return EXIT_FAILURE;
    }
  }
  else
  {
    fd = 0;
  }

  /* disable buffering on all streams */
  setbuf(stdout, NULL);
  setbuf(stderr, NULL);
  setbuf(stdin, NULL);

  initialize_settings(argv[5], &settings); /* grab settings from current ppd choices */

  job_setup(&settings); /* send appropriate parameters to the printer */

  ras = cupsRasterOpen(fd, CUPS_RASTER_READ); /* open the data stream for reading */

  page = 0; /* we are on page 0. This is incremented as we see data. */

/*  while (cupsRasterReadHeader(ras, &header)) */
  while (cupsRasterReadHeader2(ras, &header))
  {
//    printf("\nHeader height %d width %d cupsBytesPerLine %d\n", header.cupsHeight, header.cupsWidth, header.cupsBytesPerLine);  /* debug only */
//    printf("Header cupsPageSize[0] %d cupsPageSize[1] %d\n", header.cupsPageSize[0], header.cupsPageSize[1]);  /* debug only */
    
    if ((header.cupsHeight == 0) || (header.cupsBytesPerLine == 0)) 
    {
      break;
    }
    if (raster_data == NULL) /* it will be null on the first time through this function */
    {
      raster_data = malloc(header.cupsBytesPerLine); /* allocate enough memory for one pixel line */
      if (raster_data == NULL) /* couldn´t get memory! */
      {
        CLEANUP;
        return EXIT_FAILURE;
      }
      original_raster_data_ptr = raster_data; /* used to later free the memory */
    }

    page_setup(&settings, header); /* now that we have the image header, set up the page */
    settings.last_page = 0; /* we are not on the last page of the print job */
    page++; /* starting next page */

    fprintf(stderr, "PAGE: %d %d\n", page, header.NumCopies);

    num_blank_scan_lines = 0; /* This is a running total of consecutive blank lines */

    if (header.cupsBytesPerLine <= settings.bytes_per_scanline) /* not a complete line */
    {
      settings.bytes_per_scanline = header.cupsBytesPerLine; /* send only as many bytes as we need */ 
      left_byte_diff = 0;
    }
    else
    {
      settings.bytes_per_scanline = settings.bytes_per_scanline_std;
    }

//    printf("\nsettings.bytes_per_scanline %d \n",settings.bytes_per_scanline);  /* debug only */
    for (y = 0; y < header.cupsHeight; y++)
    {
      if ((y & 127) == 0)
      {
        fprintf(stderr, "INFO: Printing page %d, %d%% complete...\n", page,
            (100 * y / header.cupsHeight));
      }
      if (cupsRasterReadPixels(ras, raster_data, header.cupsBytesPerLine) < 1)
      {
        break;
      }

//      printf("\nleft_byte_diff %d\n",left_byte_diff);  /* debug only */
      raster_data += left_byte_diff;
//      printf("*raster_data %d raster_data[max] %d raster_data[max-1] %d \n",*raster_data, raster_data[settings.bytes_per_scanline],raster_data[settings.bytes_per_scanline-1]);   /* debug only */
      for (i = settings.bytes_per_scanline-1; i >= 0; i--)
      {
        if (((char) *(raster_data + i)) != ((char) 0x00))
        {
          break;
        }
      }
    
      if (i == -1)
      {
        scan_line_blank = TRUE;
        num_blank_scan_lines++;
      }
      else
      {
        last_black_pixel = i + 1;
//        last_black_pixel = i;
        scan_line_blank = FALSE;
      }
//      printf("\nblank line %d, last_black_pixel %d\n", i, last_black_pixel);  /* debug only */
//      printf("settings.bytes_per_scanline %d *(raster_data + settings.bytes_per_scanline-1) %d\n",settings.bytes_per_scanline, *(raster_data + settings.bytes_per_scanline-1));  /* debug only */

      if (scan_line_blank == FALSE)
      {
        if (num_blank_scan_lines > 0)
        {
          fprintf(stderr, "***num_blank_scan_lines = %d\n", num_blank_scan_lines);
          int n1 = num_blank_scan_lines / 256;
          int n2 = num_blank_scan_lines % 256;
          int i;
          for (i = n1; i > 0; i--) /* highbyte = number of full esc j's we need */
          {
            printf("\033J%c", 255);
          }

          printf("\033J%c", n2);

          num_blank_scan_lines = 0;
        }

        printf("\033s");
        putchar((char) ((last_black_pixel > 254) ? 255 : last_black_pixel));

        for (i = 0; i <= (last_black_pixel - 1); i++)
        {
          putchar((char) *raster_data);
          raster_data++;
        }
      }

      raster_data = original_raster_data_ptr;
    }
/*
    if (page == header.NumCopies) /* we´re on the last page */
    /* used for partial cut control */
/*    {
      fprintf(stderr, "******** setting last_page=1\n");
      settings.last_page = 1;
    }
*/
    end_page(&settings); /* do our end of page stuff */
  }

  end_job(&settings); /* end the job */


  if (page == 0) /* if we get here without page being incremented, then there is/was no data */
  {
    fputs("ERROR: No pages found!\n", stderr);
  }
  else
  {
    fputs("INFO: Ready to print.\n", stderr);
  }

  CLEANUP; /* Close out the raster stream */
  clean_up(); /* unregister signal handler, other clean_up tasks if desired later */

  return (page == 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}


/*
 * End of "$Id: rastertozebrakiosk.c,v 1.6 2010/11/18 13:27:07 mwilner Exp $".
 */
