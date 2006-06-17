/*
 * muscle-filesystem.c: Support for MuscleCard Applet from musclecard.com 
 *
 * Copyright (C) 2006, Identity Alliance, Thomas Harning <support@identityalliance.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "muscle-filesystem.h"
#include <opensc/errors.h>
#include <memory.h>
#include <stdio.h>
#include <assert.h>

#define MSCFS_NO_MEMORY  	SC_ERROR_OUT_OF_MEMORY
#define MSCFS_INVALID_ARGS 	SC_ERROR_INVALID_ARGUMENTS
#define MSCFS_FILE_NOT_FOUND 	SC_ERROR_FILE_NOT_FOUND
#define MSCFS_CACHE_INCREMENT 128


static const u8* ignoredFiles[] = {
	(const u8*)"l0\0\0",
	(const u8*)"L0\0\0",
	NULL
};

mscfs_t *mscfs_new() {
	mscfs_t *fs = (mscfs_t*)malloc(sizeof(mscfs_t));
	memset(fs, 0, sizeof(mscfs_t));
	memcpy(fs->currentPath, "\x3F\x00", 2);
	return fs;
}

void mscfs_free(mscfs_t *fs) {
	mscfs_clear_cache(fs);
}

void mscfs_clear_cache(mscfs_t* fs) {
	if(!fs->cache.array) {
		return;
	}
	free(fs->cache.array);
	fs->cache.array = NULL;
	fs->cache.totalSize = 0;
	fs->cache.size = 0;
}

int mscfs_is_ignored(mscfs_t* fs, u8* objectId)
{
	int ignored = 0;
	const u8** ptr = ignoredFiles;
	while(ptr && *ptr && !ignored) {
		if(0 == memcmp(objectId, *ptr, 4))
			ignored = 1;
		ptr++;
	}
	return ignored;
}

int mscfs_push_file(mscfs_t* fs, mscfs_file_t *file)
{
	mscfs_cache_t *cache = &fs->cache;
	if(!cache->array || cache->size == cache->totalSize) {
		int length = cache->totalSize + MSCFS_CACHE_INCREMENT;
		mscfs_file_t *oldArray;
		cache->totalSize = length;
		oldArray = cache->array;
		cache->array = malloc(sizeof(mscfs_file_t) * length);
		if(!cache->array)
			return MSCFS_NO_MEMORY;
		if(oldArray) {
			memcpy(cache->array, oldArray, sizeof(mscfs_file_t) * cache->size);
			free(oldArray);
		}
	}
	cache->array[cache->size] = *file;
	cache->size++;
	return 0;
}

int mscfs_update_cache(mscfs_t* fs) {
	mscfs_file_t file;
	int r;
	mscfs_clear_cache(fs);
	r = fs->listFile(&file, 1, fs->udata);
	if(r == 0)
		return 0;
	else if(r < 0)
		return r;
	while(1) {
		if(!mscfs_is_ignored(fs, file.objectId)) {
			/* Check if its a directory in the root */
			if(file.objectId[2] == 0 && file.objectId[3] == 0) {
				file.objectId[2] = file.objectId[0];
				file.objectId[3] = file.objectId[1];
				file.objectId[0] = 0x3F;
				file.objectId[1] = 0x00;
				file.ef = 0;
			} else  {
				file.ef = 1; /* File is a working elementary file */
			}
			
			mscfs_push_file(fs, &file);
		}
		r = fs->listFile(&file, 0, fs->udata);
		if(r == 0)
			break;
		else if(r < 0)
			return r;
	}
	return fs->cache.size;
}

void mscfs_check_cache(mscfs_t* fs)
{
	if(!fs->cache.array) {
		mscfs_update_cache(fs);
	}
}

