#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

void mkdir_dst(const char *fpath, const struct stat *sb);
void copy_file(const char *fpath, const struct stat *sb);
void copy_link(const char *fpath, const struct stat *sb);

static int copy_if_needed(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
	switch (tflag) {
		case FTW_D:
			mkdir_dst(fpath, sb);
			break;

		case FTW_F:
			copy_file(fpath, sb);
			break;

		case FTW_SL:
			copy_link(fpath, sb);
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
		mkdir(dst_filename, 0644);
	}

	int flags = 0;
	int max_depth = 10;
	flags |= FTW_PHYS;
	nftw(src_filename, copy_if_needed, max_depth, flags);

	return 0;
}

#define BUFFER_SIZE (1 * 1024  * 1024)
static char buffer[BUFFER_SIZE];


void mkdir_dst(const char *fpath, const struct stat *sb)
{
	INFO(printf("mkdir_dst(%s, %s/%s)\n", fpath, dst_filename, fpath));
	sprintf(dst_fpath, "%s/%s", dst_filename, fpath);
	mkdir(dst_fpath, sb->st_mode);
}

/** Only copy regular file
 * --> Any character special file, block special file, FIFO or SOCKET is ignored.
 */
void copy_file(const char *fpath, const struct stat *sb)
{
	INFO(printf("copy_file(%s, %s/%s)\n", fpath, dst_filename, fpath));
	if (! S_ISREG(sb->st_mode)) {
		WARN(printf("%s is ignored as it's not a regular file\n", fpath));
		return;
	}

	sprintf(dst_fpath, "%s/%s", dst_filename, fpath);

	int src = open(fpath, O_RDONLY, 0);
	int dst = open(dst_fpath, O_WRONLY | O_CREAT, 0644);

	size_t size;
	while ((size = read(src, buffer, BUFFER_SIZE)) > 0) {
		// assume one can write the whole buffer at once
		write(dst, buffer, size);
	}

	close(src);
	close(dst);
}

void copy_link(const char *fpath, const struct stat *sb)
{
	INFO(printf("copy_link(%s, %s/%s)\n", fpath, dst_filename, fpath));
	sprintf(dst_fpath, "%s/%s", dst_filename, fpath);

	ssize_t buf_size = readlink(fpath, buffer, BUFFER_SIZE-1);
	buffer[buf_size] = 0; // Null termination, as readlink won't do it

	symlink(buffer, dst_fpath);
}
