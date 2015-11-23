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

#include "gui.h"

/* #####################################################################
   ##              ANCILLARY MATHEMATICAL FUNCTIONS                   ##
   ##################################################################### */

static void calculate_dimensions(int *width, int *height, int *total_blocks,
                          unsigned long *bytes_per_block,
                          unsigned long largest_file_size,
                          int *blocks_with_excess_byte)
{
	/* Acquire the dimensions of window */
	getmaxyx(stdscr, *height, *width);

	/* If the window dimensions are too small, exit. */
	if ((*height < 16) || (*width < 10)) {
		endwin();
		printf("Terminal dimensions are too small to proceed. "
		       "Increase the size to a minimum of 16w x 10h.\n\n");
		exit(1);
	}

	/* Calculate how many bytes are held in a block.
	   Each block holds a minimum of one byte. The number is
	   biggest file size / # blocks ((width-SIDE_MARGIN) * (height-11))
	   rounded to the next number up. */
	*total_blocks = (*width - SIDE_MARGIN*2) *
	                (*height - VERTICAL_BLACK_SPACE);
	*bytes_per_block = (unsigned long) largest_file_size /
	                   (*total_blocks);
	*blocks_with_excess_byte = (unsigned long) largest_file_size %
	                   (*total_blocks);

	return;
}

static int calculate_current_block(int total_blocks, unsigned long file_offset,
                            unsigned long *offset_index)
{
	/* With a given offset, calculate which element it corresponds to
	   in the offset_index. */
	int i, current_block = 0;
	for (i = 0; i < total_blocks; i++) {

		/* Go block by block, and see if our offset is greater than
		   that of the block's. */
		if (file_offset >= offset_index[i]) {
			current_block = i;
		} else {
			break;
		}

		/* Break if we reach EOF. */
		if ((i+1 < total_blocks) && (offset_index[i+1] == offset_index[i]))
		break;
	}

	return current_block;
}

/* computes the width of the hex offset margin on the left */
static int calculate_max_offset_characters(unsigned long fsz)
{
	char s[32];
	sprintf(s, "%lX", fsz);
	return(strlen(s));
}

/* #####################################################################
   ##                    SCREEN HANDLING FUNCTIONS                    ##
   ##################################################################### */

static char raw_to_ascii(char input)
{
	if ((input > 31) && (input < 127)) return input;
	return '.';
}

/* #####################################################################
   ##                      HANDLE MOUSE ACTIONS                       ##
   ##################################################################### */

static void mouse_clicked(unsigned long *file_offset, unsigned long
                   *offset_index, int width, int height,
                   int total_blocks, char *mode,
                   int mouse_x, int mouse_y, int action)
{
	int index;

	/* In overview mode, you can single-click boxes in the top view
	   to move to the offset that they represent. Double click brings
	   you to the hex representation. */
	if (*mode == OVERVIEW_MODE && (action == BUTTON1_CLICKED ||
		action == BUTTON1_DOUBLE_CLICKED)) {

		/* If the mouse is out of bounds, return. */
		if (mouse_x < SIDE_MARGIN || mouse_x > width - SIDE_MARGIN - 1
			|| mouse_y < 2 || mouse_y > height - 8)
			return;

		/* Calculate the box it falls in. */
		index = (width-SIDE_MARGIN*2) * (mouse_y-2) +
					mouse_x-SIDE_MARGIN;

		/* Set the offset to the value in the box. */
		if (index < total_blocks && index >= 0)
			*file_offset = offset_index[index];

		/* If double-clicked, set to HEX MODE. */
		if (action == BUTTON1_DOUBLE_CLICKED)
			*mode = HEX_MODE;
	}

	return;
}


/* #####################################################################
   ##                      GENERATE TITLE BAR                         ##
   ##################################################################### */

