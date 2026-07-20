
#include <assert.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/unistd.h>
#include "fs.h"
#include "util.h"

static const char *_suffixes[] = {
	"hod.dem",
	"setup.dat",
	"setup.dax",
	".paf",
	"_hod.lvl",
	"_hod.sss",
	"_hod.mst",
	0
};

static bool matchGameData(const char *path) {
	const int len = strlen(path);
	for (int i = 0; _suffixes[i]; ++i) {
		const int suffixLen = strlen(_suffixes[i]);
		if (len >= suffixLen && strcasecmp(path + len - suffixLen, _suffixes[i]) == 0) {
			return true;
		}
	}
	return false;
}

struct FileSystem_POSIX: FileSystem {

	const char *_dataPath;
	const char *_savePath;
	int _filesCount;
	char **_filesList;

	FileSystem_POSIX(const char *dataPath, const char *savePath);
	virtual ~FileSystem_POSIX();

	virtual FILE *openAssetFile(const char *filename);
	virtual FILE *openSaveFile(const char *filename, bool write);
	virtual int closeFile(FILE *);

	void addFilePath(const char *path);
	void listFiles(const char *dir);
};

FileSystem_POSIX::FileSystem_POSIX(const char *dataPath, const char *savePath)
	: _dataPath(dataPath), _savePath(savePath), _filesCount(0), _filesList(0) {
	listFiles(dataPath);
}

FileSystem_POSIX::~FileSystem_POSIX() {
	for (int i = 0; i < _filesCount; ++i) {
		free(_filesList[i]);
	}
	free(_filesList);
}

FILE *FileSystem_POSIX::openAssetFile(const char *name) {
	FILE *fp = 0;
	for (int i = 0; i < _filesCount; ++i) {
		const char *p = strrchr(_filesList[i], '/');
		assert(p);
		if (strcasecmp(name, p + 1) == 0) {
			fp = fopen(_filesList[i], "rb");
			break;
		}
	}
	return fp;
}

FILE *FileSystem_POSIX::openSaveFile(const char *filename, bool write) {
	char path[MAXPATHLEN];
	snprintf(path, sizeof(path), "%s/%s", _savePath, filename);
	return fopen(path, write ? "wb" : "rb");
}

int FileSystem_POSIX::closeFile(FILE *fp) {
	const int err = ferror(fp);
	fclose(fp);
	return err;
}

void FileSystem_POSIX::addFilePath(const char *path) {
	_filesList = (char **)realloc(_filesList, (_filesCount + 1) * sizeof(char *));
	if (_filesList) {
		_filesList[_filesCount] = strdup(path);
		++_filesCount;
	}
}

void FileSystem_POSIX::listFiles(const char *dir) {
	DIR *d = opendir(dir);
	if (d) {
		dirent *de;
		while ((de = readdir(d)) != NULL) {
			if (de->d_name[0] == '.') {
				continue;
			}
			char filePath[MAXPATHLEN];
			snprintf(filePath, sizeof(filePath), "%s/%s", dir, de->d_name);
			struct stat st;
			if (stat(filePath, &st) == 0) {
				if (S_ISDIR(st.st_mode)) {
					listFiles(filePath);
				} else if (matchGameData(filePath)) {
					addFilePath(filePath);
				}
			}
		}
		closedir(d);
	}
}

struct FileSystem *FileSystem_POSIX_create(const char *dataPath, const char *savePath) {
	return new FileSystem_POSIX(dataPath, savePath);
}
