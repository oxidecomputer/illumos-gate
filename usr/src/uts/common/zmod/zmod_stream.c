/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2022 Oxide Computer Company
 */

/*
 * A simple streaming interface for inflating a stream of data.
 */

#include <sys/stdbool.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>
#include <sys/kobj.h>
#include <sys/kobj_impl.h>
#include <sys/zmod.h>

#include <zlib.h>
#include <zutil.h>

/*
 * The zlib manual recommends that the output buffer should be on the
 * order of 128KiB or 256KiB.
 */
#define	ZS_OUTPUT_BUFFER_SIZE	0x20000

struct zmod_stream {
	z_stream	zsi_stream;
	bool		zsi_initdone;
	uint8_t		zsi_out[ZS_OUTPUT_BUFFER_SIZE];
};

int
z_uncompress_stream_init(zmod_stream_t **hdl)
{
	zmod_stream_t *zs;

	zs = kobj_zalloc(sizeof (struct zmod_stream), KM_NOWAIT|KM_TMP);

	if (zs == NULL)
		return (Z_MEM_ERROR);

	zs->zsi_stream.zalloc = zcalloc;
	zs->zsi_stream.zfree = zcfree;
	zs->zsi_stream.opaque = Z_NULL;

	*hdl = zs;

	return (Z_OK);
}

void
z_uncompress_stream_fini(zmod_stream_t *zs)
{
	if (zs != NULL) {
		if (zs->zsi_initdone)
			(void) inflateEnd(&zs->zsi_stream);
		kobj_free(zs, sizeof (struct zmod_stream));
	}
}

int
z_uncompress_stream(zmod_stream_t *zs, uint8_t *in, size_t inl,
    z_uncompress_dataf cb, void *arg)
{
	int ret;

	zs->zsi_stream.next_in = in;
	zs->zsi_stream.avail_in = inl;

	/*
	 * zlib initialisation is deferred until we receive the first block of
	 * data. According to the zlib manual, it is not safe to call one of
	 * the inflateInit*() functions until there is some data available:
	 * "The fields next_in, avail_in ... must be initialized before by the
	 *  caller."
	 * although this is technically not necessary as of zlib 1.2.13:
	 * "The current implementation of inflateInit() does not process any
	 *  header information - that is deferred until inflate() is called."
	 */
	if (!zs->zsi_initdone) {
		/*
		 * Call inflateInit2() specifying a window size of DEF_WBITS
		 * with the 6th bit set to indicate that the compression format
		 * type (zlib or gzip) should be automatically detected.
		 */
		ret = inflateInit2(&zs->zsi_stream, DEF_WBITS | 0x20);
		if (ret != Z_OK) {
			kobj_free(zs, sizeof (zmod_stream_t));
			return (ret);
		}
		zs->zsi_initdone = true;
	}

	/*
	 * Call inflate() repeatedly and pass the output to the callback until
	 * there is no more, indicated by inflate() not filling the output
	 * buffer. We cannot do the more obvious thing of looping until
	 * avail_in is zero since the deflate stream may end before the data
	 * does.
	 */
	do {
		size_t len;

		zs->zsi_stream.next_out = zs->zsi_out;
		zs->zsi_stream.avail_out = sizeof (zs->zsi_out);

		ret = inflate(&zs->zsi_stream, Z_NO_FLUSH);
		switch (ret) {
		case Z_OK:
		case Z_STREAM_END:
			break;
		default:
			return (ret);
		}

		len = sizeof (zs->zsi_out) - zs->zsi_stream.avail_out;
		if (!cb(arg, zs->zsi_out, len))
			return (Z_BUF_ERROR);
	} while (zs->zsi_stream.avail_out == 0);

	if (ret == Z_STREAM_END) {
		ret = inflateEnd(&zs->zsi_stream);
		zs->zsi_initdone = false;
		if (ret == Z_OK)
			return (Z_STREAM_END);
	}

	return (ret);
}