static void generate_titlebar(struct file *file_one, struct file *file_two,
                       unsigned long file_offset, int width, int height,
                       char mode, int display)
{
	int i;
	char title_offset[32];
	char bottom_message[128];

	/* Define and set colour for the title bar. */
	init_pair(TITLE_BAR, COLOR_BLACK, COLOR_WHITE);
	attron(COLOR_PAIR(TITLE_BAR) | A_BOLD);

	/* Create the title bar background. */
	for (i = 0; i < width; i++) {
		mvprintw(0, i, " ");
		mvprintw(height-1, i, " ");
	}

	/* Create the title. */
	mvprintw(0, SIDE_MARGIN, "hexcompare: %s vs. %s",
	         file_one->name, file_two->name);

	/* Indicate file offset. */
	sprintf(title_offset, " 0x%04x", (unsigned int) file_offset);
	mvprintw(0, width-strlen(title_offset)-SIDE_MARGIN, "%s",
	         title_offset);

	/* Write bottom menu options. */
	strcpy(bottom_message, "Quit: q | ");

	if (display == HEX_VIEW) {
		strcat(bottom_message, "Hex Mode: m | ");
	} else {
		strcat(bottom_message, "ASCII Mode: m | ");
	}

	if (mode == OVERVIEW_MODE) {
		strcat(bottom_message, "Full View: v | Page & Arrow Keys to Move");
	} else {
		strcat(bottom_message, "Mixed View: v | Arrow Keys to Move");
	}

	mvprintw(height-1, SIDE_MARGIN, "%s", bottom_message);

	/* Set the colour scheme back to default. */
	attroff(COLOR_PAIR(TITLE_BAR) | A_BOLD);

	return;
}

/* #####################################################################
   ##            GENERATE BLOCK DATA FOR OVERVIEW MODE                ##
   ##################################################################### */

static char *generate_blocks(struct file *file_one, struct file *file_two,
                 char *block_cache, int total_blocks,
                 unsigned long bytes_per_block,
                 int blocks_with_excess_byte)
{
	int i;
	unsigned char *block_one, *block_two;

	block_one = malloc(bytes_per_block + 1);
	block_two = malloc(bytes_per_block + 1);

	/* De-allocate existing memory that holds the block data. */
	if (block_cache != NULL) free(block_cache);

	/* Allocate the correct amount of memory and initialize it. */
	block_cache = malloc(total_blocks);
	memset(block_cache, BLOCK_EMPTY, total_blocks);

	/* Seek to start of file. */
	fseek(file_one->pointer, 0, SEEK_SET);
	fseek(file_two->pointer, 0, SEEK_SET);

	/* Compare bytes of file_one with file_two. Store results in */
	/* a dynamically-sized block_cache. */
	for (i = 0; i < total_blocks; i++) {
		int bytes_read_one, bytes_read_two;
		size_t bytes_in_block, j;

		/* Calculate how many bytes to read for this block. */
		if (i < blocks_with_excess_byte) {
			bytes_in_block = bytes_per_block + 1;
		} else {
			bytes_in_block = bytes_per_block;
		}

		/* Set the default. */
		block_cache[i] = BLOCK_SAME;

		/* Clear the block of data. */
		memset(block_one, '\0', bytes_in_block);
		memset(block_two, '\0', bytes_in_block);

		/* Read in the next block of data. */
		bytes_read_one = fread(block_one, bytes_in_block,
		                           1, file_one->pointer);
		bytes_read_two = fread(block_two, bytes_in_block,
		                           1, file_two->pointer);

		/* Stop here if we read 0 bytes. Both files are fully read. */
		if (bytes_read_one == 0 && bytes_read_two == 0) {
			block_cache[i] = BLOCK_EMPTY;
			break;
		}

		/* If the file lengths don't match up, we know the blocks are
		   different. Go to the next block. */
		if (bytes_read_one != bytes_read_two) {
			block_cache[i] = BLOCK_DIFFERENT;
			continue;
		}

		/* Both blocks have the same length. Compare them character by
		   character. */
		for (j = 0; j < bytes_in_block; j++) {
			if (block_one[j] != block_two[j]) {
				block_cache[i] = BLOCK_DIFFERENT;
				break;
			}
		}
	}

	/* free memory */
	free(block_one);
	free(block_two);

	return block_cache;
}

/* #####################################################################
   ##            BLOCK OFFSET FUNCTIONS FOR OVERVIEW MODE             ##
   ##################################################################### */

static unsigned long *generate_offsets(unsigned long *offset_index,
                                       int total_blocks,
                                       unsigned long bytes_per_block,
                                       int blocks_with_excess_byte)
{
	int i;
	unsigned long offset = 0;

	/* De-allocate existing memory that holds the offset data. */
	if (offset_index != NULL) free(offset_index);

	/* Allocate the correct amount of memory and initialize it. */
	offset_index = malloc(total_blocks * sizeof(unsigned long));
	memset(offset_index, 0, total_blocks);

	/* Generate offset data. */
	for (i = 0; i < total_blocks; i++) {
		offset_index[i] = offset;
		if (i < blocks_with_excess_byte) {
			offset += bytes_per_block + 1;
		} else {
			offset += bytes_per_block;
		}
	}

	return offset_index;
}

