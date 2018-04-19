#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <arpa/inet.h>
#include "util.h"

#define CBFS_HEADER_MAGIC  0x4F524243
#define CBFS_HEADER_VERSION1 0x31313131
#define CBFS_HEADER_VERSION2 0x31313132
#define CBFS_HEADER_VERSION  CBFS_HEADER_VERSION2

int verbose = 0;

static const struct option long_options[] = {
	{ "verbose",		0, NULL, 'v' },
	{ "read",		1, NULL, 'r' },
	{ "list",		0, NULL, 'l' },
	{ "type",		1, NULL, 't' },
	{ "help",		0, NULL, 'h' },
	{ NULL,			0, NULL, 0 },
};


static const char usage[] =
"Usage: sudo cbfs [options]\n"
"\n"
"    -h | -? | --help       This help\n"
"    -v | --verbose         Increase verbosity\n"
"    -r | --read file       Export a CBFS file to stdout\n"
"    -l | --list            List the names of CBFS files\n"
"    -t | --type 50         Filter to specific CBFS file type (hex)\n"
"\n";

struct cbfs_header {
	uint32_t magic;
	uint32_t version;
	uint32_t romsize;
	uint32_t bootblocksize;
	uint32_t align; /* hard coded to 64 byte */
	uint32_t offset;
	uint32_t architecture;  /* Version 2 */
	uint32_t pad[1];
};

#define CBFS_FILE_MAGIC "LARCHIVE"

struct cbfs_file {
	uint8_t magic[8];
	/* length of file data */
	uint32_t len;
	uint32_t type;
	/* offset to struct cbfs_file_attribute or 0 */
	uint32_t attributes_offset;
	/* length of header incl. variable data */
	uint32_t offset;
	char filename[];
};


int main(int argc, char** argv) {
	const char * const prog_name = argv[0];
	if (argc <= 1)
	{
		fprintf(stderr, "%s", usage);
		return EXIT_FAILURE;
	}

	int opt;
	int do_read = 0;
	int do_list = 0;
	int do_type = 0;
	uint32_t cbfs_file_type = 0;
	const char * filename = NULL;
	while ((opt = getopt_long(argc, argv, "h?vlr:t:",
		long_options, NULL)) != -1)
	{
		switch(opt)
		{
		case 'v':
			verbose++;
			break;
		case 'l':
			do_list = 1;
			break;
		case 'r':
			do_read = 1;
			filename = optarg;
			break;
		case 't':
			do_type = 1;
			cbfs_file_type = strtoul(optarg, NULL, 16);
			break;
		case '?': case 'h':
			fprintf(stderr, "%s", usage);
			return EXIT_SUCCESS;
		default:
			fprintf(stderr, "%s", usage);
			return EXIT_FAILURE;
		}
	}

	if (!do_list && !do_read) {
		fprintf(stderr, "%s", usage);
		return EXIT_FAILURE;
	}

	argc -= optind;
	argv += optind;
	if (argc != 0)
	{
		fprintf(stderr, "%s: Excess arguments?\n", prog_name);
		return EXIT_FAILURE;
	}

	uint64_t end = 0x100000000;

	if (verbose) {
		fprintf(stderr, "Seeking to %lx\n", end-4);
	}
	int32_t header_delta;
	copy_physical(end-4, sizeof(header_delta), &header_delta);

	if (verbose) {
		fprintf(stderr, "Header Offset: %d\n", header_delta);
	}

	uint64_t header_off = end + header_delta;

	if (verbose) {
		fprintf(stderr, "Seeking to %lx\n", header_off);
	}
	struct cbfs_header header;
	copy_physical(header_off, sizeof(header), &header);

	header.magic = ntohl(header.magic);
	header.version = ntohl(header.version);
	header.romsize = ntohl(header.romsize);
	header.bootblocksize = ntohl(header.bootblocksize);
	header.align = ntohl(header.align);
	header.offset = ntohl(header.offset);
	header.architecture = ntohl(header.architecture);

	if (verbose) {
		fprintf(stderr, "Header magic          : %x\n", header.magic);
		fprintf(stderr, "Header version        : %x\n", header.version);
		fprintf(stderr, "Header ROM size       : %x\n", header.romsize);
		fprintf(stderr, "Header boot block size: %x\n", header.bootblocksize);
		fprintf(stderr, "Header align          : %x\n", header.align);
		fprintf(stderr, "Header offset         : %x\n", header.offset);
		fprintf(stderr, "Header arch           : %x\n", header.architecture);
	}

	if (header.magic != CBFS_HEADER_MAGIC) {
		fprintf(stderr, "Failed to find valid header\n");
		return EXIT_FAILURE;
	}

	uint32_t align = header.align;

	// loop through files
	uint64_t off = end - ((uint64_t) header.romsize) +
		((uint64_t) header.offset);
	void *rom = map_physical(off, end - off);
	while (off < end) {
		if (verbose) {
			fprintf(stderr, "Potential CBFS File Offset: %lx\n", off);
		}
		struct cbfs_file file;
		memcpy(&file, rom, sizeof(file));

		file.len = ntohl(file.len);
		file.type = ntohl(file.type);
		file.attributes_offset = ntohl(file.attributes_offset);
		file.offset = ntohl(file.offset);

		if (verbose) {
			fprintf(stderr, "File magic             : %.8s\n", file.magic);
			fprintf(stderr, "File len               : %x\n", file.len);
			fprintf(stderr, "File type              : %x\n", file.type);
			fprintf(stderr, "File attributes_offset : %x\n", file.attributes_offset);
			fprintf(stderr, "File offset            : %x\n", file.offset);
		}

		if (strncmp((char *)file.magic, CBFS_FILE_MAGIC, 8) != 0) {
			break;
		}

		size_t name_size = file.offset - sizeof(file);
		char *name = (char *)rom + sizeof(file);

		if (verbose) {
			fprintf(stderr, "File name              : '%s'\n", name);
		}

		if (do_list &&
			(!do_type || (do_type && file.type == cbfs_file_type))) {
			printf("%s\n", name);
		}

		if (do_read &&
			(!do_type || (do_type && file.type == cbfs_file_type)) &&
			strncmp(name, filename, name_size) == 0)
		{
			uint64_t file_off = off + file.offset;
			if (verbose) {
				fprintf(stderr, "Seeking to %lx\n-------- Start Data\n", file_off);
			}

			if (file_off + file.len > end) {
				fprintf(stderr, "File offset/length extends beyond ROM");
				return EXIT_FAILURE;
			}

			char *file_data = (char *) rom + file.offset;
			for (size_t offset = 0 ; offset < file.len ; ) {
				const ssize_t rc = write(
					STDOUT_FILENO,
					file_data + offset,
					file.len - offset
				);

				if (rc <= 0) {
					fprintf(stderr, "Failed to write file to stdout: %s\n",
						strerror(errno));
					return EXIT_FAILURE;
				}

				offset += rc;
			}

			if (verbose) {
				fprintf(stderr, "\n-------- End Data\n");
			}

			do_read++;
			break;
		}

		uint64_t inc = (align + (file.offset + file.len) - 1) & (~(align-1));
		off += inc;
		rom += inc;
		if (verbose) {
			fprintf(stderr, "File Off+Len    : %x\n", file.offset + file.len);
			fprintf(stderr, "Align           : %x\n", align);
			fprintf(stderr, "Inc             : %lx\n", inc);
			fprintf(stderr, "Next file off   : %lx\n", off);
		}
	}

	if (do_read == 1) {
		fprintf(stderr, "Failed to find CBFS file named '%s'\n", filename);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