int mscfs_lookup_path(mscfs_t* fs, const u8 *path, int pathlen, u8 objectId[4], int isDirectory)
{
	if ((pathlen & 1) != 0) /* not divisble by 2 */
		return MSCFS_INVALID_ARGS;
	if(isDirectory) {
		/* Directory must be right next to root */
		if((0 == memcmp(path, "\x3F\x00", 2) && pathlen == 4)
		|| (0 == memcmp(fs->currentPath, "\x3F\x00", 2) && pathlen == 2)) {
			objectId[0] = path[pathlen - 2];
			objectId[1] = path[pathlen - 1];
			objectId[2] = objectId[3] = 0;
		} else {
			return MSCFS_INVALID_ARGS;
		}
	}
	objectId[0] = fs->currentPath[0];
	objectId[1] = fs->currentPath[1];
	/* Chop off the root in the path */
	if(pathlen > 2 && memcmp(path, "\x3F\x00", 2) == 0) {
		path += 2;
		pathlen -= 2;
		objectId[0] = 0x3F;
		objectId[1] = 0x00;
	}
	/* Limit to a single directory */
	if(pathlen > 4)
		return MSCFS_INVALID_ARGS;
	/* Reset to root */
	if(0 == memcmp(path, "\x3F\x00", 2) && pathlen == 2) {
		objectId[0] = objectId[2] = path[0];
		objectId[1] = objectId[3] = path[1];
	} else if(pathlen == 2) { /* Path preserved for current-path */
		objectId[2] = path[0];
		objectId[3] = path[1];
	} else if(pathlen == 4) {
		objectId[0] = path[0];
		objectId[1] = path[1];
		objectId[2] = path[2];
		objectId[3] = path[3];
	}
	
	return 0;
}

int mscfs_lookup_local(mscfs_t* fs, const int id, u8 objectId[4])
{
	objectId[0] = fs->currentPath[0];
	objectId[1] = fs->currentPath[1];
	objectId[2] = (id >> 8) & 0xFF;
	objectId[3] = id & 0xFF;
	return 0;
}

/* -1 any, 0 DF, 1 EF */
int mscfs_check_selection(mscfs_t *fs, int requiredItem)
{
	if(fs->currentPath[0] == 0 && fs->currentPath[1] == 0)
		return MSCFS_INVALID_ARGS;
	if(requiredItem == 1 && fs->currentFile[0] == 0 && fs->currentFile[1] == 0)
		return MSCFS_INVALID_ARGS;
	return 0;
}

int mscfs_loadFileInfo(mscfs_t* fs, const u8 *path, int pathlen, mscfs_file_t **file_data, int* idx)
{
	u8 fullPath[4];
	int x;
	assert(fs != NULL && path != NULL && file_data != NULL);
	mscfs_lookup_path(fs, path, pathlen, fullPath, 0);
	
	/* Obtain file information while checking if it exists */
	mscfs_check_cache(fs);
	if(idx) *idx = -1;
	for(x = 0; x < fs->cache.size; x++) {
		u8 *objectId;
		*file_data = &fs->cache.array[x];
		objectId = (*file_data)->objectId;
		if(0 == memcmp(objectId, fullPath, 4)) {
			if(idx) *idx = x;
			break;
		}
		*file_data = NULL;
	}
	if(*file_data == NULL && (0 == memcmp("\x3F\x00\x00\x00", fullPath, 4) || 0 == memcmp("\x3F\x00\x3F\x00", fullPath, 4 ))) {
		static mscfs_file_t ROOT_FILE;
		ROOT_FILE.ef = 0;
		ROOT_FILE.size = 0;
		/* Faked Root ID */
		ROOT_FILE.objectId[0] = 0x3F;
		ROOT_FILE.objectId[1] = 0x00;
		ROOT_FILE.objectId[2] = 0x3F;
		ROOT_FILE.objectId[3] = 0x00;
		
		ROOT_FILE.read = 0;
		ROOT_FILE.write = 0x02; /* User Pin access */
		ROOT_FILE.delete = 0x02;
		
		*file_data = &ROOT_FILE;
		if(idx) *idx = -2;
	} else if(*file_data == NULL) {
		return MSCFS_FILE_NOT_FOUND;
	}
	
	return 0;
}
