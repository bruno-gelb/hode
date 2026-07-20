
#include <sys/param.h>
#include <sys/unistd.h>
#include <zlib.h>
#include "fileio.h"
#include "fs.h"
#include "util.h"

static uint16_t freadUint16LE(FILE *fp) {
	uint8_t buf[2];
	const int count = fread(buf, sizeof(buf), 1, fp);
	return count == 1 ? READ_LE_UINT16(buf) : 0;
}

static uint32_t freadUint32LE(FILE *fp) {
	uint8_t buf[4];
	const int count = fread(buf, sizeof(buf), 1, fp);
	return count == 1 ? READ_LE_UINT32(buf) : 0;
}

struct ZipInstallerEntry {
	char *name;
	uint32_t local_offset;

	static int compare(const void *a, const void *b);
};

int ZipInstallerEntry::compare(const void *a, const void *b) {
	return strcasecmp(((const ZipInstallerEntry *)a)->name, ((const ZipInstallerEntry *)b)->name);
}

struct ZipInstallerEntryData {
	const ZipInstallerEntry *ze;

	uint32_t size, offset;
	uint8_t *buffer;

	static ssize_t read(void *cookie, char *buf, size_t size);
	static int seek(void *cookie, off_t *offset, int whence);
	static int close(void *cookie);
};

ssize_t ZipInstallerEntryData::read(void *cookie, char *buf, size_t size) {
	ZipInstallerEntryData *zed = (ZipInstallerEntryData *)cookie;

	const size_t remain = zed->size - zed->offset;
	const int count = MIN(remain, size);
	if (count > 0) {
		memcpy(buf, zed->buffer + zed->offset, count);
		zed->offset += count;
	}
	return count;
}

int ZipInstallerEntryData::seek(void *cookie, off_t *offset, int whence) {
	ZipInstallerEntryData *zed = (ZipInstallerEntryData *)cookie;
	off_t seekOffset = zed->offset;
	switch (whence) {
	case SEEK_SET:
		seekOffset = *offset;
		break;
	case SEEK_CUR:
		seekOffset += *offset;
		break;
	case SEEK_END:
		seekOffset = zed->size + *offset;
		break;
	}
	if (seekOffset < 0 || seekOffset > zed->size) {
		return -1;
	}
	zed->offset = *offset = seekOffset;
	return 0;
}

int ZipInstallerEntryData::close(void *cookie) {
	ZipInstallerEntryData *zed = (ZipInstallerEntryData *)cookie;
	free(zed);
	return 0;
}

static const char *kSig = "Windows Self-Installing Executable";

struct FileSystem_ZipInstaller: FileSystem {

	FileSystem_ZipInstaller(const char *dataPath, const char *savePath);
	~FileSystem_ZipInstaller();

	FILE *openAssetFile(const char *filename);
	FILE *openSaveFile(const char *filename, bool write);
	int closeFile(FILE *);

	static bool probe(FILE *fp);

private:
	void readHeader();
	const ZipInstallerEntry *find(const char *name) const;

	FILE *_fp;
	const char *_savePath;
	ZipInstallerEntry *_entries;
	int _entriesCount;
};

FileSystem_ZipInstaller::FileSystem_ZipInstaller(const char *dataPath, const char *savePath)
	: _savePath(savePath), _entries(0), _entriesCount(0) {
	_fp = fopen(dataPath, "rb");
	assert(_fp);
	readHeader();
}

FileSystem_ZipInstaller::~FileSystem_ZipInstaller() {
	fclose(_fp);
	_fp = 0;
	free(_entries);
	_entries = 0;
}

FILE *FileSystem_ZipInstaller::openAssetFile(const char *filename) {
	debug(kDebug_FS, "openAssetFile filename:%s", filename);
	const ZipInstallerEntry *entry = find(filename);
	if (!entry) {
		return 0;
	}
	fseek(_fp, entry->local_offset, SEEK_SET);
	/* local file header */
	const uint32_t signature = freadUint32LE(_fp);
	assert(signature == 0x04034B50);
	freadUint16LE(_fp); /* version needed for extraction */
	freadUint16LE(_fp); /* flags */
	const uint16_t compression = freadUint16LE(_fp);
	if (compression != 0 && compression != 8) {
		warning("Unhandled zip installer compression %d", compression);
		return 0;
	}
	freadUint16LE(_fp); /* last modification file time */
	freadUint16LE(_fp); /* last modification file date */
	freadUint32LE(_fp); /* crc */
	const int compressed_size = freadUint32LE(_fp);
	const int size = freadUint32LE(_fp);
	const uint16_t name_length = freadUint16LE(_fp);
	const uint16_t extra_length = freadUint16LE(_fp);
	if (name_length != 0) {
		fseek(_fp, name_length, SEEK_CUR);
	}
	if (extra_length != 0) {
		fseek(_fp, extra_length, SEEK_CUR);
	}
	ZipInstallerEntryData *data = (ZipInstallerEntryData *)calloc(1, sizeof(ZipInstallerEntryData));
	if (!data) {
		return 0;
	}
	uint8_t *tmp = (uint8_t *)malloc(compressed_size);
	if (!tmp) {
		error("Failed to allocate %d bytes for compressed zip installer data");
	} else {
		const int count = fread(tmp, 1, compressed_size, _fp);
		if (count != compressed_size) {
			error("Failed to read %d bytes (ret %d)", compressed_size, count);
		} else if (compression == 8) {
			uint8_t *dst = (uint8_t *)malloc(size);
			if (!dst) {
				error("Failed to allocate %d bytes for decompressed zip installer data", size);
			} else {
				z_stream zs;
				memset(&zs, 0, sizeof(zs));
				int ret = inflateInit2(&zs, -MAX_WBITS);
				zs.avail_in = compressed_size;
				zs.next_in = tmp;
				zs.avail_out = size;
				zs.next_out = dst;
				ret = inflate(&zs, Z_NO_FLUSH);
				if (ret != Z_STREAM_END) {
					warning("inflate() returned %d expected Z_STREAM_END", ret);
				}
				inflateEnd(&zs);
				data->buffer = dst;
				free(tmp);
			}
		} else {
			assert(compression == 0);
			data->buffer = tmp;
		}
	}
	data->offset = 0;
	data->size = size;
	data->ze = entry;

	cookie_io_functions_t funcs;
	memset(&funcs, 0, sizeof(funcs));
	funcs.read  = ZipInstallerEntryData::read;
	funcs.seek  = ZipInstallerEntryData::seek;
	funcs.close = ZipInstallerEntryData::close;
	return fopencookie(data, "r", funcs);
}