static unsigned long calculate_offset(unsigned long file_offset,
                                      unsigned long *offset_index, int width,
                                      int total_blocks, int shift_type,
                                      unsigned long largest_file_size)
{

	/* Initialize variables. */
	unsigned long new_offset = file_offset;
	int blocks_in_row = width - SIDE_MARGIN*2;
	int current_block = 0;

	/* Calculate parameters for the offset. */
	int offset_char_size = calculate_max_offset_characters(largest_file_size);
	int hex_width = width - offset_char_size - 3 - SIDE_MARGIN * 2;
	int offset_jump = (hex_width - (hex_width % 4)) / 4;

	/* Locate the current block we're in. */
	current_block = calculate_current_block(total_blocks, file_offset,
	                                        offset_index);

	/* Return the offset of the block we want. */
	switch (shift_type) {
		case LEFT_BLOCK:
			if (current_block > 0) current_block--;
			break;
		case RIGHT_BLOCK:
			if (current_block < total_blocks - 1) current_block++;
			break;
		case UP_ROW:
			if (current_block - blocks_in_row < 0) {
				current_block = 0;
			} else {
				current_block -= blocks_in_row;
			}
			break;
		case DOWN_ROW:
			if (current_block + blocks_in_row >= total_blocks) {
				current_block = total_blocks - 1;
			} else {
				current_block += blocks_in_row;
			}
			break;
		case UP_LINE:
			if (file_offset - offset_jump > file_offset) {
				current_block = 0;
				break;
			} else {
				return file_offset - offset_jump + 1;
			}
		case DOWN_LINE:
			if (file_offset + offset_jump >= largest_file_size) {
				return file_offset;
			} else {
				return file_offset + offset_jump - 1;
			}
	}

	new_offset = offset_index[current_block];
	return new_offset;
}

/* returns a pointer to the 'filename' part of a path. One could argue
 * that basename() would be appropriate here, but the problem is that
 * some platforms have a basename() that modifies the passed string,
 * which we want to avoid. */
static char *getfilename(char *f) {
	char *res = f;
	for (; *f != 0; f += 1) {
		if ((*f == '/') || (*f == '\\')) {
			res = f + 1;
		}
	}
	return(res);
}

/* #####################################################################
   ##           DRAW ROWS OF RAW DATA IN HEX/ASCII FORM               ##
   ##################################################################### */

static void display_file_names(int row, struct file *file_one,
                               struct file *file_two, int offset_char_size,
                               int offset_jump)
{
	char *filename_one, *filename_two;

	/* ltrim the filenames if any / character is found */
	filename_one = getfilename(file_one->name);
	filename_two = getfilename(file_two->name);

	/* Display the file names. */
	attron(COLOR_PAIR(TITLE_BAR));
	mvprintw(row, SIDE_MARGIN+offset_char_size+3, " %s   ",
	        filename_one);
	mvprintw(row, SIDE_MARGIN+offset_char_size+4+
	        offset_jump*2, " %s   ", filename_two);
	attroff(COLOR_PAIR(TITLE_BAR));
}

static void display_offsets(int start_row, int finish_row, int offset_jump,
                            int offset_char_size, unsigned long file_offset)
{
	int i;
	char offset_line[32];
	unsigned long temp_offset = file_offset;

	attron(COLOR_PAIR(TITLE_BAR));
	for (i = start_row; i < finish_row; i++) {
		sprintf(offset_line, "0x%%0%ix ", offset_char_size);
		mvprintw(i, SIDE_MARGIN, offset_line, temp_offset);
		temp_offset += offset_jump - 1;
	}
	attroff(COLOR_PAIR(TITLE_BAR));
}

