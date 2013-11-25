
/******************************************************************************
 *                                  Grapes                                    *
 *                                                                            *
 *    Copyright (C) 2013 Andreas Grapentin                                    *
 *                                                                            *
 *    This program is free software: you can redistribute it and/or modify    *
 *    it under the terms of the GNU General Public License as published by    *
 *    the Free Software Foundation, either version 3 of the License, or       *
 *    (at your option) any later version.                                     *
 *                                                                            *
 *    This program is distributed in the hope that it will be useful,         *
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *    GNU General Public License for more details.                            *
 *                                                                            *
 *    You should have received a copy of the GNU General Public License       *
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 ******************************************************************************/


#pragma once

#include "util.h"

#include <stdlib.h>

/* maps a file read-only to a buffer via mmap, use file_unmap to free
 *
 * params:
 *   filename - the path to the file to map
 *   length - a pointer where the size of the allocation is stored in
 *
 * errors:
 *   may fail and set errno for the same reasons as open, fstat and mmap
 *
 * returns:
 *   a pointer to the mapped area, if successful, NULL otherwise
 */
void *file_map(const char *filename, size_t *length) MAY_FAIL;

/* unmaps a memory area previously mapped with file_map
 *
 * params:
 *   data - the pointer reurned by file_map
 *   length - the length returned by file_map
 *
 * errors:
 *   shit hits the fan if you pass anything except values returned by file_map
 */
void file_unmap(void *data, size_t length);