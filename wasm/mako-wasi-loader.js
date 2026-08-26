/**
 * Browser WASI polyfill seed for Mako wasm32-wasi (Vision Later Partial).
 *
 * - fd_write → console.log / #out; also appends into writable virtual files
 * - environ_get / args_get: empty environ and argv
 * - clock_time_get → Date.now() (realtime, nanoseconds)
 * - random_get → crypto.getRandomValues (fallback Math.random)
 * - fd_prestat_get / fd_prestat_dir_name → one virtual preopen "/" on fd 3
 * - path_open: hello.txt→"hi", bye.txt→"bye"; CREAT of new path → empty writable file
 * - path_open `/host/<rel>` → fetch cwd-relative file; write if FD_WRITE rights (overlay;
 *   no `..`, no absolute escape). CREAT allowed for empty overlay under /host/.
 * - path_create_directory → ENOTCAPABLE (76)
 * - path_unlink_file → remove virtual path (EBADF/ENOENT as appropriate)
 * - fd_seek / fd_tell on virtual file fds (SET/CUR/END)
 * - fd_filestat_get → size (and filetype) for virtual files
 * - fd_read / fd_write round-trip on created virtual files
 *
 * Security limits for /host/: browser fetch + in-memory write overlay only;
 * path must be relative under the served directory; `..` and absolute paths rejected.
 *
 * Usage (after `./scripts/wasi-ci-build.sh`):
 *   cp out/hello.wasm wasm/
 *   python3 -m http.server -d wasm 8080
 */
const out = document.getElementById("out");

/** Single virtual preopen: fd 3 → "/" */
const PREOPEN_FD = 3;
const PREOPEN_PATH = "/";

/** __WASI_OFLAGS_CREAT */
const OFLAGS_CREAT = 1;
/** __WASI_RIGHTS_FD_WRITE */
const RIGHTS_FD_WRITE = 1n << 6n;

/** Virtual files under preopen (path → content bytes). Mutable store. */
const VFILES = new Map([
  ["hello.txt", new TextEncoder().encode("hi")],
  ["/hello.txt", new TextEncoder().encode("hi")],
  ["bye.txt", new TextEncoder().encode("bye")],
  ["/bye.txt", new TextEncoder().encode("bye")],
]);
/** In-memory overlay for /host/<rel> writes (never escapes to real FS). */
const HOST_OVERLAY = new Map();
const VFILE_FD_BASE = 4;
/** @type {Map<number, {path: string, bytes: Uint8Array, pos: number, writable: boolean, host?: boolean}>} */
const openFiles = new Map();
let nextFd = VFILE_FD_BASE;

function log(msg) {
  out.textContent += msg + "\n";
  console.log(msg);
}

function normalizePath(path) {
  if (path.startsWith("/")) return path.slice(1) || path;
  return path;
}

function pathKeys(path) {
  const n = normalizePath(path);
  return n.startsWith("/") ? [n] : [n, "/" + n];
}

function getVFile(path) {
  for (const k of pathKeys(path)) {
    if (VFILES.has(k)) return { key: k, bytes: VFILES.get(k) };
  }
  return null;
}

