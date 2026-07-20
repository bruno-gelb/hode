
#ifndef FS_H__
#define FS_H__

#include <stdio.h>

struct FileSystem {

	virtual ~FileSystem() {}

	virtual FILE *openAssetFile(const char *filename) = 0;
	virtual FILE *openSaveFile(const char *filename, bool write) = 0;
	virtual int closeFile(FILE *) = 0;
};

struct FileSystem *FileSystem_Android_create(const char *dataPath, const char *savePath);
struct FileSystem *FileSystem_POSIX_create(const char *dataPath, const char *savePath);
struct FileSystem *FileSystem_ZipInstaller_create(const char *dataPath, const char *savePath);

#endif // FS_H__
