#include "lib/bookid.h"

#include "core/fnv.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>

int bookid_file(const char *path, uint64_t *out_id, uint64_t *out_size)
{
	FILE *f = fopen(path, "rb");
	unsigned char *buf;
	uint64_t size;
	size_t plen;
	int rc = 0;

	if (!f)
		return -1;
	if (fseeko(f, 0, SEEK_END) != 0 || ftello(f) < 0) {
		fclose(f);
		return -1;
	}
	size = (uint64_t)ftello(f);
	plen = bookid_part_len(size);
	buf = (unsigned char *)malloc(2 * (plen ? plen : 1));
	if (!buf) {
		fclose(f);
		return -2;
	}
	if (plen) {
		if (fseeko(f, 0, SEEK_SET) != 0 || fread(buf, 1, plen, f) != plen)
			rc = -2;
		else if (fseeko(f, (off_t)bookid_tail_offset(size), SEEK_SET) != 0 ||
		         fread(buf + plen, 1, plen, f) != plen)
			rc = -2;
	}
	fclose(f);
	if (rc == 0) {
		if (out_id)
			*out_id = bookid_from_parts(size, buf, plen, buf + plen, plen);
		if (out_size)
			*out_size = size;
	}
	free(buf);
	return rc;
}

uint64_t bookid_folder(const char *const *names, const uint64_t *sizes, int n)
{
	static const char tag[] = "folder"; /* hashed with its NUL */
	fnv64_t c;

	fnv64_init(&c);
	fnv64_update(&c, tag, sizeof tag);
	for (int i = 0; i < n; i++) {
		unsigned char le[8];
		fnv64_update(&c, names[i], strlen(names[i]) + 1);
		for (int k = 0; k < 8; k++)
			le[k] = (unsigned char)(sizes[i] >> (8 * k));
		fnv64_update(&c, le, sizeof le);
	}
	return fnv64_final(&c);
}
