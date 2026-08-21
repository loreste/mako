# Standard library safety matrix

This matrix classifies every checked-in `std/**/*.mko` file against the
[stdlib memory-safety gate](STDLIB_SAFETY.md). It is intentionally about Mako's
safe surface, not Go or Rust syntax parity.

Status values:

- `safe`: pure safe-Mako/package wrapper over compiler-owned checked primitives.
- `checked-native`: crosses C/OS/crypto/compression/network/parser code and
  needs explicit wrapper contracts plus sanitizer and malformed-input tests.
- `unsafe-boundary`: must stay explicit or opt-in because the compiler cannot
  prove the underlying operation safe.
- `blocked`: not safe enough to claim as default safe stdlib yet.
- `won't`: intentionally excluded from safe parity.

## Current blockers

- Full `examples/testing` is not green on both C and native backends.
- Native-backed packages need package-local safety contracts.
- Parser/codec packages need malformed-input, size-overflow, and cleanup tests.
- Dynamic/plugin/syscall/runtime surfaces need explicit unsafe-boundary wording
  or checked-handle hardening before they can be included in the default safe
  claim.

## Matrix

| File | Tier | Required before 100% safe claim |
|------|------|---------------------------------|
| `archive/tar/tar.mko` | `checked-native` | malformed archive, long name, size overflow, cleanup tests |
| `archive/zip/zip.mko` | `checked-native` | malformed central directory, zip bomb limits, cleanup tests |
| `bufio/bufio.mko` | `checked-native` | read/write bounds, repeated flush/close tests |
| `bytes/bytes.mko` | `safe` | keep OOB clamp and ownership tests |
| `cmp/cmp.mko` | `safe` | keep scalar comparison tests |
| `collections/collections.mko` | `safe` | generic collection ownership/drop tests |
| `collections/stack.mko` | `safe` | stack push/pop ownership tests |
| `compress/bzip2/bzip2.mko` | `checked-native` | optional-lib failure path, malformed stream, output cap tests |
| `compress/flate/flate.mko` | `checked-native` | malformed stream, output cap, zlib error cleanup tests |
| `compress/gzip/gzip.mko` | `checked-native` | header/trailer corruption, output cap, cleanup tests |
| `compress/lzw/lzw.mko` | `checked-native` | invalid code stream, dictionary bounds tests |
| `compress/zlib/zlib.mko` | `checked-native` | malformed stream, output cap, cleanup tests |
| `context/context.mko` | `safe` | value-chain ownership and shadowing tests |
| `crypto/aes/aes.mko` | `checked-native` | key/nonce length validation and error cleanup tests |
| `crypto/argon2/argon2.mko` | `checked-native` | parameter bounds, allocation limits, cleanup tests |
| `crypto/bcrypt/bcrypt.mko` | `checked-native` | cost bounds and malformed hash tests |
| `crypto/crypto.mko` | `checked-native` | aggregate crypto wrapper contract |
| `crypto/ecdsa/ecdsa.mko` | `checked-native` | key/signature length validation, OpenSSL cleanup tests |
| `crypto/hkdf/hkdf.mko` | `checked-native` | RFC vector plus zero/large length tests |
| `crypto/hmac/hmac.mko` | `checked-native` | key/message length and cleanup tests |
| `crypto/md5/md5.mko` | `checked-native` | digest length and allocation tests |
| `crypto/pbkdf2/pbkdf2.mko` | `checked-native` | iteration/key-length bounds tests |
| `crypto/rand/rand.mko` | `checked-native` | OS RNG error propagation tests |
| `crypto/rsa/rsa.mko` | `checked-native` | DER/signature bounds and verify-only contract |
| `crypto/sha1/sha1.mko` | `checked-native` | digest length and allocation tests |
| `crypto/sha256/sha256.mko` | `checked-native` | digest length and allocation tests |
| `crypto/sha3/sha3.mko` | `checked-native` | digest length and allocation tests |
| `crypto/sha512/sha512.mko` | `checked-native` | digest length and allocation tests |
| `crypto/subtle/subtle.mko` | `checked-native` | constant-time length mismatch tests |
| `crypto/tls/tls.mko` | `checked-native` | handle lifecycle, verify defaults, close-in-use tests |
| `crypto/x509/x509.mko` | `checked-native` | malformed PEM/DER, bounded parse, cleanup tests |
| `database/sql/sql.mko` | `checked-native` | parameter arity, cursor cleanup, connection close tests |
| `diameter/diameter.mko` | `checked-native` | frame length, AVP bounds, malformed input tests |
| `dtls/dtls.mko` | `checked-native` | UDP/TLS handle lifecycle and timeout cleanup tests |
| `embed/embed.mko` | `safe` | bounded resource lookup tests |
| `encoding/ascii85/ascii85.mko` | `checked-native` | malformed block and output size tests |
| `encoding/asn1/asn1.mko` | `checked-native` | DER length overflow and malformed integer tests |
| `encoding/avro/avro.mko` | `checked-native` | malformed schema/data and size limit tests |
| `encoding/base32/base32.mko` | `checked-native` | malformed padding and output size tests |
| `encoding/base64/base64.mko` | `checked-native` | malformed padding and output size tests |
| `encoding/binary/binary.mko` | `safe` | endian bounds tests |
| `encoding/cbor/cbor.mko` | `checked-native` | malformed length and nesting limit tests |
| `encoding/csv/csv.mko` | `checked-native` | quoted field, row growth, malformed input tests |
| `encoding/gob/gob.mko` | `checked-native` | bounded decode and malformed type tests |
| `encoding/hex/hex.mko` | `checked-native` | odd length and invalid byte tests |
| `encoding/json/json.mko` | `checked-native` | malformed UTF-8, nesting, escape, cleanup tests |
| `encoding/msgpack/msgpack.mko` | `checked-native` | malformed length and nesting limit tests |
| `encoding/pem/pem.mko` | `checked-native` | malformed block and output bounds tests |
| `encoding/protobuf/protobuf.mko` | `checked-native` | varint overflow and malformed field tests |
| `encoding/toml/toml.mko` | `checked-native` | malformed key/value and allocation tests |
| `encoding/xml/xml.mko` | `checked-native` | entity expansion limits and malformed tag tests |
| `encoding/yaml/yaml.mko` | `checked-native` | alias/nesting limits and malformed input tests |
| `errors/errors.mko` | `safe` | wrapping/root tests |
| `expvar/expvar.mko` | `checked-native` | concurrent registry and string ownership tests |
| `flag/flag.mko` | `safe` | parse failure tests |
| `fmt/fmt.mko` | `checked-native` | format width/precision bounds tests |
| `graphql/graphql.mko` | `checked-native` | query depth, JSON escape, malformed body tests |
| `graphql/schema.mko` | `safe` | schema ownership tests |
| `grpc/grpc.mko` | `checked-native` | frame length, compression flag, malformed input tests |
| `hash/adler32/adler32.mko` | `checked-native` | known vector and empty input tests |
| `hash/crc32/crc32.mko` | `checked-native` | known vector and table bounds tests |
| `hash/crc64/crc64.mko` | `checked-native` | sign-bit and table bounds tests |
| `hash/fnv/fnv.mko` | `checked-native` | known vector tests |
| `hash/hash.mko` | `safe` | common interface tests |
| `hash/maphash/maphash.mko` | `checked-native` | seeded hash and bounds tests |
| `html/html.mko` | `safe` | escape/unescape bounds tests |
| `html/template/template.mko` | `checked-native` | escaping, recursion, malformed template tests |
| `image/color/color.mko` | `safe` | channel clamp tests |
| `image/draw/draw.mko` | `checked-native` | rectangle overlap and stride bounds tests |
| `image/gif/gif.mko` | `checked-native` | malformed LZW, frame bounds, palette tests |
| `image/image.mko` | `safe` | point/rectangle invariant tests |
| `image/jpeg/jpeg.mko` | `checked-native` | malformed marker, DCT/Huffman bounds tests |
| `image/png/png.mko` | `checked-native` | chunk length, CRC, decompression limit tests |
| `index/suffixarray/suffixarray.mko` | `safe` | lookup bounds and empty input tests |
| `io/fs/fs.mko` | `checked-native` | path traversal and FileInfo ownership tests |
| `io/io.mko` | `checked-native` | short read/write, copy limit, cleanup tests |
| `iter/iter.mko` | `safe` | iterator exhaustion and ownership tests |
| `llm/llm.mko` | `checked-native` | HTTP/JSON bounds, retry cleanup, secret handling tests |
| `log/log.mko` | `checked-native` | file handle and concurrent write tests |
| `log/slog/slog.mko` | `checked-native` | redaction and concurrent field ownership tests |
| `log/syslog/syslog.mko` | `checked-native` | PRI bounds and malformed message tests |
| `maps/maps.mko` | `safe` | map ownership and remove tests |
| `math/big/big.mko` | `checked-native` | allocation limits and parse overflow tests |
| `math/bits/bits.mko` | `safe` | scalar bit operation tests |
| `math/cmplx/cmplx.mko` | `safe` | complex pair math tests |
| `math/math.mko` | `safe` | scalar math and NaN/Inf tests |
| `math/rand/rand.mko` | `safe` | deterministic seed and bounds tests |
| `messaging/messaging.mko` | `checked-native` | queue close/drain and ownership tests |
| `messaging/nats.mko` | `checked-native` | protocol frame and reconnect cleanup tests |
| `messaging/redis.mko` | `checked-native` | RESP parse and connection cleanup tests |
| `mime/mime.mko` | `safe` | media type parse bounds tests |
| `mime/multipart/multipart.mko` | `checked-native` | boundary limits and part cleanup tests |
| `mime/quotedprintable/quotedprintable.mko` | `checked-native` | malformed escape and output size tests |
| `net/http/cookiejar/cookiejar.mko` | `checked-native` | host isolation and expiry cleanup tests |
| `net/http/http.mko` | `checked-native` | header validation, body limits, connection cleanup tests |
| `net/http/httptrace/httptrace.mko` | `checked-native` | event buffer ownership tests |
| `net/http/httputil/httputil.mko` | `checked-native` | proxy escape and connection cleanup tests |
| `net/mail/mail.mko` | `checked-native` | malformed address and header injection tests |
| `net/net.mko` | `checked-native` | socket handle lifecycle and timeout tests |
| `net/netip/netip.mko` | `safe` | parse bounds and prefix tests |
| `net/rpc/rpc.mko` | `checked-native` | JSON-RPC escape and malformed request tests |
| `net/smtp/smtp.mko` | `checked-native` | STARTTLS verify policy and auth cleanup tests |
| `net/textproto/textproto.mko` | `checked-native` | header canonicalization and injection tests |
| `net/url/url.mko` | `safe` | parse/escape bounds tests |
| `net/wss.mko` | `checked-native` | websocket frame bounds and close tests |
| `openapi/openapi.mko` | `checked-native` | schema size and JSON parse limit tests |
| `os/env/env.mko` | `checked-native` | env string ownership tests |
| `os/exec/exec.mko` | `unsafe-boundary` | process boundary, argv/env ownership, timeout/kill tests |
| `os/os.mko` | `checked-native` | file handle lifecycle, path limits, permissions tests |
| `os/signal/signal.mko` | `checked-native` | handler lifecycle and channel ownership tests |
| `os/user/user.mko` | `checked-native` | passwd/group lookup ownership tests |
| `path/filepath/filepath.mko` | `safe` | clean/rel/glob traversal tests |
| `path/path.mko` | `safe` | clean/join/match tests |
| `peer/peer.mko` | `checked-native` | table ownership and route bounds tests |
| `plugin/plugin.mko` | `unsafe-boundary` | dynamic code boundary; require manifest, symbol, handle hardening |
| `print/print.mko` | `checked-native` | formatting and output error tests |
| `reflect/reflect.mko` | `safe` | POD-only rejection and clone/equal tests |
| `regexp/regexp.mko` | `checked-native` | pattern bounds, UTF-8, catastrophic-input tests |
| `regexp/syntax/syntax.mko` | `safe` | quote/meta parser tests |
| `runtime/runtime.mko` | `unsafe-boundary` | stats only safe; raw runtime controls must stay explicit |
| `sctp/sctp.mko` | `checked-native` | kernel feature failure and handle lifecycle tests |
| `sip/sip.mko` | `checked-native` | header/body bounds and parser malformed tests |
| `slices/slices.mko` | `safe` | OOB clamp and ownership/drop tests |
| `sort/sort.mko` | `safe` | comparator and slice bounds tests |
| `strconv/strconv.mko` | `checked-native` | parse overflow and quote/unquote malformed tests |
| `strings/strings.mko` | `safe` | UTF-8/view/owned string tests |
| `sync/atomic/atomic.mko` | `checked-native` | atomic ordering and handle lifecycle tests |
| `sync/sync.mko` | `checked-native` | mutex/rwmutex/once lifecycle and contention tests |
| `syscall/syscall.mko` | `unsafe-boundary` | raw OS boundary; expose only checked safe wrappers by default |
| `testing/fstest/fstest.mko` | `safe` | mapfs path and overwrite tests |
| `testing/httptest/httptest.mko` | `checked-native` | listener lifecycle and request cleanup tests |
| `testing/iotest/iotest.mko` | `safe` | error reader/writer behavior tests |
| `testing/quick/quick.mko` | `safe` | seed determinism and predicate ownership tests |
| `testing/slogtest/slogtest.mko` | `safe` | redaction/shape assertions |
| `text/scanner/scanner.mko` | `checked-native` | malformed token and unicode bounds tests |
| `text/tabwriter/tabwriter.mko` | `checked-native` | width overflow and buffer growth tests |
| `text/template/template.mko` | `checked-native` | recursion, escaping, malformed template tests |
| `time/time.mko` | `checked-native` | parse overflow, timezone bounds, monotonic tests |
| `timer/timer.mko` | `checked-native` | heap bounds, cancel, stale timer tests |
| `unicode/unicode.mko` | `safe` | table bounds and case tests |
| `unicode/utf16/utf16.mko` | `safe` | surrogate and replacement tests |
| `unicode/utf8/utf8.mko` | `safe` | invalid sequence and bounds tests |
| `unique/unique.mko` | `checked-native` | intern table ownership and concurrency tests |
| `uuid/uuid.mko` | `checked-native` | parse/random bounds and native backend parity tests |

