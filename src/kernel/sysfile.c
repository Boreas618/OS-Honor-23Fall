//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include <fcntl.h>

#include "syscall.h"
#include <aarch64/mmu.h>
#include <fs/file.h>
#include <fs/inode.h>
#include <fs/pipe.h>
#include <kernel/mem.h>
#include <lib/defines.h>
#include <lib/printk.h>
#include <lib/spinlock.h>
#include <lib/string.h>
#include <proc/proc.h>
#include <proc/sched.h>
#include <sys/syscall.h>
#include <vm/paging.h>

extern struct inode_tree inodes;
extern struct block_cache bcache;

struct iovec {
    void *iov_base; /* Starting address. */
    usize iov_len;  /* Number of bytes to transfer. */
};

/* Get the file object by fd */
static struct file *fd2file(int fd) {
    if (fd >= 0 && fd < NOFILE)
        return thisproc()->oftable.ofiles[fd];
    else
        return NULL;
}

/*
 * Allocate a file descriptor for the given file.
 * Takes over file reference from caller on success.
 */
int fdalloc(struct file *f) {
    struct proc *p = thisproc();
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->oftable.ofiles[fd]) {
            p->oftable.ofiles[fd] = f;
            return fd;
        }
    }
    return -1;
}

define_syscall(ioctl, int fd, u64 request) {
    ASSERT(request == 0x5413);
    (void)fd;
    return 0;
}

/*
define_syscall(mmap, void* addr, int length, int prot, int flags, int fd, int offset) {
     // TODO
}

define_syscall(munmap, void *addr, usize length) {
     // TODO
}
*/

/* Get the parameters and call filedup. */
define_syscall(dup, int fd) {
    struct file *f = fd2file(fd);
    if (!f) return -1;
    fd = fdalloc(f);
    if (fd < 0) return -1;
    file_dup(f);
    return fd;
}

/* Get the parameters and call fileread. */
define_syscall(read, int fd, char *buffer, int size) {
    struct file *f = fd2file(fd);
    if (!f || size <= 0 || !user_writeable(buffer, size))
        return -1;
    return file_read(f, buffer, size);
}

/*
 * Get the parameters and call filewrite.
 */
define_syscall(write, int fd, char *buffer, int size) {
    struct file *f = fd2file(fd);
    if (!f || size <= 0 || !user_readable(buffer, size))
        return -1;
    return file_write(f, buffer, size);
}

define_syscall(writev, int fd, struct iovec *iov, int iovcnt) {
    struct file *f = fd2file(fd);
    struct iovec *p;
    if (!f || iovcnt <= 0 || !user_readable(iov, sizeof(struct iovec) * iovcnt))
        return -1;
    usize tot = 0;
    for (p = iov; p < iov + iovcnt; p++) {
        if (!user_readable(p->iov_base, p->iov_len))
            return -1;
        tot += file_write(f, p->iov_base, p->iov_len);
    }
    return tot;
}

/*
 * Get the parameters and call fileclose.
 * Clear this fd of this process.
 */
define_syscall(close, int fd) {
    struct file *f = thisproc()->oftable.ofiles[fd];
    thisproc()->oftable.ofiles[fd] = NULL;
    file_close(f);
    return 0;
}

/*
 * Get the parameters and call filestat.
 */
define_syscall(fstat, int fd, struct stat *st) {
    struct file *f = fd2file(fd);
    if (!f || !user_writeable(st, sizeof(*st)))
        return -1;
    return file_stat(f, st);
}

