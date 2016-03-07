#include <unistd.h>
#include <fcntl.h>

void copy(int src, int dst);

int main(int argc, char **argv)
{
	char* src_filename = argv[1];
	char* dst_filename = argv[2];

	int src = open(src_filename, O_RDONLY, 0);
	int dst = open(dst_filename, O_WRONLY | O_CREAT, 0644);

	copy(src, dst);

	close(src);
	close(dst);
}

#define BUFFER_SIZE (1 * 1024  * 1024)
static char buffer[BUFFER_SIZE];

void copy(int src, int dst) {

	size_t size;
	while ((size = read(src, buffer, BUFFER_SIZE)) > 0) {
		// assume one can write the whole buffer at once
		write(dst, buffer, size);
	}
}
