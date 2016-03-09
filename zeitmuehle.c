#ifdef __linux_
#define _XOPEN_SOURCE 500
#endif


#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <ftw.h>

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


char* src_filename = ".";
char* dst_filename = "/tmp";

int main(int argc, char **argv)
{
	if (argc > 1) src_filename = argv[1];
	if (argc > 2) dst_filename = argv[2];

	int flags = 0;
	int max_depth = 10;
	flags |= FTW_PHYS;
	nftw(src_filename, copy_if_needed, max_depth, flags);
}

#define BUFFER_SIZE (1 * 1024  * 1024)
static char buffer[BUFFER_SIZE];
#define PATH_MAX        4096
static char dst_fpath[PATH_MAX];

void mkdir_dst(const char *fpath, const struct stat *sb)
{
	printf("mkdir_dst(%s, %s/%s)\n", fpath, dst_filename, fpath);
	sprintf(dst_fpath, "%s/%s", dst_filename, fpath);
	mkdir(dst_fpath, sb->st_mode);
}

void copy_file(const char *fpath, const struct stat *sb)
{
	printf("copy_file(%s, %s/%s)\n", fpath, dst_filename, fpath);
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
	printf("copy_link(%s, %s/%s)\n", fpath, dst_filename, fpath);
	sprintf(dst_fpath, "%s/%s", dst_filename, fpath);

	symlink(dst_fpath, fpath);
}