## Intentional exclusions for safe parity

| Surface | Decision | Reason |
|---------|----------|--------|
| Go `unsafe` | `won't` | raw memory conflicts with safe Mako |
| Go `go/*` | `blocked` | compiler/toolchain data only; reconsider bounded parsers case by case |
| Go `debug/*` | `blocked` | binary parsers need sandbox, bounds, and fuzzing first |
| Rust raw pointer APIs | `won't` | safe Mako does not expose raw pointer arithmetic |
| Rust unchecked indexing and uninitialized memory | `won't` | conflicts with checked indexing and definite initialization |
| Rust allocator internals / low-level FFI | `unsafe-boundary` | only narrow checked wrappers may enter the safe surface |

## Next implementation order

1. Fix current red `examples/testing` tests on C and native backends.
2. Add safety contracts to all `checked-native` and `unsafe-boundary` packages.
3. Add malformed-input tests for `encoding/*`, `compress/*`, `archive/*`,
   `image/*`, `regexp`, `graphql`, `grpc`, and protocol parsers.
4. Add handle lifecycle tests for `net/*`, `crypto/tls`, `database/sql`,
   `sync/*`, `timer`, `plugin`, `syscall`, `os/*`, and messaging packages.
5. Run sanitizer gates over the native-backed stdlib paths and record evidence
   in [STATUS.md](STATUS.md).