static void draw_hex_data(int start_row, int finish_row, struct file *file_one,
                          struct file *file_two, unsigned long file_offset,
                          int offset_char_size, int offset_jump, int display)
{

	unsigned long temp_offset = file_offset;
	int i, j;

	for (i = start_row; i < finish_row; i++) {
		int bold = 0;
		for (j = SIDE_MARGIN+offset_char_size+3; j <
			SIDE_MARGIN+offset_char_size+offset_jump*2+1; j += 2) {
			int colour_pair;
			unsigned char byte_one, byte_two;
			char byte_one_hex[16], byte_two_hex[16];
			char byte_one_ascii, byte_two_ascii;
			int bytes_read_one, bytes_read_two;

			/* Seek to the proper locations in the file. */
			fseek(file_one->pointer, temp_offset, SEEK_SET);
			fseek(file_two->pointer, temp_offset, SEEK_SET);

			/* Read a byte from the files. */
			bytes_read_one = fread(&byte_one, 1, 1, file_one->pointer);
			bytes_read_two = fread(&byte_two, 1, 1, file_two->pointer);

			/* Convert binary to ASCII hex. */
			sprintf(byte_one_hex, "%02x", byte_one);
			sprintf(byte_two_hex, "%02x", byte_two);
			byte_one_hex[2] = '\0';
			byte_two_hex[2] = '\0';

			/* Interpret ASCII version of bytes. */
			byte_one_ascii = raw_to_ascii(byte_one);
			byte_two_ascii = raw_to_ascii(byte_two);

			/* Make every other byte bold. */
			if (bold != 0) attron(A_BOLD);

			/* Post results. */

			/* Byte 1:
			   Determine if its EMPTY/DIFFERENT/SAME. */
			if (bytes_read_one == 0) {
				colour_pair = BLOCK_EMPTY;
			} else if (bytes_read_two == 0) {
				colour_pair = BLOCK_DIFFERENT;
			} else if (byte_one == byte_two) {
				colour_pair = BLOCK_SAME;
			} else {
				colour_pair = BLOCK_DIFFERENT;
			}

			/* Display the block. */
			attron(COLOR_PAIR(colour_pair));
			if (colour_pair == BLOCK_EMPTY) {
				mvprintw(i,j, "  ");
			} else if (display == HEX_VIEW) {
				mvprintw(i,j, " %c", byte_one_ascii);
			} else {
				mvprintw(i,j, "%s", byte_one_hex);
			}
			attroff(COLOR_PAIR(colour_pair));

			/* Byte 2:
			   Determine if its EMPTY/DIFFERENT/SAME. */
			if (bytes_read_two == 0) {
				colour_pair = BLOCK_EMPTY;
			} else if (bytes_read_one == 0) {
				colour_pair = BLOCK_DIFFERENT;
			} else if (byte_one == byte_two) {
				colour_pair = BLOCK_SAME;
			} else {
				colour_pair = BLOCK_DIFFERENT;
			}

			/* Display the block. */
			attron(COLOR_PAIR(colour_pair));
			if (colour_pair == BLOCK_EMPTY) {
				mvprintw(i,j+offset_jump*2+1, "  ");
			} else if (display == HEX_VIEW) {
				mvprintw(i,j+offset_jump*2+1, " %c", byte_two_ascii);
			} else {
				mvprintw(i,j+offset_jump*2+1, "%s", byte_two_hex);
			}
			attroff(COLOR_PAIR(colour_pair));

			/* Switch bold characters with non-bold characters. */
			if (bold != 0) attroff(A_BOLD);
			bold ^= 1;

			temp_offset++;
		}
	}

	return;
}

/* #####################################################################
   ##              GENERATE SCREEN IN OVERVIEW MODE                   ##
   ##################################################################### */

