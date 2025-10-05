/*
 ° Copyright (c) 2025 Luc Hondareyte
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if defined (__FreeBSD__) || defined (__NetBSD__) || defined(__OpenBSD__)
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined (__OpenBSD__)
#include <fcntl.h>
#include <unistd.h>
#endif

#include <sys/types.h>
#include <sys/sysctl.h>

#define MAXPATH 256

char *
get_install_path(void) {

	/*
	 * get path of the current process with sysctl()
	 */

#if defined (__FreeBSD__)
	int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
#elif defined (__NetBSD__)
	int mib[] = {CTL_KERN, KERN_PROC_ARGS, -1, KERN_PROC_PATHNAME};
#endif

	char *p = NULL; 
	char *dir = NULL;
	size_t s_path = MAXPATH;  

	p = malloc(MAXPATH * sizeof(char));
	if (p == NULL) {
		return NULL;
	}
	memset(p, 0, MAXPATH);

#if defined (__FreeBSD__) || defined (__NetBSD__)
	if (sysctl(mib, 4, p, &s_path, NULL, 0) == 0) {
		if ((dir = dirname(p)) != NULL) {
			memmove(p, dir, strlen(dir) + 1);
			return p;
		}
		else {
			free(p);
			return NULL;
		}
	}
#elif defined (__OpenBSD__)
	/* 
	 * Afaik, there is no official way to determine 
	 * the path of an executable on OpenBSD, so we 
	 * badly try all common paths.
	 */
	const char *paths[] = {
		"/bin", "/sbin", "/usr/bin", "/usr/sbin",
		"/usr/local/bin", "/usr/local/sbin",
		"/opt/Pawn/bin", "/opt/pawn/bin",
		"/usr/local/Pawn/bin", "/usr/local/pawn/bin", "."
	};
	int i=0;
	int s = (sizeof(paths) / sizeof(paths[0]));

	do {
		memset(dir, 0, MAXPATH);
		strncpy(dir, paths[i], strlen(paths[i]));
		strncat(dir, "/", 1);
		strncat(dir, getprogname(), strlen(getprogname()));
		if (access(dir,X_OK) == 0) {
			strncat(p, paths[i], strlen(paths[i]));
			return(p);
		}
		i++;
	} while(i < s);
#endif
	free(p);
	return NULL;
}

#endif /* *BSD */

