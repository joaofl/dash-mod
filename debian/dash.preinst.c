/*
 * This file is in the public domain.
 * You may freely use, modify, distribute, and relicense it.
 */

#include "dash.preinst.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

static void copy(const char *file, const char *dest)
{
	const char * const cmd[] = {"cp", "-dp", file, dest, NULL};

	if (exists(file))
		run(cmd);
	else if (unlink(dest) && errno != ENOENT)
		die_errno("cannot remove %s", dest);
}

static void force_symlink(const char *target, const char *link,
						const char *temp)
{
	/*
	 * Forcibly create a symlink to "target" from "link".
	 * This is performed in two stages with an
	 * intermediate temporary file because symlink(2) cannot
	 * atomically replace an existing file.
	 */
	if ((unlink(temp) && errno != ENOENT) ||
	    symlink(target, temp) ||
	    rename(temp, link))
		die_errno("cannot create symlink %s -> %s", link, target);
}

static void reset_diversion(const char *package, const char *file,
						const char *distrib)
{
	const char * const remove_old_diversion[] =
		{"dpkg-divert", "--package", "dash", "--remove", file, NULL};
	const char * const new_diversion[] =
		{"dpkg-divert", "--package", package,
		"--divert", distrib, "--add", file, NULL};
	run(remove_old_diversion);
	run(new_diversion);
	copy(file, distrib);
}

static int undiverted(const char *path)
{
	const char * const cmd[] =
		{"dpkg-divert", "--listpackage", path, NULL};
	pid_t child;
	char packagename[sizeof("dash\n")];
	size_t len;
	FILE *in = spawn_pipe(&child, cmd, -1);
	int diverted = 1;

	/* Is $path diverted by someone other than dash? */

	len = fread(packagename, 1, sizeof(packagename), in);
	if (ferror(in))
		die_errno("cannot read from dpkg-divert");
	if (len == 0)
		diverted = 0;	/* No diversion. */
	if (len == strlen("dash\n") && !memcmp(packagename, "dash\n", len))
		diverted = 0;	/* Diverted by dash. */

	if (fclose(in))
		die_errno("cannot close read end of pipe");
	wait_or_die(child, "dpkg-divert", ERROR_OK | SIGPIPE_OK);
	return !diverted;
}

int main(int argc, char *argv[])
{
	/*
	 * To help with bootstrapping Debian, the dash package includes
	 * symlinks for /bin/sh and the sh(1) manpage in its data.tar.
	 *
	 * Unless we are careful, unpacking the new version of dash
	 * can overwrite them.  So we tell dpkg that the files from dash
	 * are elsewhere, using a diversion on behalf of another package.
	 *
	 * Based on an idea by Michael Stone.
	 * “You're one sick individual.” -- Anthony Towns
	 * http://bugs.debian.org/cgi-bin/bugreport.cgi?msg=85;bug=34717
	 */
	if (undiverted("/bin/sh"))
		reset_diversion("bash", "/bin/sh", "/bin/sh.distrib");
	if (undiverted("/usr/share/man/man1/sh.1.gz"))
		reset_diversion("bash", "/usr/share/man/man1/sh.1.gz",
				"/usr/share/man/man1/sh.distrib.1.gz");

	/* /bin/sh needs to point to a valid target. */
	if (access("/bin/sh", X_OK)) {
		force_symlink("dash", "/bin/sh", "/bin/sh.temp");
		force_symlink("dash.1.gz", "/usr/share/man/man1/sh.1.gz",
			"/usr/share/man/man1/sh.1.gz.temp");
	}

	return 0;
}
