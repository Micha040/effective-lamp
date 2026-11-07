// fragdesk-server – Dateien (siehe files.h)
#include "files.h"

#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "../src/api/mobile.h"   // randomHex

namespace fdsrv {

namespace {

bool writeAll(int fd, const char* p, size_t n) {
    while (n > 0) {
        ssize_t k = write(fd, p, n);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return false;
        p += k;
        n -= size_t(k);
    }
    return true;
}

std::string parentOf(const std::string& path) {
    size_t slash = path.rfind('/');
    if (slash == std::string::npos) return ".";
    return slash == 0 ? "/" : path.substr(0, slash);
}

void syncDir(const std::string& dir) {   // damit das rename auch nach einem Stromausfall gilt
    int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return;
    fsync(fd);
    close(fd);
}

int removeEntry(const char* path, const struct stat*, int type, struct FTW*) {
    return (type == FTW_DP ? rmdir(path) : unlink(path)) == 0 || errno == ENOENT ? 0 : -1;
}

}  // namespace

std::string lastError() { return std::strerror(errno); }

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    return a.back() == '/' ? a + b : a + "/" + b;
}

bool readFile(const std::string& path, std::string& out, int64_t maxBytes) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st{};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > maxBytes) {
        close(fd);
        return false;
    }
    out.clear();
    out.reserve(size_t(st.st_size));
    char buf[65536];
    for (;;) {
        ssize_t k = read(fd, buf, sizeof buf);
        if (k < 0 && errno == EINTR) continue;
        if (k < 0) {
            close(fd);
            return false;
        }
        if (k == 0) break;
        out.append(buf, size_t(k));
        if (int64_t(out.size()) > maxBytes) {
            close(fd);
            return false;
        }
    }
    close(fd);
    return true;
}

bool writeAtomic(const std::string& path, const std::string& data, int mode) {
    std::string tmp = path + ".tmp-" + fdapi::randomHex(6);
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
    if (fd < 0) return false;
    bool ok = writeAll(fd, data.data(), data.size()) && fsync(fd) == 0;
    ok = close(fd) == 0 && ok;
    if (ok) ok = rename(tmp.c_str(), path.c_str()) == 0;
    if (!ok) {
        int e = errno;
        unlink(tmp.c_str());
        errno = e;
        return false;
    }
    syncDir(parentOf(path));
    return true;
}

bool appendDurable(const std::string& path, const std::string& data, int64_t expectedSize, int64_t& newSize) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_size != expectedSize) {   // jemand anders war schneller
        close(fd);
        errno = EAGAIN;
        return false;
    }
    bool ok = lseek(fd, expectedSize, SEEK_SET) == expectedSize && writeAll(fd, data.data(), data.size()) && fdatasync(fd) == 0;
    if (!ok) {
        int e = errno;
        if (ftruncate(fd, expectedSize) == 0) fdatasync(fd);   // halbes Stück wieder weg
        close(fd);
        errno = e;
        return false;
    }
    close(fd);
    newSize = expectedSize + int64_t(data.size());
    if (expectedSize == 0) syncDir(parentOf(path));
    return true;
}

bool makeDirs(const std::string& path, int mode) {
    if (path.empty()) return false;
    if (isDir(path)) return true;
    std::string parent = parentOf(path);
    if (parent != path && !isDir(parent) && !makeDirs(parent, mode)) return false;
    return mkdir(path.c_str(), mode_t(mode)) == 0 || (errno == EEXIST && isDir(path));
}

bool removeFile(const std::string& path) { return unlink(path.c_str()) == 0 || errno == ENOENT; }

bool removeTree(const std::string& path) {
    if (!isDir(path)) return removeFile(path);
    return nftw(path.c_str(), removeEntry, 16, FTW_DEPTH | FTW_PHYS) == 0;
}

bool renameFile(const std::string& from, const std::string& to) {
    if (rename(from.c_str(), to.c_str()) != 0) return false;
    syncDir(parentOf(to));
    return true;
}

int64_t fileSize(const std::string& path) {
    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    return int64_t(st.st_size);
}

int64_t fileTime(const std::string& path) {
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) return 0;
    return int64_t(st.st_mtime);
}

bool isDir(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::vector<std::string> listDir(const std::string& path) {
    std::vector<std::string> out;
    DIR* d = opendir(path.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n != "." && n != "..") out.push_back(n);
    }
    closedir(d);
    return out;
}

int64_t freeBytes(const std::string& path) {
    struct statvfs s{};
    if (statvfs(path.c_str(), &s) != 0) return -1;
    return int64_t(s.f_bavail) * int64_t(s.f_frsize);
}

}  // namespace fdsrv