function setVFile(path, bytes) {
  const n = normalizePath(path);
  VFILES.set(n, bytes);
  VFILES.set("/" + n.replace(/^\//, ""), bytes);
}

/** Validate guest pointer + length fits within WASM linear memory. */
function boundsCheck(memory, ptr, len) {
  return ptr >= 0 && len >= 0 && ptr + len <= memory.buffer.byteLength;
}

/** fd_write: stdout/stderr → console; writable virtual fd → append to buffer. */
function fd_write(memory, fd, iovs_ptr, iovs_len, nwritten_ptr) {
  const memLen = memory.buffer.byteLength;
  if (!boundsCheck(memory, iovs_ptr, iovs_len * 8)) return 21; // EFAULT
  if (!boundsCheck(memory, nwritten_ptr, 4)) return 21;
  const view = new DataView(memory.buffer);
  const file = openFiles.get(fd);
  if (file && file.writable) {
    let written = 0;
    const chunks = [];
    for (let i = 0; i < iovs_len; i++) {
      const base = iovs_ptr + i * 8;
      const buf = view.getUint32(base, true);
      const len = view.getUint32(base + 4, true);
      if (!boundsCheck(memory, buf, len)) return 21; // EFAULT
      chunks.push(new Uint8Array(memory.buffer, buf, len).slice());
      written += len;
    }
    const total = file.bytes.length + written;
    const merged = new Uint8Array(total);
    merged.set(file.bytes, 0);
    let off = file.bytes.length;
    for (const c of chunks) {
      merged.set(c, off);
      off += c.length;
    }
    file.bytes = merged;
    file.pos = merged.length;
    if (file.host) {
      const rel = String(file.path).replace(/^\/host\//, "");
      HOST_OVERLAY.set(rel, merged);
    } else {
      setVFile(file.path, merged);
    }
    view.setUint32(nwritten_ptr, written, true);
    return 0;
  }
  let written = 0;
  let text = "";
  for (let i = 0; i < iovs_len; i++) {
    const base = iovs_ptr + i * 8;
    const buf = view.getUint32(base, true);
    const len = view.getUint32(base + 4, true);
    if (!boundsCheck(memory, buf, len)) return 21; // EFAULT
    const bytes = new Uint8Array(memory.buffer, buf, len);
    text += new TextDecoder().decode(bytes);
    written += len;
  }
  if (fd === 1 || fd === 2) {
    const line = text.replace(/\n$/, "");
    if (line.length) log(line);
    else if (text.includes("\n")) log("");
  }
  view.setUint32(nwritten_ptr, written, true);
  return 0;
}

/** Empty argv/environ: write a single NULL pointer at environ/argv[0]. */
function writeEmptyPointerList(memory, list_ptr) {
  const view = new DataView(memory.buffer);
  view.setUint32(list_ptr, 0, true);
  return 0;
}

/** WASI clock_time_get: write u64 ns timestamp (little-endian) at timestamp_ptr. */
function clock_time_get(memory, _clock_id, _precision, timestamp_ptr) {
  if (!boundsCheck(memory, timestamp_ptr, 8)) return 21; // EFAULT
  const view = new DataView(memory.buffer);
  const ns = BigInt(Date.now()) * 1000000n;
  view.setBigUint64(timestamp_ptr, ns, true);
  return 0;
}

/** WASI random_get: fill buffer with crypto.getRandomValues when available. */
function random_get(memory, buf, len) {
  if (!boundsCheck(memory, buf, len)) return 21; // EFAULT
  const bytes = new Uint8Array(memory.buffer, buf, len);
  if (typeof crypto !== "undefined" && typeof crypto.getRandomValues === "function") {
    crypto.getRandomValues(bytes);
  } else {
    for (let i = 0; i < len; i++) bytes[i] = (Math.random() * 256) | 0;
  }
  return 0;
}

function readGuestPath(memory, pathPtr, pathLen) {
  if (!boundsCheck(memory, pathPtr, pathLen)) return "";
  const bytes = new Uint8Array(memory.buffer, pathPtr, pathLen);
  return new TextDecoder().decode(bytes);
}

function wasiPolyfill(getMemory) {
  return {
    wasi_snapshot_preview1: {
      fd_write(fd, iovs, iovs_len, nwritten) {
        return fd_write(getMemory(), fd, iovs, iovs_len, nwritten);
      },
      fd_close(fd) {
        if (openFiles.has(fd)) openFiles.delete(fd);
        return 0;
      },
      /** whence: 0=SET, 1=CUR, 2=END. Writes new offset to offset_out_ptr (u64 LE). */
      fd_seek(fd, offset, whence, offset_out_ptr) {
        const file = openFiles.get(fd);
        if (!file) return 8; // EBADF
        const mem = getMemory();
        if (!boundsCheck(mem, offset_out_ptr, 8)) return 21;
        const view = new DataView(mem.buffer);
        const off = typeof offset === "bigint" ? Number(offset) : offset;
        let next = file.pos;
        if (whence === 0) next = off;
        else if (whence === 1) next = file.pos + off;
        else if (whence === 2) next = file.bytes.length + off;
        else return 28; // EINVAL
        if (next < 0) return 28;
        file.pos = next;
        view.setBigUint64(offset_out_ptr, BigInt(next), true);
        return 0;
      },
      fd_tell(fd, offset_out_ptr) {
        const file = openFiles.get(fd);
        if (!file) return 8;
        if (!boundsCheck(getMemory(), offset_out_ptr, 8)) return 21;
        new DataView(getMemory().buffer).setBigUint64(
          offset_out_ptr,
          BigInt(file.pos),
          true
        );
        return 0;
      },
      /**
       * Minimal __wasi_filestat_t (preview1): write filetype + size.
       * Layout (64-bit): device(8) ino(8) filetype(1) nlink(8) size(8) ...
       * We zero the buffer then set filetype=4 (REGULAR_FILE) and size.
       */
      fd_filestat_get(fd, buf) {
        const file = openFiles.get(fd);
        if (!file) return 8; // EBADF
        const mem = getMemory();
        if (!boundsCheck(mem, buf, 64)) return 21; // EFAULT
        const view = new DataView(mem.buffer);
        // Zero first 64 bytes of filestat
        for (let i = 0; i < 64; i++) view.setUint8(buf + i, 0);
        view.setUint8(buf + 16, 4); // __WASI_FILETYPE_REGULAR_FILE
        // size at offset 32 in wasi_snapshot_preview1 filestat
        view.setBigUint64(buf + 32, BigInt(file.bytes.length), true);
        return 0;
      },
      fd_fdstat_get() {
        return 0;
      },
      path_unlink_file(dirfd, path_ptr, path_len) {
        if (dirfd !== PREOPEN_FD) return 76; // ENOTCAPABLE
        const path = readGuestPath(getMemory(), path_ptr, path_len);
        const entry = getVFile(path);
        if (!entry) return 44; // ENOENT
        for (const k of pathKeys(path)) VFILES.delete(k);
        // Invalidate open fds pointing at this path
        for (const [fd, f] of openFiles) {
          if (f.path === entry.key || pathKeys(f.path).includes(entry.key)) {
            openFiles.delete(fd);
          }
        }
        return 0;
      },
      environ_sizes_get(count_ptr, buf_size_ptr) {
        const v = new DataView(getMemory().buffer);
        v.setUint32(count_ptr, 0, true);
        v.setUint32(buf_size_ptr, 0, true);
        return 0;
      },
      environ_get(environ_ptr, _environ_buf_ptr) {
        return writeEmptyPointerList(getMemory(), environ_ptr);
      },
      args_sizes_get(argc_ptr, argv_buf_size_ptr) {
        const v = new DataView(getMemory().buffer);
        v.setUint32(argc_ptr, 0, true);
        v.setUint32(argv_buf_size_ptr, 0, true);
        return 0;
      },
      args_get(argv_ptr, _argv_buf_ptr) {
        return writeEmptyPointerList(getMemory(), argv_ptr);
      },
      clock_time_get(clock_id, precision, timestamp_ptr) {
        return clock_time_get(getMemory(), clock_id, precision, timestamp_ptr);
      },
      proc_exit(code) {
        log("proc_exit(" + code + ")");
      },
      random_get(buf, len) {
        return random_get(getMemory(), buf, len);
      },
      poll_oneoff() {
        return 58;
      },
      path_open(
        dirfd,
        _dirflags,
        path_ptr,
        path_len,
        oflags,
        fs_rights_base,
        _fs_rights_inheriting,
        _fdflags,
        fd_ptr
      ) {
        if (dirfd !== PREOPEN_FD) return 76; // ENOTCAPABLE
        const mem = getMemory();
        const path = readGuestPath(mem, path_ptr, path_len);
        const wantWrite = (BigInt(fs_rights_base) & RIGHTS_FD_WRITE) !== 0n;
        // /host/<rel> — sandboxed fetch + optional write overlay (cwd-relative).
        // Reject `..`, absolute escapes. Write/CREAT only into HOST_OVERLAY.
        if (path === "/host" || path.startsWith("/host/") || path.startsWith("host/")) {
          let rel = path.replace(/^\/?host\/?/, "");
          if (!rel || rel.includes("..") || rel.startsWith("/") || rel.includes("\\")) {
            return 28; // EINVAL
          }
          const hostPath = "/host/" + rel;
          if (HOST_OVERLAY.has(rel)) {
            const bytes = HOST_OVERLAY.get(rel);
            const fd = nextFd++;
            openFiles.set(fd, {
              path: hostPath,
              bytes: bytes.slice(),
              pos: 0,
              writable: wantWrite,
              host: true,
            });
            new DataView(mem.buffer).setUint32(fd_ptr, fd, true);
            return 0;
          }
          if ((oflags & OFLAGS_CREAT) !== 0) {
            if (!wantWrite) return 76; // ENOTCAPABLE — CREAT without write
            const empty = new Uint8Array(0);
            HOST_OVERLAY.set(rel, empty);
            const fd = nextFd++;
            openFiles.set(fd, {
              path: hostPath,
              bytes: empty.slice(),
              pos: 0,
              writable: true,
              host: true,
            });
            new DataView(mem.buffer).setUint32(fd_ptr, fd, true);
            return 0;
          }
          try {
            const xhr = new XMLHttpRequest();
            xhr.open("GET", "./" + rel, false);
            xhr.responseType = "arraybuffer";
            xhr.send(null);
            if (xhr.status < 200 || xhr.status >= 300) return 44; // ENOENT
            const bytes = new Uint8Array(xhr.response);
            const fd = nextFd++;
            openFiles.set(fd, {
              path: hostPath,
              bytes: bytes.slice(),
              pos: 0,
              writable: wantWrite,
              host: true,
            });
            new DataView(mem.buffer).setUint32(fd_ptr, fd, true);
            return 0;
          } catch (_e) {
            return 44;
          }
        }
        let entry = getVFile(path);
        let writable = false;
        if (!entry) {
          if ((oflags & OFLAGS_CREAT) === 0) return 44; // ENOENT
          const empty = new Uint8Array(0);
          setVFile(path, empty);
          entry = getVFile(path);
          writable = true;
        } else if ((oflags & OFLAGS_CREAT) !== 0) {
          // Open existing for write/append seed
          writable = true;
        }
        const fd = nextFd++;
        openFiles.set(fd, {
          path: entry.key,
          bytes: entry.bytes.slice(),
          pos: 0,
          writable,
        });
        new DataView(mem.buffer).setUint32(fd_ptr, fd, true);
        return 0;
      },
      path_create_directory(_dirfd, _path_ptr, _path_len) {
        return 76; // ENOTCAPABLE
      },
      fd_read(fd, iovs, iovs_len, nread_ptr) {
        const mem = getMemory();
        if (!boundsCheck(mem, iovs, iovs_len * 8)) return 21;
        if (!boundsCheck(mem, nread_ptr, 4)) return 21;
        const view = new DataView(mem.buffer);
        const file = openFiles.get(fd);
        if (!file) {
          view.setUint32(nread_ptr, 0, true);
          return 0;
        }
        // Refresh from store if another fd wrote (virtual files only; not /host/)
        if (!String(file.path).startsWith("/host/")) {
          const latest = getVFile(file.path);
          if (latest) file.bytes = latest.bytes;
        } else if (file.host) {
          const rel = String(file.path).replace(/^\/host\//, "");
          if (HOST_OVERLAY.has(rel)) file.bytes = HOST_OVERLAY.get(rel);
        }
        let total = 0;
        for (let i = 0; i < iovs_len; i++) {
          const base = iovs + i * 8;
          const buf = view.getUint32(base, true);
          const len = view.getUint32(base + 4, true);
          if (!boundsCheck(mem, buf, len)) return 21; // EFAULT
          const avail = file.bytes.length - file.pos;
          if (avail <= 0) break;
          const n = Math.min(len, avail);
          const dest = new Uint8Array(mem.buffer, buf, n);
          dest.set(file.bytes.subarray(file.pos, file.pos + n));
          file.pos += n;
          total += n;
        }
        view.setUint32(nread_ptr, total, true);
        return 0;
      },
      fd_prestat_get(fd, buf) {
        if (fd !== PREOPEN_FD) return 8;
        const view = new DataView(getMemory().buffer);
        view.setUint8(buf, 0);
        view.setUint32(buf + 4, PREOPEN_PATH.length, true);
        return 0;
      },
      fd_prestat_dir_name(fd, path_ptr, path_len) {
        if (fd !== PREOPEN_FD) return 8;
        if (path_len < PREOPEN_PATH.length) return 28;
        const bytes = new Uint8Array(getMemory().buffer, path_ptr, PREOPEN_PATH.length);
        for (let i = 0; i < PREOPEN_PATH.length; i++) {
          bytes[i] = PREOPEN_PATH.charCodeAt(i);
        }
        return 0;
      },
    },
  };
}

async function main() {
  out.textContent = "";
  log("fetch hello.wasm…");
  const res = await fetch("./hello.wasm");
  if (!res.ok) {
    log("missing hello.wasm — run ./scripts/wasi-ci-build.sh and copy to wasm/");
    return;
  }
  const bytes = await res.arrayBuffer();
  log(`bytes: ${bytes.byteLength}`);
  let memory = null;
  const imports = wasiPolyfill(() => memory);
  try {
    const { instance } = await WebAssembly.instantiate(bytes, imports);
    memory = instance.exports.memory;
    log("instantiated (virtual fs: hello/bye + CREAT writable)");
    if (typeof instance.exports._start === "function") {
      instance.exports._start();
      log("_start done");
    } else {
      log("no _start export");
    }
  } catch (e) {
    log("instantiate/run: " + e.message);
    log("polyfill: fd_write, virtual files, /host/ write overlay");
  }
}

main();
