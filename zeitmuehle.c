#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <utime.h>
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


#ifndef LOG_LEVEL
#define LOG_LEVEL 2
#endif
#ifndef QUIET_LEVEL
#define QUIET_LEVEL 0
#endif

static int v = LOG_LEVEL;
static int q = QUIET_LEVEL;
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
	char datestring[256];
	{
		time_t now = time(NULL);
		strftime (datestring, sizeof(datestring), "%Y-%m-%d-%H%M%S", localtime(&now));
		strcat(dst_filename, "/");
		strcat(dst_filename, datestring);

		// Create the temp dir
		strcpy(current_timestamp, dst_filename);
		strcat(current_timestamp, ".inprogress");
	}

	// The last fully copied timestamp is the "Latest" symlink. Use that if exists
	{
		strncpy(previous_timestamp, argv[2], sizeof(previous_timestamp));
		strcat(previous_timestamp, "/Latest");

		struct stat sb_previous;
		if (stat(previous_timestamp, &sb_previous) != 0) {
			// Nullify the timestamp if no stat
			previous_timestamp[0] = 0;
		}
	}

	// mkpath dst_filename
	{
		mkdir(current_timestamp, 0755);
	}

	int flags = 0;
	int max_depth = 10;
	flags |= FTW_PHYS;
	int ret = nftw(src_filename, copy_if_needed, max_depth, flags);

	if (! ret) {
		// Everything was successful, rename the working dir
		rename(current_timestamp, dst_filename);

		// And update the "Latest" symlink
		strcpy(current_timestamp, argv[2]);
		strcat(current_timestamp, "/Latest");

		unlink(current_timestamp);
		symlink(datestring, current_timestamp);
	}

	return 0;
}

#define BUFFER_SIZE (1 * 1024  * 1024)
static char buffer[BUFFER_SIZE];


int mkdir_dst(const char *fpath, const struct stat *sb)
{
	INFO(printf("mkdir_dst(%s, %s/%s)\n", fpath, current_timestamp, fpath));

	// Test if we should create current dir "."
	if (strcmp(fpath, ".") == 0) return 0;

	sprintf(dst_fpath, "%s/%s", current_timestamp, fpath);
	return mkdir(dst_fpath, sb->st_mode);
}

/** Only copy regular file
 * --> Any character special file, block special file, FIFO or SOCKET is ignored.
 */
int copy_file(const char *fpath, const struct stat *sb)
{
	INFO(printf("copy_file(%s, %s/%s)\n", fpath, current_timestamp, fpath));
	if (! S_ISREG(sb->st_mode)) {
		WARN(printf("%s is ignored as it's not a regular file\n", fpath));
		// 0 == continue
		return 0;
	}

	sprintf(dst_fpath, "%s/%s", current_timestamp, fpath);

	// Check if previous file is same than src
	if (previous_timestamp[0]) {
		char dst_fpath_previous[PATH_MAX];
		sprintf(dst_fpath_previous, "%s/%s", previous_timestamp, fpath);

		struct stat sb_previous;
		stat(dst_fpath_previous, &sb_previous);

		int same_uid = (sb_previous.st_uid == sb->st_uid);
		int same_gid = (sb_previous.st_gid == sb->st_gid);
		int same_size = (sb_previous.st_size == sb->st_size);
		int same_mode = (sb_previous.st_mode == sb->st_mode);
		int same_mtime = (sb_previous.st_mtime == sb->st_mtime);

		INFO(printf("same_uid:%d,same_gid:%d,same_size:%d,same_mode:%d,same_mtime:%d\n", same_uid, same_gid, same_size, same_mode, same_mtime));
		if (same_uid && same_gid && same_size && same_mode && same_mtime) {
			// The files are the same, hardlink instead of copy
			INFO(printf("link(%s, %s)\n", dst_fpath_previous, current_timestamp));
			link(dst_fpath_previous, dst_fpath);
			return 0;
		}
	}

	int src = open(fpath, O_RDONLY, 0);
	int dst = open(dst_fpath, O_WRONLY | O_CREAT, 0644);

	if (dst < 0) return 1;

#ifdef HAVE_POSIX_FADVISE
	// Add some hints for the OS
	off_t window_start = 0;
	posix_fadvise(src, window_start, window_start + BUFFER_SIZE, POSIX_FADV_SEQUENTIAL | POSIX_FADV_WILLNEED);
#endif

	size_t size;
	while ((size = read(src, buffer, BUFFER_SIZE)) > 0) {
#ifdef HAVE_POSIX_FADVISE
		// We don't need to keep the buffers on src
		posix_fadvise(src, window_start, window_start + size, POSIX_FADV_DONTNEED);

		// Hint the kernel that we'll soon need the next window
		off_t window_end = window_start + size;
		posix_fadvise(src, window_end, window_end + BUFFER_SIZE, POSIX_FADV_SEQUENTIAL | POSIX_FADV_WILLNEED);
#endif

		// assume one can write the whole buffer at once
		write(dst, buffer, size);
#ifdef HAVE_POSIX_FADVISE

		// We don't need to keep the buffers on dst
		posix_fadvise(dst, window_start, window_end, POSIX_FADV_DONTNEED);

		// move the window
		window_start = window_end;
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

	// same time
	struct utimbuf tb;
	tb.actime = sb->st_atime;
	tb.modtime = sb->st_mtime;
	utime(dst_fpath, &tb);

	return 0;
}

int copy_link(const char *fpath, const struct stat *sb)
{
	INFO(printf("copy_link(%s, %s/%s)\n", fpath, current_timestamp, fpath));
	sprintf(dst_fpath, "%s/%s", current_timestamp, fpath);

	ssize_t buf_size = readlink(fpath, buffer, BUFFER_SIZE-1);
	buffer[buf_size] = 0; // Null termination, as readlink won't do it

	return symlink(buffer, dst_fpath);
}