define_syscall(newfstatat, int dirfd, const char *path, struct stat *st,
               int flags) {
    if (!user_strlen(path, 256) || !user_writeable(st, sizeof(*st)))
        return -1;
    if (dirfd != AT_FDCWD) {
        printk("sys_fstatat: dirfd unimplemented\n");
        return -1;
    }
    if (flags != 0) {
        printk("sys_fstatat: flags unimplemented\n");
        return -1;
    }

    Inode *ip;
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((ip = namei(path, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.lock(ip);
    stati(ip, st);
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);

    return 0;
}

// Is the directory dp empty except for "." and ".." ?
static int isdirempty(Inode *dp) {
    usize off;
    struct dirent de;

    for (off = 2 * sizeof(de); off < dp->entry.num_bytes; off += sizeof(de)) {
        if (inodes.read(dp, (u8 *)&de, off, sizeof(de)) != sizeof(de))
            PANIC();
        if (de.inode_no != 0)
            return 0;
    }
    return 1;
}

define_syscall(unlinkat, int fd, const char *path, int flag) {
    // printk("at unlinkat\n");
    ASSERT(fd == AT_FDCWD && flag == 0);
    Inode *ip, *dp;
    struct dirent de;
    char name[FILE_NAME_MAX_LENGTH];
    usize off;
    if (!user_strlen(path, 256))
        return -1;
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((dp = nameiparent(path, name, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }

    inodes.lock(dp);

    // Cannot unlink "." or "..".
    if (strncmp(name, ".", FILE_NAME_MAX_LENGTH) == 0 ||
        strncmp(name, "..", FILE_NAME_MAX_LENGTH) == 0)
        goto bad;

    usize inumber = inodes.lookup(dp, name, &off);
    if (inumber == 0)
        goto bad;
    ip = inodes.get(inumber);
    inodes.lock(ip);

    if (ip->entry.num_links < 1)
        PANIC();
    if (ip->entry.type == INODE_DIRECTORY && !isdirempty(ip)) {
        inodes.unlock(ip);
        inodes.put(&ctx, ip);
        goto bad;
    }

    memset(&de, 0, sizeof(de));
    if (inodes.write(&ctx, dp, (u8 *)&de, off, sizeof(de)) != sizeof(de))
        PANIC();
    if (ip->entry.type == INODE_DIRECTORY) {
        dp->entry.num_links--;
        inodes.sync(&ctx, dp, true);
    }
    inodes.unlock(dp);
    inodes.put(&ctx, dp);
    ip->entry.num_links--;
    inodes.sync(&ctx, ip, true);
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return 0;

bad:
    inodes.unlock(dp);
    inodes.put(&ctx, dp);
    bcache.end_op(&ctx);
    return -1;
}

/** 
 * Create an inode.
 *
 * Example:
 * Path is "/foo/bar/bar1", type is normal file.
 * You should get the inode of "/foo/bar", and
 * create an inode named "bar1" in this directory.
 *
 * If type is directory, you should additionally handle "." and "..".
 */
struct inode *create(const char *path, short type, short major, short minor,
              OpContext *ctx) {
    struct inode *ip, *dp;
    usize ino;
    char name[FILE_NAME_MAX_LENGTH];

    dp = nameiparent(path, name, ctx);
    ASSERT(dp->entry.type == INODE_DIRECTORY);

    inodes.lock(dp);

    ino = inodes.lookup(dp, name, NULL);
    if (ino != 0) {
        ip = inodes.get(ino);
        inodes.unlock(dp);
        inodes.put(ctx, dp);

        inodes.lock(ip);
        if (type == INODE_REGULAR && (ip->entry.type == INODE_REGULAR ||
                                      ip->entry.type == INODE_DEVICE)) {
            inodes.unlock(ip);
            return ip;
        }
        printk("mkdir: cannot create directory, File exists\n");
        inodes.unlock(ip);
        inodes.put(ctx, ip);
        return 0;
    }

    ip = inodes.get(inodes.alloc(ctx, type));
    inodes.lock(ip);
    ip->entry.major = major;
    ip->entry.minor = minor;
    ip->entry.num_links = 1;
    inodes.sync(ctx, ip, true);

    if (type == INODE_DIRECTORY) {
        dp->entry.num_links++;
        inodes.sync(ctx, ip, true);

        inodes.insert(ctx, ip, ".", ip->inode_no);
        inodes.insert(ctx, ip, "..", dp->inode_no);
    }
    inodes.insert(ctx, dp, name, ip->inode_no);

    inodes.unlock(ip);
    inodes.unlock(dp);
    inodes.put(ctx, dp);
    return ip;
}

define_syscall(openat, int dirfd, const char *path, int omode) {
    int fd;
    struct file *f;
    Inode *ip;

    if (!user_strlen(path, 256))
        return -1;

    if (dirfd != AT_FDCWD) {
        printk("sys_openat: dirfd unimplemented\n");
        return -1;
    }

    OpContext ctx;
    bcache.begin_op(&ctx);
    if (omode & O_CREAT) {
        // FIXME: Support acl mode.
        ip = create(path, INODE_REGULAR, 0, 0, &ctx);
        if (ip == 0) {
            bcache.end_op(&ctx);
            return -1;
        }
    } else {
        if ((ip = namei(path, &ctx)) == 0) {
            bcache.end_op(&ctx);
            return -1;
        }
        inodes.lock(ip);
    }

    if ((f = file_alloc()) == 0 || (fd = fdalloc(f)) < 0) {
        if (f)
            file_close(f);
        inodes.unlock(ip);
        inodes.put(&ctx, ip);
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(ip);
    bcache.end_op(&ctx);

    f->type = FD_INODE;
    f->ip = ip;
    f->off = 0;
    f->readable = !(omode & O_WRONLY);
    f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
    return fd;
}

define_syscall(mkdirat, int dirfd, const char *path, int mode) {
    // printk("at mkdirat \n");
    Inode *ip;
    if (!user_strlen(path, 256))
        return -1;
    if (dirfd != AT_FDCWD) {
        printk("sys_mkdirat: dirfd unimplemented\n");
        return -1;
    }
    if (mode != 0) {
        printk("sys_mkdirat: mode unimplemented\n");
        return -1;
    }
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((ip = create(path, INODE_DIRECTORY, 0, 0, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return 0;
    }
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return 0;
}

define_syscall(mknodat, int dirfd, const char *path, int major, int minor) {
    // printk("at mknodat \n");
    Inode *ip;
    if (!user_strlen(path, 256))
        return -1;
    if (dirfd != AT_FDCWD) {
        printk("sys_mknodat: dirfd unimplemented\n");
        return -1;
    }
    printk("mknodat: path '%s', major:minor %d:%d\n", path, major, minor);
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((ip = create(path, INODE_DEVICE, major, minor, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return 0;
}

define_syscall(chdir, const char *path) {
    // change the cwd (current working dictionary) of current process to 'path'
    // you may need to do some validations
    // printk("at chdir \n");
    Inode *ip;
    OpContext ctx_, *ctx;
    ctx = &ctx_;
    struct proc *proc = thisproc();

    bcache.begin_op(ctx);
    ip = namei(path, ctx);

    if (ip == 0) {
        bcache.end_op(ctx);
        return -1;
    }

    inodes.lock(ip);
    if (ip->entry.type != INODE_DIRECTORY) {
        inodes.unlock(ip);
        bcache.end_op(ctx);
        return -1;
    }

    inodes.unlock(ip);
    inodes.put(ctx, proc->cwd);
    bcache.end_op(ctx);

    proc->cwd = ip;
    return 0;
}

define_syscall(pipe2, int *fd, int flags) {
    struct file *f0, *f1;
    int fd0, fd1;
    if (pipe_alloc(&f0, &f1) < 0) {
        return -1;
    }
    fd0 = fdalloc(f0);
    fd1 = fdalloc(f1);

    fd[0] = fd0;
    fd[1] = fd1;

    ASSERT(flags || true);

    return 0;
}
