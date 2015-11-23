/*
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HEX_GUI
#define HEX_GUI

#include <curses.h>
#include <stdlib.h>
#include <string.h>
#include "general.h"

#define OVERVIEW_MODE 0
#define HEX_MODE 1

#define HEX_VIEW 0
#define ASCII_VIEW 1

#define BLOCK_SAME 1            /* Blue Box */
#define BLOCK_DIFFERENT 2       /* Red Box */
#define BLOCK_EMPTY 3           /* Grey Box */
#define BLOCK_ACTIVE 4          /* Green Box */
#define TITLE_BAR 5             /* Black text on White Background */

#define SIDE_MARGIN 2           /* Width of the side margins in chars */
#define VERTICAL_BLACK_SPACE 11 /* Sum of padding from top to bottom */

#define UP_ROW 2
#define DOWN_ROW -2
#define LEFT_BLOCK -1
#define RIGHT_BLOCK 1
#define UP_LINE 3
#define DOWN_LINE -3

/* If I'm not running PDCURSES, I assume it's going to be ncurses */
#ifndef __PDCURSES__
#define nc_getmouse getmouse
#endif

void start_gui(struct file *file_one, struct file *file_two,
               unsigned long largest_file_size);

#endif
