#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

int mkdir_dst(const char *fpath, const struct stat *sb);
int copy_file(const char *fpath, const struct stat *sb);
int copy_link(const char *fpath, const struct stat *sb);

static int copy_if_needed(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
	switch (tflag) {
		case FTW_D:
			return mkdir_dst(fpath, sb);
			break;

		case FTW_F:
			return copy_file(fpath, sb);
			break;

		case FTW_SL:
			return copy_link(fpath, sb);
			break;

		case FTW_DNR:
		case FTW_NS:
		case FTW_DP:
		case FTW_SLN:
			// Should not happen : indicate failure
			return 1;
	}

	// continue
	return 0;
}


static int v = 2;
static int q = 0;
#define INFO(x)  if(v > 0) { x ; };
#define DEBUG(x) if(v > 1) { x ; };
#define WARN(x)  if(q == 0) { x ; };

#define PATH_MAX        4096
char dst_fpath[PATH_MAX];

char src_filename[PATH_MAX];
char dst_filename[PATH_MAX];

char current_timestamp[PATH_MAX];
char previous_timestamp[PATH_MAX];

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <src> <dst>\n", argv[0]);
		return 2;
	}

	strncpy(src_filename, argv[1], sizeof(src_filename));
	strncpy(dst_filename, argv[2], sizeof(dst_filename));

	// Appending the timestamp
	{
		time_t now = time(NULL);
		char datestring[256];
		strftime (datestring, sizeof(datestring), "%FT%T", localtime(&now));
		strcat(dst_filename, "/");
		strcat(dst_filename, datestring);
	}

	// mkpath dst_filename
	{
		mkdir(dst_filename, 0755);
	}

	int flags = 0;
	int max_depth = 10;
	flags |= FTW_PHYS;
	nftw(src_filename, copy_if_needed, max_depth, flags);

	return 0;
}

#define BUFFER_SIZE (1 * 1024  * 1024)
static char buffer[BUFFER_SIZE];


int mkdir_dst(const char *fpath, const struct stat *sb)
{
	INFO(printf("mkdir_dst(%s, %s/%s)\n", fpath, dst_filename, fpath));

	// Test if we should create current dir "."
	if (strcmp(fpath, ".") == 0) return 0;

	sprintf(dst_fpath, "%s/%s", dst_filename, fpath);
	return mkdir(dst_fpath, sb->st_mode);
}

/** Only copy regular file
 * --> Any character special file, block special file, FIFO or SOCKET is ignored.
 */
int copy_file(const char *fpath, const struct stat *sb)
{
	INFO(printf("copy_file(%s, %s/%s)\n", fpath, dst_filename, fpath));
	if (! S_ISREG(sb->st_mode)) {
		WARN(printf("%s is ignored as it's not a regular file\n", fpath));
		// 0 == continue
		return 0;
	}

	sprintf(dst_fpath, "%s/%s", dst_filename, fpath);

	int src = open(fpath, O_RDONLY, 0);
	int dst = open(dst_fpath, O_WRONLY | O_CREAT, 0644);

	if (dst < 0) return 1;

#if 0
	// Add some hints for the OS
	off_t START_FILE = 0;
	off_t ALL_FILE = 0;
	posix_fadvise(src, START_FILE, ALL_FILE, POSIX_FADV_SEQUENTIAL | POSIX_FADV_WILLNEED | POSIX_FADV_NOREUSE);
#endif

	size_t size;
	while ((size = read(src, buffer, BUFFER_SIZE)) > 0) {
		// assume one can write the whole buffer at once
		write(dst, buffer, size);

#if 0
		// We don't need to keep the buffers on dst
		posix_fadvise(dst, START_FILE, ALL_FILE, POSIX_FADV_DONTNEED);
#endif
	}

	// Copy the metadata
	{
		// same uid/gid
		fchown(dst, sb->st_uid, sb->st_gid);
		// same file perms
		fchmod(dst, sb->st_mode);
	}

	close(src);
	close(dst);

	return 0;
}

int copy_link(const char *fpath, const struct stat *sb)
{
	INFO(printf("copy_link(%s, %s/%s)\n", fpath, dst_filename, fpath));
	sprintf(dst_fpath, "%s/%s", dst_filename, fpath);

	ssize_t buf_size = readlink(fpath, buffer, BUFFER_SIZE-1);
	buffer[buf_size] = 0; // Null termination, as readlink won't do it

	return symlink(buffer, dst_fpath);
}
