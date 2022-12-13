/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_ZMOD_H
#define	_ZMOD_H

#include <sys/stdbool.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * zmod - RFC-1950-compatible decompression routines
 *
 * This file provides the public interfaces to zmod, an in-kernel RFC 1950
 * decompression library.  More information about the implementation of these
 * interfaces can be found in the usr/src/uts/common/zmod/ directory.
 */

#define	Z_OK		0
#define	Z_STREAM_END	1
#define	Z_NEED_DICT	2
#define	Z_ERRNO		(-1)
#define	Z_STREAM_ERROR	(-2)
#define	Z_DATA_ERROR	(-3)
#define	Z_MEM_ERROR	(-4)
#define	Z_BUF_ERROR	(-5)
#define	Z_VERSION_ERROR	(-6)

#define	Z_NO_COMPRESSION	0
#define	Z_BEST_SPEED		1
#define	Z_BEST_COMPRESSION	9
#define	Z_DEFAULT_COMPRESSION	(-1)

extern int z_uncompress(void *, size_t *, const void *, size_t);
extern int z_compress(void *, size_t *, const void *, size_t);
extern int z_compress_level(void *, size_t *, const void *, size_t, int);
extern const char *z_strerror(int);

/*
 * Stream decompression interface.
 *
 * As with the functions above, these functions return zlib error values, such
 * as Z_OK (see contrib/zlib/zlib.h).
 *
 * To use this interface, callers should first call z_uncompress_stream_init()
 * providing a pointer to a zmod_stream_t * which will be filled in on
 * successful initialisation.
 * Whenever additional data is available, pass it to the decompressor by
 * calling z_uncompress_stream() with the initialised handle and providing a
 * callback function. The callback function will be called zero or more times
 * with uncompressed data from the stream. z_uncompress_stream() can be called
 * multiple times to provide additional data and once it returns Z_STREAM_END,
 * decompression of the stream is complete. Callers should call
 * z_uncompress_stream_fini() when finished.
 */

/* opaque handle for stream decompression functions */
struct zmod_stream;
typedef struct zmod_stream zmod_stream_t;
extern int z_uncompress_stream_init(zmod_stream_t **);
extern void z_uncompress_stream_fini(zmod_stream_t *);
typedef bool (*z_uncompress_dataf)(void *, uint8_t *, size_t);
extern int z_uncompress_stream(zmod_stream_t *, uint8_t *, size_t,
    z_uncompress_dataf, void *);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZMOD_H */