FILE *FileSystem_ZipInstaller::openSaveFile(const char *filename, bool write) {
	char path[MAXPATHLEN];
	snprintf(path, sizeof(path), "%s/%s", _savePath, filename);
	return fopen(path, write ? "wb" : "rb");
}

int FileSystem_ZipInstaller::closeFile(FILE *fp) {
	const int err = ferror(fp);
	fclose(fp);
	return err;
}

bool FileSystem_ZipInstaller::probe(FILE *fp) {
	fseek(fp, -36, SEEK_END);
	uint8_t buf[36];
	const int count = fread(buf, sizeof(buf), 1, fp);
	return count == 1 && READ_LE_UINT16(buf) == 34 && memcmp(buf + 2, kSig, 34) == 0;
}

void FileSystem_ZipInstaller::readHeader() {
	const int EOD_SIZE = 22 + strlen(kSig);
	fseek(_fp, -EOD_SIZE, SEEK_END);
	const uint32_t eod_signature = freadUint32LE(_fp);
	assert(eod_signature == 0x06054B50);
	freadUint16LE(_fp); /* current disk number */
	freadUint16LE(_fp); /* central directory disk number */
	_entriesCount = freadUint16LE(_fp);
	const uint16_t totalEntriesCount = freadUint16LE(_fp);
	assert(totalEntriesCount == _entriesCount);
	freadUint32LE(_fp); /* directory size */
	const uint32_t directoryOffset = freadUint32LE(_fp); /* directory offset */
	fseek(_fp, directoryOffset, SEEK_SET);
	_entries = (ZipInstallerEntry *)calloc(_entriesCount, sizeof(ZipInstallerEntry));
	if (!_entries) {
		warning("Failed to allocate %d zip_installer entries", _entriesCount);
		return;
	}
	for (int i = 0; i < _entriesCount; ++i) {
		ZipInstallerEntry *entry = &_entries[i];
		/* central directory file header */
		const uint32_t signature = freadUint32LE(_fp);
		assert(signature == 0x02014B50);
		freadUint16LE(_fp); /* version */
		freadUint16LE(_fp); /* version needed for extraction */
		freadUint16LE(_fp); /* flags */
		freadUint16LE(_fp); /* compression */
		freadUint16LE(_fp); /* last modification file time */
		freadUint16LE(_fp); /* last modification file date */
		freadUint32LE(_fp); /* crc */
		freadUint32LE(_fp); /* size */
		freadUint32LE(_fp); /* compressed size */
		const uint16_t name_length = freadUint16LE(_fp);
		const uint16_t extra_length = freadUint16LE(_fp);
		const uint16_t comment_length = freadUint16LE(_fp);
		freadUint16LE(_fp); /* disk number start */
		freadUint16LE(_fp); /* internal attributes */
		freadUint32LE(_fp); /* external attributes */
		entry->local_offset = freadUint32LE(_fp); /* offset to local header */
		if (name_length != 0) {
			entry->name = (char *)malloc(name_length + 1);
			if (entry->name) {
				fread(entry->name, 1, name_length, _fp);
				entry->name[name_length] = 0;
			} else {
				fseek(_fp, name_length, SEEK_CUR);
			}
		}
		if (extra_length != 0) {
			fseek(_fp, extra_length, SEEK_CUR);
		}
		if (comment_length != 0) {
			fseek(_fp, comment_length, SEEK_CUR);
		}
	}
	qsort(_entries, _entriesCount, sizeof(struct ZipInstallerEntry), ZipInstallerEntry::compare);
}

const ZipInstallerEntry *FileSystem_ZipInstaller::find(const char *name) const {
	ZipInstallerEntry e;
	e.name = (char *)name;
	return (ZipInstallerEntry *)bsearch(&e, _entries, _entriesCount, sizeof(ZipInstallerEntry), ZipInstallerEntry::compare);
}

struct FileSystem *FileSystem_ZipInstaller_create(const char *dataPath, const char *savePath) {
	FileSystem *fs = 0;
	FILE *fp = fopen(dataPath, "rb");
	if (fp) {
		if (FileSystem_ZipInstaller::probe(fp)) {
			fs = new FileSystem_ZipInstaller(dataPath, savePath);
		}
		fclose(fp);
	}
	return fs;
}