static void generate_overview(struct file *file_one, struct file *file_two,
                              unsigned long *file_offset, int width,
                              int height, char *block_cache, int total_blocks,
                              unsigned long *offset_index, int display,
                              unsigned long largest_file_size)
{

	/* In overview mode:

	   BLOCKDIAGRAM-BLOCKDIAGRAM-BLOCKDIAGRAM-BLOCKDIAGRAM
	   BLOCKDIAGRAM-BLOCKDIAGRAM-BLOCKDIAGRAM-BLOCKDIAGRAM
	   BLOCKDIAGRAM-BLOCKDIAGRAM-BLOCKDIAGRAM-BLOCKDIAGRAM
	   BLOCKDIAGRAM-BLOCKDIAGRAM-BLOCKDIAGRAM-BLOCKDIAGRAM

	          FILENAME 1               FILENAME 2
	   OFFSET HEX1-HEX1-HEX1-HEX1-HEX1 HEX2-HEX2-HEX2-HEX2
	   OFFSET HEX1-HEX1-HEX1-HEX1-HEX1 HEX2-HEX2-HEX2-HEX2
	   OFFSET HEX1-HEX1-HEX1-HEX1-HEX1 HEX2-HEX2-HEX2-HEX2

	   Where BLOCKDIAGRAM is the blue/red squares comparing
	   hex blocks from file 1 and file 2. Size is variable.
	   SCROLLBAR is the scrollbar representing how far in
	   the file we are, HEX1 is the hex for file 1 from the
	   offset, and HEX2 is the hex for file 2 from the
	   offset. */

	/* Create variables. */
	int i, j;
	int offset_char_size;
	int hex_width;
	int offset_jump;
	int current_block;

	/* Define colors block diagram. */
	init_pair(BLOCK_SAME,      COLOR_WHITE, COLOR_BLUE);
	init_pair(BLOCK_DIFFERENT, COLOR_WHITE, COLOR_RED);
	init_pair(BLOCK_EMPTY,     COLOR_BLACK, COLOR_CYAN);
	init_pair(BLOCK_ACTIVE,    COLOR_BLACK, COLOR_YELLOW);
	init_pair(TITLE_BAR,       COLOR_BLACK, COLOR_WHITE);

	/* Find which block in the diagram is active based off of
	   the current offset. */

	/* Generate the block diagram. */
	for (i = 0; i < height - VERTICAL_BLACK_SPACE; i++) {
		for (j = 0; j < width - SIDE_MARGIN*2; j++) {

			/* Draw the blocks that are matching/different/empty. */
			int index = i*(width-SIDE_MARGIN*2)+j;
			attron(COLOR_PAIR(block_cache[index]));
			mvprintw(i+2,j+SIDE_MARGIN," ");
			attroff(COLOR_PAIR(block_cache[index]));
		}
	}

	/* Show the active block. */
	current_block = calculate_current_block(total_blocks, *file_offset, offset_index);
	attron(COLOR_PAIR(BLOCK_ACTIVE));
	mvprintw(current_block / (width - SIDE_MARGIN*2) + 2,
	         current_block % (width - SIDE_MARGIN*2) + SIDE_MARGIN," ");
	attroff(COLOR_PAIR(BLOCK_ACTIVE));

	/* Generate the offset markers.
	   Calculate parameters for the offset. */
	offset_char_size = calculate_max_offset_characters(largest_file_size);
	hex_width = width - offset_char_size - 3 - SIDE_MARGIN * 2;
	offset_jump = (hex_width - (hex_width % 4)) / 4;

	/* Display the offsets.
	   Display the hex offsets on the left. */
	display_offsets(height-7, height-2, offset_jump, offset_char_size,
	                *file_offset);

	/* Generate HEX characters
	   Seek to initial offset. */
	draw_hex_data(height - 7, height - 2, file_one, file_two,
	              *file_offset, offset_char_size, offset_jump, display);

	/* Write the file titles. */
	display_file_names(height-8, file_one, file_two, offset_char_size,
	                   offset_jump);

	return;
}

/* #####################################################################
   ##                 GENERATE SCREEN IN HEX MODE                     ##
   ##################################################################### */

