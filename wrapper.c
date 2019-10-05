/*
 * Copyright 2017 Joey Hewitt <joey@joeyhewitt.com>
 *
 * This file is part of libsmdpkt_wrapper.
 *
 * libsmdpkt_wrapper is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libsmdpkt_wrapper is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libsmdpkt_wrapper.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This is intended as a LD_PRELOAD'ed library. It will look for open()s of
 * char devices driven by smdpkt. When seen, the SMD_PKT_IOCTL_BLOCKING_WRITE
 * ioctl will be issued, and poll() calls specifying POLLOUT will act as if
 * the device is always ready to be written to. (POLLOUT on smdpkt is not supported
 * in the kernel.)
 *
 * Because of the missing POLLOUT support, ofonod and qmicli don't work on SMD
 * devices (they hang waiting to write), but with this wrapper they do.
 *
 * The ioctl is used by the proprietary qmuxd daemon, and I *think* it makes sense
 * to move the waiting from poll() to write().
 *
 * Presumably the poll() fix would be a two-line kernel patch mirroring the
 * support for POLLIN, but I'd rather not mess with kernel code.
 *
 * For simplicity and efficiency, we assume only one smd device will be opened,
 * and not closed.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <linux/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <string.h>

#define SMD_PKT_IOCTL_MAGIC (0xC2)

#define SMD_PKT_IOCTL_BLOCKING_WRITE \
		_IOR(SMD_PKT_IOCTL_MAGIC, 0, unsigned int)

static int smd_fd = -1;

static char is_smd_device(int fd) {
	struct stat stat;

	if (fstat(fd, &stat)) {
		return 0;
	}
	if (!S_ISCHR(stat.st_mode)) {
		return 0;
	}

	char path[64];
	if (snprintf(path, sizeof(path), "/sys/dev/char/%d:%d", major(stat.st_rdev), minor(stat.st_rdev)) < 0) {
		return 0;
	}

	char linkpath[128];
	if (readlink(path, linkpath, sizeof(linkpath)) < 0) {
		return 0;
	}

	return strstr(linkpath, "/smdpkt/") != NULL;
}

int open(const char *pathname, int flags) {
	int (*real_func)(const char *, int) = dlsym(RTLD_NEXT, "open");
	int fd = real_func(pathname, flags);

	if (fd >= 0 && is_smd_device(fd)) {
		smd_fd = fd;
		unsigned int blocking = 1;
		ioctl(fd, SMD_PKT_IOCTL_BLOCKING_WRITE, &blocking);
	}

	return fd;
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
	int (*real_func)(struct pollfd *, nfds_t, int) = dlsym(RTLD_NEXT, "poll");

	struct pollfd orig_fds[nfds];

	if (smd_fd >= 0) {
		memcpy(orig_fds, fds, sizeof(orig_fds[0]) * nfds);

		for (nfds_t i = 0; i < nfds; i++) {
			if (fds[i].fd == smd_fd && (fds[i].events & POLLOUT)) {
				fds[i].events &= ~POLLOUT;
				timeout = 0; // no need to wait; POLLOUT is already "known" to be set
			}
		}
	}

	int poll_ret = real_func(fds, nfds, timeout);

	if (smd_fd >= 0) {
		poll_ret = 0; // need to recalculate this
		for (nfds_t i = 0; i < nfds; i++) {
			if (fds[i].fd == smd_fd && (orig_fds[i].events & POLLOUT)) {
				if (!(fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))) {
					fds[i].revents |= POLLOUT;
				}
			}
			if (fds[i].revents) {
				poll_ret++;
			}
		}
	}

	return poll_ret;
}
