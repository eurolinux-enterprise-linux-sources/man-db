/*
 * manconv_client.c: use manconv in a pipeline
 *
 * Copyright (C) 2007, 2008, 2010 Colin Watson.
 *
 * This file is part of man-db.
 *
 * man-db is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * man-db is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with man-db; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "manconfig.h"

#include "pipeline.h"
#include "decompress.h"

#ifdef SECURE_MAN_UID
#  include "idpriv.h"
#  include "security.h"
#endif /* SECURE_MAN_UID */

#include "manconv.h"
#include "manconv_client.h"

struct manconv_codes {
	char **from;
	char *to;
};

static void manconv_stdin (void *data)
{
	struct manconv_codes *codes = data;
	pipeline *p;

#ifdef SECURE_MAN_UID
	/* iconv_open may not work correctly in setuid processes; in GNU
	 * libc, gconv modules may be linked against other gconv modules and
	 * rely on RPATH $ORIGIN to load those modules from the correct
	 * path, but $ORIGIN is disabled in setuid processes.  It is
	 * impossible to reset libc's idea of setuidness without creating a
	 * whole new process image.  Therefore, if the calling process is
	 * setuid, we must drop privileges and execute manconv.
	 *
	 * If dropping privileges fails, fall through to the in-process
	 * code, as in some situations it may actually manage to work.
	 */
	if (running_setuid () && !idpriv_drop ()) {
		char **from_code;
		char *sources = NULL;
		pipecmd *cmd;

		for (from_code = codes->from; *from_code; ++from_code) {
			sources = appendstr (sources, *from_code, NULL);
			if (*(from_code + 1))
				sources = appendstr (sources, ":", NULL);
		}

		cmd = pipecmd_new_args (MANCONV, "-f", sources,
					"-t", codes->to, NULL);
		free (sources);

		if (quiet >= 2)
			pipecmd_arg (cmd, "-q");

		pipecmd_exec (cmd);
		/* never returns */
	}
#endif /* SECURE_MAN_UID */

	p = decompress_fdopen (dup (STDIN_FILENO));
	pipeline_start (p);
	manconv (p, codes->from, codes->to);
	pipeline_wait (p);
	pipeline_free (p);
}

static void free_manconv_codes (void *data)
{
	struct manconv_codes *codes = data;
	char **try_from;

	for (try_from = codes->from; *try_from; ++try_from)
		free (*try_from);
	free (codes->from);
	free (codes->to);
	free (codes);
}

void add_manconv (pipeline *p, const char *source, const char *target)
{
	struct manconv_codes *codes = xmalloc (sizeof *codes);
	char *name;
	pipecmd *cmd;

	if (STREQ (source, "UTF-8") && STREQ (target, "UTF-8"))
		return;

	/* informational only; no shell quoting concerns */
	name = appendstr (NULL, MANCONV, " -f ", NULL);
	if (STREQ (source, "UTF-8")) {
		codes->from = XNMALLOC (2, char *);
		codes->from[0] = xstrdup (source);
		codes->from[1] = NULL;
		name = appendstr (name, source, NULL);
	} else {
		codes->from = XNMALLOC (3, char *);
		codes->from[0] = xstrdup ("UTF-8");
		codes->from[1] = xstrdup (source);
		codes->from[2] = NULL;
		name = appendstr (name, "UTF-8:", source, NULL);
	}
	codes->to = appendstr (NULL, target, "//IGNORE", NULL);
	/* informational only; no shell quoting concerns */
	name = appendstr (name, " -t ", codes->to, NULL);
	if (quiet >= 2)
		name = appendstr (name, " -q", NULL);
	cmd = pipecmd_new_function (name, &manconv_stdin, &free_manconv_codes,
				    codes);
	free (name);
	pipeline_command (p, cmd);
}