static void generate_hex(struct file *file_one, struct file *file_two,
                         unsigned long *file_offset, int width, int height,
                         int display, unsigned long largest_file_size)
{

	/* In hex mode:

	          FILENAME 1               FILENAME 2
	   OFFSET HEX1-HEX1-HEX1-HEX1-HEX1 HEX2-HEX2-HEX2-HEX2
	   OFFSET HEX1-HEX1-HEX1-HEX1-HEX1 HEX2-HEX2-HEX2-HEX2
	   OFFSET HEX1-HEX1-HEX1-HEX1-HEX1 HEX2-HEX2-HEX2-HEX2
	   OFFSET HEX1-HEX1-HEX1-HEX1-HEX1 HEX2-HEX2-HEX2-HEX2
	   OFFSET HEX1-HEX1-HEX1-HEX1-HEX1 HEX2-HEX2-HEX2-HEX2
	   OFFSET HEX1-HEX1-HEX1-HEX1-HEX1 HEX2-HEX2-HEX2-HEX2
	   OFFSET HEX1-HEX1-HEX1-HEX1-HEX1 HEX2-HEX2-HEX2-HEX2
	   OFFSET HEX1-HEX1-HEX1-HEX1-HEX1 HEX2-HEX2-HEX2-HEX2
	   OFFSET HEX1-HEX1-HEX1-HEX1-HEX1 HEX2-HEX2-HEX2-HEX2
	*/

	/* Generate the offset markers.
	   Calculate parameters for the offset. */
	int offset_char_size = calculate_max_offset_characters(largest_file_size);
	int hex_width = width - offset_char_size - 3 - SIDE_MARGIN * 2;
	int offset_jump = (hex_width - (hex_width % 4)) / 4;

	/* Define colors block diagram. */
	init_pair(BLOCK_SAME,      COLOR_WHITE, COLOR_BLUE);
	init_pair(BLOCK_DIFFERENT, COLOR_WHITE, COLOR_RED);
	init_pair(BLOCK_EMPTY,     COLOR_BLACK, COLOR_CYAN);
	init_pair(BLOCK_ACTIVE,    COLOR_BLACK, COLOR_YELLOW);
	init_pair(TITLE_BAR,       COLOR_BLACK, COLOR_WHITE);

	/* Display the hex offsets on the left. */
	display_offsets(3, height-2, offset_jump, offset_char_size,
	                *file_offset);

	/* Generate HEX characters
	   Seek to initial offset. */
	draw_hex_data(3, height - 2, file_one, file_two,
	              *file_offset, offset_char_size, offset_jump, display);


	/* Write the file titles. */
	display_file_names(2, file_one, file_two, offset_char_size,
	                   offset_jump);

	return;
}

/* #####################################################################
   ##                    GENERATE SCREEN VIEW                         ##
   ##################################################################### */

static void generate_screen(struct file *file_one, struct file *file_two,
                            char mode, unsigned long *file_offset, int width,
                            int height, char *block_cache, int total_blocks,
                            unsigned long *offset_index, int display,
                            unsigned long largest_file_size)
{
	/* Clear the window. */
	erase();

	/* Generate the title bar. */
	generate_titlebar(file_one, file_two, *file_offset, width, height,
		          mode, display);

	/* Generate the window contents according to the mode we're in. */
	if (mode == OVERVIEW_MODE) {
		generate_overview(file_one, file_two, file_offset,
		                  width, height, block_cache, total_blocks,
		                  offset_index, display, largest_file_size);

	} else if (mode == HEX_MODE) {
		generate_hex(file_one, file_two, file_offset, width, height,
		             display, largest_file_size);
	}
}

/* #####################################################################
   ##                       MAIN FUNCTION                             ##
   ##################################################################### */

