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

#include <stdio.h>
#include "general.h"
#include "gui.h"


int main(int argc, char **argv)
{
	struct file file_one, file_two;
	unsigned long largest_file_size;
	char *message[] = {
		"Arguments missing.\n",
		"Usage:\n  hexcompare file1 [file2]\n",
		"Failed to open file \"%s\".\n"
	};

	/* Verify that we have enough input arguments. */
	if (argc < 2) {
		puts("hexcompare v" PVER "\n");
		printf("%s%s", message[0], message[1]);
		return 1;
	}

	/* Load in the file names. */
	file_one.name = argv[1];
	if (argc == 2) {
		file_two.name = argv[1];
	} else {
		file_two.name = argv[2];
	}

	/* Open the files.
	   Present the user with an error message if they cannot be opened. */
	if ((file_one.pointer = fopen(file_one.name, "rb")) == NULL) {
		printf(message[2], file_one.name);
		return 1;
	}
	if ((file_two.pointer = fopen(file_two.name, "rb")) == NULL) {
		printf(message[2], file_two.name);
		fclose(file_one.pointer);
		return 1;
	}

	/* Get the file size */
	fseek(file_one.pointer, 0, SEEK_END);
	file_one.size = ftell(file_one.pointer);

	fseek(file_two.pointer, 0, SEEK_END);
	file_two.size = ftell(file_two.pointer);

	/* Determine the largest file size */
	largest_file_size = (file_one.size > file_two.size) ? file_one.size
	                    : file_two.size;

	/* Initiate the GUI display. */
	start_gui(&file_one, &file_two, largest_file_size);

	/* Close the files. */
	fclose(file_one.pointer);
	fclose(file_two.pointer);

	/* Clean exit. */
	return 0;
}