void start_gui(struct file *file_one, struct file *file_two,
               unsigned long largest_file_size)
{
	/* Initiate variables */
	unsigned long file_offset = 0;      /* File offset. */
	char mode = OVERVIEW_MODE;          /* Display mode. */
	int key_pressed;                    /* What key is pressed. */
	char *block_cache = NULL;           /* A quick comparison overview. */
	unsigned long *offset_index = NULL; /* Keep track of offsets per block. */
	int display = HEX_VIEW;             /* ASCII vs. HEX mode. */
	MEVENT mouse;                       /* Mouse event struct. */
	WINDOW *main_window;                /* Pointer for main window. */

	int width, height, total_blocks, blocks_with_excess_byte;
	unsigned long bytes_per_block;

	/* Initiate the display. */
	main_window = initscr(); /* Start curses mode. */
	if (has_colors() != TRUE) {
		puts("Error: Your terminal do not seem to handle colors.");
		endwin();
		return;
	}
	start_color();           /* Enable the use of colours. */
	raw();                   /* Disable line buffering. */
	noecho();                /* Don't echo while we get characters. */
	keypad(stdscr, TRUE);    /* Enable capture of arrow keys. */
	curs_set(0);             /* Make the cursor invisible. */
	mousemask(ALL_MOUSE_EVENTS, NULL); /* Get all mouse events. */
	clear();                 /* Clear out the screen */

	/* Calculate values based on window dimensions. */
	calculate_dimensions(&width, &height, &total_blocks, &bytes_per_block,
                            largest_file_size, &blocks_with_excess_byte);

	/* Compile the block/offset cache. The block cache contains an index
	   of what the general differences are between the two compared
	   files. It exists to avoid re-comparing the two files every time
	   the screen is regenerated. The offset cache keeps track of what
	   the offsets are for each block in the block diagram, as they
	   may be uneven. */

	block_cache = generate_blocks(file_one, file_two, block_cache,
	                              total_blocks, bytes_per_block,
	                              blocks_with_excess_byte);
	offset_index = generate_offsets(offset_index, total_blocks,
	                          bytes_per_block, blocks_with_excess_byte);

	/* Generate initial screen contents. */
	generate_screen(file_one, file_two, mode, &file_offset, width, height,
	                block_cache, total_blocks, offset_index, display,
                        largest_file_size);

	/* Wait for user-keypresses and react accordingly. */
	for(;;) {
		/* poll the next keypress event from curses */
		key_pressed = wgetch(main_window);

		/* if we got 'q' or ESC, then quit */
		if ((key_pressed == 'q') || (key_pressed == 27)) break;

		switch (key_pressed) {
			/* Move left/right/down/up on the blog diagram in overview
			   mode. */

			case KEY_LEFT:
				if (mode == OVERVIEW_MODE)
				file_offset = calculate_offset(file_offset,
				              offset_index, width, total_blocks,
				              LEFT_BLOCK, largest_file_size);
				break;
			case KEY_RIGHT:
				if (mode == OVERVIEW_MODE)
				file_offset = calculate_offset(file_offset,
				              offset_index, width, total_blocks,
				              RIGHT_BLOCK, largest_file_size);
				break;
			case KEY_UP:
				if (mode == OVERVIEW_MODE)
				file_offset = calculate_offset(file_offset,
				              offset_index, width, total_blocks,
				              UP_ROW, largest_file_size);
				else if (mode == HEX_MODE)
				file_offset = calculate_offset(file_offset,
				              offset_index, width, total_blocks,
				              UP_LINE, largest_file_size);
				break;
			case KEY_DOWN:
				if (mode == OVERVIEW_MODE)
				file_offset = calculate_offset(file_offset,
				              offset_index, width, total_blocks,
				              DOWN_ROW, largest_file_size);
				else if (mode == HEX_MODE)
				file_offset = calculate_offset(file_offset,
				              offset_index, width, total_blocks,
				              DOWN_LINE, largest_file_size);
				break;
			case KEY_NPAGE:
				file_offset = calculate_offset(file_offset,
				              offset_index, width, total_blocks,
				              DOWN_LINE, largest_file_size);
				break;
			case KEY_PPAGE:
				file_offset = calculate_offset(file_offset,
				              offset_index, width, total_blocks,
				              UP_LINE, largest_file_size);
				break;
			case 'm':
				if (display == ASCII_VIEW) display = HEX_VIEW;
				else display = ASCII_VIEW;
				break;
			case 'v':
				if (mode == OVERVIEW_MODE) mode = HEX_MODE;
				else mode = OVERVIEW_MODE;
				break;
			case KEY_MOUSE:
				if (nc_getmouse(&mouse) == OK) {

					/* Left single-click. */
					if (mouse.bstate & BUTTON1_CLICKED)
						mouse_clicked(&file_offset, offset_index,
									 width, height, total_blocks, &mode,
									 mouse.x, mouse.y, BUTTON1_CLICKED);

					/* Left double-click. */
					if (mouse.bstate & BUTTON1_DOUBLE_CLICKED)
						mouse_clicked(&file_offset, offset_index,
								     width, height, total_blocks, &mode,
								     mouse.x, mouse.y,
								     BUTTON1_DOUBLE_CLICKED);
				}
				break;


			/* Redraw the window on resize. Recaltulate dimensions,
			   and redo the block/offset cache. */
			case KEY_RESIZE:
				calculate_dimensions(&width, &height, &total_blocks,
	                               &bytes_per_block, largest_file_size,
	                               &blocks_with_excess_byte);
				block_cache = generate_blocks(file_one, file_two,
				            block_cache, total_blocks, bytes_per_block,
				            blocks_with_excess_byte);
				offset_index = generate_offsets(offset_index,
				               total_blocks, bytes_per_block,
				               blocks_with_excess_byte);
				break;
		}

		generate_screen(file_one, file_two, mode, &file_offset, width,
	                        height, block_cache, total_blocks,
                                offset_index, display, largest_file_size);
	}

	/* End curses mode and exit. */
	clear();
	refresh();
	endwin();
	free(block_cache);
	return;
}
