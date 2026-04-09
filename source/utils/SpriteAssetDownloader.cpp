#include "utils/SpriteAssetDownloader.hpp"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <zlib.h>

#include <switch.h>

#include "pksmcore/utils/crypto.hpp"
#include "utils/Logger.hpp"

namespace pksm::utils {

namespace {

constexpr const char* SD_DATA_JSON = "sdmc:/switch/PKSM/assets/data.json";
constexpr const char* SD_ASSETS_DIR = "sdmc:/switch/PKSM/assets";
constexpr const char* SD_ASSETS_STAGING_DIR = "sdmc:/switch/PKSM/assets_staging";
constexpr const char* SD_ASSETS_BACKUP_DIR = "sdmc:/switch/PKSM/assets_backup_tmp";
constexpr const char* SD_SPRITES_PACK_TEMP = "sdmc:/switch/PKSM/assets/sprites-pack.zip.tmp";
constexpr const char* SD_SPRITES_PACK_MANIFEST = "sdmc:/switch/PKSM/assets/sprites-pack-manifest.json";
constexpr const char* SD_SPRITES_DIR = "sdmc:/switch/PKSM/assets/sprites";
constexpr const char* SD_KNOWN_MISSING_SPRITES = "sdmc:/switch/PKSM/assets/known_missing_sprites.txt";
constexpr const char* ROMFS_DATA_JSON = "romfs:/gfx/data/data.json";
constexpr const char* POKESPRITE_BASE = "https://cdn.jsdelivr.net/npm/pokesprite-images@2.7.0";
constexpr const char* POKESPRITE_RAW_BASE = "https://raw.githubusercontent.com/msikma/pokesprite/master";
constexpr const char* ZIP_PACK_URL = "https://github.com/Sala01/PKSM-Switch/releases/download/PRE-RELEASE/sprites-pack.zip";
constexpr const char* ZIP_MANIFEST_URL = "https://github.com/Sala01/PKSM-Switch/releases/download/PRE-RELEASE/sprites-pack-manifest.json";

struct UrlParts {
    bool https = true;
    std::string host;
    std::string path;
    std::string port;
};

struct HttpResponse {
    int statusCode = 0;
    std::unordered_map<std::string, std::string> headers;
    std::vector<u8> body;
};

struct ZipEntryRecord {
    std::string entryName;
    u16 flags = 0;
    u16 method = 0;
    u32 crc32 = 0;
    u32 compressedSize = 0;
    u32 uncompressedSize = 0;
    u32 localHeaderOffset = 0;
};

struct TargetZipEntry {
    ZipEntryRecord record;
    std::filesystem::path relativeOutput;
    bool isDataJson = false;
};

struct ZipPackManifest {
    std::optional<std::string> zipSha256;
    std::optional<std::string> dataJsonSha256;
    std::optional<std::size_t> spritesCount;
};

struct ZipExtractionSummary {
    std::vector<u8> dataJsonBytes;
    std::size_t extractedSprites = 0;
};

struct ZipPackApplyResult {
    bool attempted = false;
    bool succeeded = false;
    bool verified = false;
    std::size_t resolvedSprites = 0;
    std::string error;
};

u16 ReadLe16(const u8* ptr) {
    return static_cast<u16>(ptr[0] | (static_cast<u16>(ptr[1]) << 8));
}

u32 ReadLe32(const u8* ptr) {
    return static_cast<u32>(
        ptr[0]
        | (static_cast<u32>(ptr[1]) << 8)
        | (static_cast<u32>(ptr[2]) << 16)
        | (static_cast<u32>(ptr[3]) << 24)
    );
}

template <std::size_t N>
std::string BytesToHexLower(const std::array<u8, N>& bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(N * 2);
    for (std::size_t i = 0; i < N; ++i) {
        out[(i * 2) + 0] = kHex[(bytes[i] >> 4) & 0x0F];
        out[(i * 2) + 1] = kHex[bytes[i] & 0x0F];
    }
    return out;
}

std::string ToLower(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return v;
}

std::string Trim(const std::string& v) {
    const auto first = v.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = v.find_last_not_of(" \t\r\n");
    return v.substr(first, (last - first) + 1);
}

bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool ParseUrl(const std::string& url, UrlParts& out, std::string& err) {
    std::string working = url;
    if (StartsWith(working, "https://")) {
        out.https = true;
        working = working.substr(8);
    } else if (StartsWith(working, "http://")) {
        out.https = false;
        working = working.substr(7);
    } else {
        err = "Unsupported URL scheme";
        return false;
    }

    const auto slashPos = working.find('/');
    std::string hostPort = (slashPos == std::string::npos) ? working : working.substr(0, slashPos);
    out.path = (slashPos == std::string::npos) ? "/" : working.substr(slashPos);
    if (out.path.empty()) {
        out.path = "/";
    }

    const auto colonPos = hostPort.rfind(':');
    if (colonPos != std::string::npos && colonPos != 0 && colonPos + 1 < hostPort.size()) {
        out.host = hostPort.substr(0, colonPos);
        out.port = hostPort.substr(colonPos + 1);
    } else {
        out.host = hostPort;
        out.port = out.https ? "443" : "80";
    }

    if (out.host.empty()) {
        err = "URL host is empty";
        return false;
    }
    return true;
}

bool EnsureParentDir(const std::string& path) {
    try {
        std::filesystem::path p(path);
        const auto parent = p.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool WriteBinaryFile(const std::string& path, const std::vector<u8>& bytes, std::string& err) {
    if (!EnsureParentDir(path)) {
        err = "Failed to create output directory: " + path;
        return false;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.good()) {
        err = "Failed to open output file: " + path;
        return false;
    }

    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!out.good()) {
        err = "Failed writing output file: " + path;
        return false;
    }
    return true;
}

bool ReadBinaryFile(const std::string& path, std::vector<u8>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        return false;
    }
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    out.resize(size);
    if (size != 0) {
        in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
    }
    return in.good();
}

bool DecodeChunkedBody(const std::vector<u8>& in, std::vector<u8>& out) {
    std::size_t pos = 0;
    while (pos < in.size()) {
        std::size_t lineEnd = std::string::npos;
        for (std::size_t i = pos; i + 1 < in.size(); ++i) {
            if (in[i] == '\r' && in[i + 1] == '\n') {
                lineEnd = i;
                break;
            }
        }
        if (lineEnd == std::string::npos) {
            return false;
        }

        std::string sizeLine(reinterpret_cast<const char*>(in.data() + pos), lineEnd - pos);
        const auto semi = sizeLine.find(';');
        if (semi != std::string::npos) {
            sizeLine = sizeLine.substr(0, semi);
        }
        sizeLine = Trim(sizeLine);
        if (sizeLine.empty()) {
            return false;
        }

        std::size_t chunkSize = 0;
        try {
            chunkSize = static_cast<std::size_t>(std::stoull(sizeLine, nullptr, 16));
        } catch (...) {
            return false;
        }

        pos = lineEnd + 2;
        if (chunkSize == 0) {
            return true;
        }

        if (pos + chunkSize + 2 > in.size()) {
            return false;
        }

        out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(pos), in.begin() + static_cast<std::ptrdiff_t>(pos + chunkSize));
        pos += chunkSize;

        if (in[pos] != '\r' || in[pos + 1] != '\n') {
            return false;
        }
        pos += 2;
    }
    return false;
}

bool ParseHttpResponse(const std::vector<u8>& raw, HttpResponse& out, std::string& err) {
    const std::string marker = "\r\n\r\n";
    const auto it = std::search(raw.begin(), raw.end(), marker.begin(), marker.end());
    if (it == raw.end()) {
        err = "HTTP response header not found";
        return false;
    }

    const auto headerLen = static_cast<std::size_t>(std::distance(raw.begin(), it));
    const auto bodyOffset = headerLen + marker.size();

    std::string headerStr(reinterpret_cast<const char*>(raw.data()), headerLen);
    std::istringstream stream(headerStr);

    std::string statusLine;
    if (!std::getline(stream, statusLine)) {
        err = "HTTP status line missing";
        return false;
    }
    statusLine = Trim(statusLine);

    int statusCode = 0;
    {
        std::istringstream statusStream(statusLine);
        std::string version;
        statusStream >> version >> statusCode;
        if (statusCode <= 0) {
            err = "Invalid HTTP status line: " + statusLine;
            return false;
        }
    }
    out.statusCode = statusCode;

    std::string line;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.empty()) {
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const auto key = ToLower(Trim(line.substr(0, colon)));
        const auto value = Trim(line.substr(colon + 1));
        out.headers[key] = value;
    }

    std::vector<u8> body;
    body.insert(body.end(), raw.begin() + static_cast<std::ptrdiff_t>(bodyOffset), raw.end());

    const auto teIt = out.headers.find("transfer-encoding");
    if (teIt != out.headers.end() && ToLower(teIt->second).find("chunked") != std::string::npos) {
        std::vector<u8> decoded;
        if (!DecodeChunkedBody(body, decoded)) {
            err = "Failed to decode chunked HTTP body";
            return false;
        }
        out.body = std::move(decoded);
    } else {
        out.body = std::move(body);
    }

    return true;
}

class NetworkSession {
public:
    bool Start(std::string& err) {
        const auto alreadyInit = R_VALUE(MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized));

        Result rc = socketInitializeDefault();
        if (R_FAILED(rc) && R_VALUE(rc) != alreadyInit) {
            err = "socketInitializeDefault failed: 0x" + ToHex(rc);
            return false;
        }

        rc = sslInitialize(3);
        if (R_FAILED(rc) && R_VALUE(rc) != alreadyInit) {
            err = "sslInitialize failed: 0x" + ToHex(rc);
            return false;
        }
        return true;
    }

    ~NetworkSession() {
        // Keep SSL and BSD sockets alive for app lifetime to avoid dropping nxlink stdio.
    }

private:
    static std::string ToHex(Result rc) {
        std::ostringstream ss;
        ss << std::hex << static_cast<u32>(rc);
        return ss.str();
    }

};

int ConnectTcpSocket(const UrlParts& url, std::string& err) {
    auto configureTimeouts = [](int sockfd) {
        timeval tv{};
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
            return false;
        }
        if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) {
            return false;
        }
        return true;
    };

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;

    addrinfo* result = nullptr;
    const int gai = getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &result);
    if (gai != 0 || result == nullptr) {
        err = "getaddrinfo failed for " + url.host;
        return -1;
    }

    int sockfd = -1;
    for (addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
        sockfd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0) {
            continue;
        }

        if (::connect(sockfd, rp->ai_addr, static_cast<socklen_t>(rp->ai_addrlen)) == 0) {
            if (!configureTimeouts(sockfd)) {
                ::close(sockfd);
                sockfd = -1;
                continue;
            }
            break;
        }

        ::close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(result);

    if (sockfd < 0) {
        err = "connect failed for " + url.host;
    }
    return sockfd;
}

bool SendAllPlain(int sockfd, const std::string& req, std::string& err) {
    std::size_t sent = 0;
    while (sent < req.size()) {
        const ssize_t n = ::send(sockfd, req.data() + static_cast<std::ptrdiff_t>(sent), req.size() - sent, 0);
        if (n <= 0) {
            err = "send failed";
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool RecvAllPlain(
    int sockfd,
    std::vector<u8>& out,
    std::string& err,
    const std::function<void(std::size_t)>& onBytesReceived = nullptr
) {
    std::array<u8, 0x4000> buf{};
    while (true) {
        const ssize_t n = ::recv(sockfd, buf.data(), buf.size(), 0);
        if (n == 0) {
            break;
        }
        if (n < 0) {
            err = "recv failed";
            return false;
        }
        out.insert(out.end(), buf.begin(), buf.begin() + n);
        if (onBytesReceived) {
            onBytesReceived(out.size());
        }
    }
    return true;
}

bool SendAllTls(SslConnection& conn, const std::string& req, std::string& err) {
    std::size_t sent = 0;
    while (sent < req.size()) {
        u32 chunk = 0;
        const auto remaining = req.size() - sent;
        const u32 writeLen = static_cast<u32>(std::min<std::size_t>(remaining, 0x4000));
        const Result rc = sslConnectionWrite(&conn, req.data() + sent, writeLen, &chunk);
        if (R_FAILED(rc) || chunk == 0) {
            std::ostringstream ss;
            ss << "ssl write failed: 0x" << std::hex << static_cast<u32>(rc);
            err = ss.str();
            return false;
        }
        sent += chunk;
    }
    return true;
}

bool RecvAllTls(
    SslConnection& conn,
    std::vector<u8>& out,
    std::string& err,
    const std::function<void(std::size_t)>& onBytesReceived = nullptr
) {
    std::array<u8, 0x4000> buf{};
    while (true) {
        u32 read = 0;
        const Result rc = sslConnectionRead(&conn, buf.data(), static_cast<u32>(buf.size()), &read);
        if (R_FAILED(rc)) {
            std::ostringstream ss;
            ss << "ssl read failed: 0x" << std::hex << static_cast<u32>(rc);
            err = ss.str();
            return false;
        }
        if (read == 0) {
            break;
        }
        out.insert(out.end(), buf.begin(), buf.begin() + read);
        if (onBytesReceived) {
            onBytesReceived(out.size());
        }
    }
    return true;
}

bool HttpGet(
    const std::string& url,
    HttpResponse& out,
    std::string& err,
    int redirectDepth = 0,
    const std::function<void(std::size_t, std::size_t)>& onDownloadProgress = nullptr
) {
    if (redirectDepth > 5) {
        err = "Too many HTTP redirects";
        return false;
    }

    UrlParts parts;
    if (!ParseUrl(url, parts, err)) {
        return false;
    }

    const int sockfd = ConnectTcpSocket(parts, err);
    if (sockfd < 0) {
        return false;
    }

    SslContext sslContext{};
    SslConnection sslConn{};
    bool tlsReady = false;

    auto closeSocket = [&]() {
        if (sockfd >= 0) {
            ::close(sockfd);
        }
    };

    auto closeTls = [&]() {
        if (tlsReady) {
            sslConnectionClose(&sslConn);
            sslContextClose(&sslContext);
            tlsReady = false;
        }
    };

    if (parts.https) {
        Result rc = sslCreateContext(&sslContext, SslVersion_Auto);
        if (R_FAILED(rc)) {
            std::ostringstream ss;
            ss << "sslCreateContext failed: 0x" << std::hex << static_cast<u32>(rc);
            err = ss.str();
            closeSocket();
            return false;
        }

        rc = sslContextCreateConnection(&sslContext, &sslConn);
        if (R_FAILED(rc)) {
            std::ostringstream ss;
            ss << "sslContextCreateConnection failed: 0x" << std::hex << static_cast<u32>(rc);
            err = ss.str();
            sslContextClose(&sslContext);
            closeSocket();
            return false;
        }
        tlsReady = true;

        rc = sslConnectionSetHostName(&sslConn, parts.host.c_str(), static_cast<u32>(parts.host.size() + 1));
        if (R_FAILED(rc)) {
            std::ostringstream ss;
            ss << "sslConnectionSetHostName failed: 0x" << std::hex << static_cast<u32>(rc);
            err = ss.str();
            closeTls();
            closeSocket();
            return false;
        }

        rc = sslConnectionSetOption(&sslConn, SslOptionType_DoNotCloseSocket, true);
        if (R_FAILED(rc)) {
            std::ostringstream ss;
            ss << "sslConnectionSetOption(DoNotCloseSocket) failed: 0x" << std::hex << static_cast<u32>(rc);
            err = ss.str();
            closeTls();
            closeSocket();
            return false;
        }

        const int outSock = socketSslConnectionSetSocketDescriptor(&sslConn, sockfd);
        if (outSock < 0 && errno != ENOENT) {
            err = "socketSslConnectionSetSocketDescriptor failed";
            closeTls();
            closeSocket();
            return false;
        }

        rc = sslConnectionDoHandshake(&sslConn, nullptr, nullptr, nullptr, 0);
        if (R_FAILED(rc)) {
            std::ostringstream ss;
            ss << "TLS handshake failed: 0x" << std::hex << static_cast<u32>(rc);
            err = ss.str();
            closeTls();
            closeSocket();
            return false;
        }
    }

    const std::string request =
        "GET " + parts.path + " HTTP/1.1\r\n"
        "Host: " + parts.host + "\r\n"
        "User-Agent: PKSM-Switch\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n\r\n";

    std::vector<u8> raw;

    bool headerParsed = false;
    std::size_t bodyOffset = 0;
    std::optional<std::size_t> contentLength;

    auto publishDownloadProgress = [&]() {
        if (!onDownloadProgress) {
            return;
        }

        if (!headerParsed) {
            static const std::string marker = "\r\n\r\n";
            const auto headerEnd = std::search(raw.begin(), raw.end(), marker.begin(), marker.end());
            if (headerEnd != raw.end()) {
                headerParsed = true;
                const std::size_t headerLen = static_cast<std::size_t>(std::distance(raw.begin(), headerEnd));
                bodyOffset = headerLen + marker.size();

                std::string headerText(reinterpret_cast<const char*>(raw.data()), headerLen);
                std::istringstream stream(headerText);
                std::string line;
                std::getline(stream, line); // status line
                while (std::getline(stream, line)) {
                    line = Trim(line);
                    if (line.empty()) {
                        continue;
                    }
                    const auto colon = line.find(':');
                    if (colon == std::string::npos) {
                        continue;
                    }
                    const std::string key = ToLower(Trim(line.substr(0, colon)));
                    if (key != "content-length") {
                        continue;
                    }

                    const std::string value = Trim(line.substr(colon + 1));
                    try {
                        contentLength = static_cast<std::size_t>(std::stoull(value));
                    } catch (...) {
                        contentLength.reset();
                    }
                    break;
                }
            }
        }

        if (!headerParsed || raw.size() < bodyOffset) {
            onDownloadProgress(0, contentLength.value_or(0));
            return;
        }

        const std::size_t received = raw.size() - bodyOffset;
        onDownloadProgress(received, contentLength.value_or(0));
    };

    bool ioOk = false;
    if (parts.https) {
        ioOk = SendAllTls(sslConn, request, err)
            && RecvAllTls(sslConn, raw, err, [&publishDownloadProgress](std::size_t) {
                publishDownloadProgress();
            });
    } else {
        ioOk = SendAllPlain(sockfd, request, err)
            && RecvAllPlain(sockfd, raw, err, [&publishDownloadProgress](std::size_t) {
                publishDownloadProgress();
            });
    }

    closeTls();
    closeSocket();

    if (!ioOk) {
        return false;
    }

    publishDownloadProgress();

    HttpResponse response;
    if (!ParseHttpResponse(raw, response, err)) {
        return false;
    }

    if (onDownloadProgress) {
        onDownloadProgress(response.body.size(), response.body.size());
    }

    if (response.statusCode == 301 || response.statusCode == 302 || response.statusCode == 307 || response.statusCode == 308) {
        const auto it = response.headers.find("location");
        if (it == response.headers.end() || it->second.empty()) {
            err = "HTTP redirect without location";
            return false;
        }

        std::string redirectUrl = it->second;
        if (StartsWith(redirectUrl, "/")) {
            redirectUrl = (parts.https ? "https://" : "http://") + parts.host + redirectUrl;
        }
        return HttpGet(redirectUrl, out, err, redirectDepth + 1, onDownloadProgress);
    }

    out = std::move(response);
    return true;
}

std::string NormalizeCdnBase(const std::string& raw) {
    if (raw.empty()) {
        return "https://cdn.sigkill.tech";
    }
    std::string out = raw;
    while (!out.empty() && out.back() == '/') {
        out.pop_back();
    }
    return out;
}

std::optional<std::string> BuildPokespriteUrlFromBase(const char* base, const std::string& filename) {
    if (!EndsWith(filename, ".png")) {
        return std::nullopt;
    }

    bool shiny = false;
    std::string baseName = filename;

    if (EndsWith(baseName, "_shiny.png")) {
        shiny = true;
        baseName = baseName.substr(0, baseName.size() - std::strlen("_shiny.png")) + ".png";
    }

    if (baseName.empty()) {
        return std::nullopt;
    }

    const std::string folder = shiny ? "shiny" : "regular";
    return std::string(base) + "/pokemon-gen8/" + folder + "/" + baseName;
}

std::optional<std::string> BuildPokespriteFallbackUrl(const std::string& filename) {
    return BuildPokespriteUrlFromBase(POKESPRITE_BASE, filename);
}

std::optional<std::string> BuildPokespriteRawFallbackUrl(const std::string& filename) {
    return BuildPokespriteUrlFromBase(POKESPRITE_RAW_BASE, filename);
}

bool FileExistsAndNotEmpty(const std::filesystem::path& p) {
    try {
        return std::filesystem::exists(p) && std::filesystem::is_regular_file(p) && std::filesystem::file_size(p) > 0;
    } catch (...) {
        return false;
    }
}

std::unordered_set<std::string> LoadKnownMissingSprites() {
    std::unordered_set<std::string> knownMissing;

    std::ifstream in(SD_KNOWN_MISSING_SPRITES);
    if (!in.good()) {
        return knownMissing;
    }

    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (!line.empty()) {
            knownMissing.insert(line);
        }
    }

    return knownMissing;
}

bool SaveKnownMissingSprites(const std::unordered_set<std::string>& knownMissing, std::string& err) {
    if (!EnsureParentDir(SD_KNOWN_MISSING_SPRITES)) {
        err = "Failed to create known-missing cache directory";
        return false;
    }

    std::vector<std::string> ordered(knownMissing.begin(), knownMissing.end());
    std::sort(ordered.begin(), ordered.end());

    std::ofstream out(SD_KNOWN_MISSING_SPRITES, std::ios::trunc);
    if (!out.good()) {
        err = "Failed to open known-missing cache file";
        return false;
    }

    for (const auto& entry : ordered) {
        out << entry << '\n';
    }

    if (!out.good()) {
        err = "Failed writing known-missing cache file";
        return false;
    }

    return true;
}

bool BuildMissingSpriteListFromData(
    const std::vector<u8>& dataJsonBytes,
    std::vector<std::string>& outMissing,
    std::size_t& outPresent,
    std::string& err
);

std::string NormalizeHexString(std::string value) {
    value = Trim(ToLower(value));
    if (StartsWith(value, "0x")) {
        value = value.substr(2);
    }
    return value;
}

bool IsValidHexDigest(const std::string& value, std::size_t expectedLen) {
    if (value.size() != expectedLen) {
        return false;
    }
    for (const unsigned char c : value) {
        if (!std::isxdigit(c)) {
            return false;
        }
    }
    return true;
}

const nlohmann::json* FindManifestValue(const nlohmann::json& parsed, const std::initializer_list<const char*>& keys) {
    for (const auto* key : keys) {
        if (parsed.contains(key)) {
            return &parsed.at(key);
        }
    }

    for (auto it = parsed.begin(); it != parsed.end(); ++it) {
        const std::string loweredKey = ToLower(it.key());
        for (const auto* key : keys) {
            if (loweredKey == ToLower(key)) {
                return &it.value();
            }
        }
    }

    return nullptr;
}

bool ParseZipPackManifest(const std::vector<u8>& bytes, ZipPackManifest& outManifest, std::string& err) {
    if (bytes.empty()) {
        err = "Manifest payload is empty";
        return false;
    }

    std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        err = "Manifest is not a valid JSON object";
        return false;
    }

    if (const auto* value = FindManifestValue(parsed, {"zip_sha256", "pack_sha256", "sprites_pack_sha256", "sha256"}); value && value->is_string()) {
        const std::string hash = NormalizeHexString(value->get<std::string>());
        if (!IsValidHexDigest(hash, 64)) {
            err = "Manifest zip hash is invalid";
            return false;
        }
        outManifest.zipSha256 = hash;
    }

    if (const auto* value = FindManifestValue(parsed, {"data_json_sha256", "data_sha256"}); value && value->is_string()) {
        const std::string hash = NormalizeHexString(value->get<std::string>());
        if (!IsValidHexDigest(hash, 64)) {
            err = "Manifest data.json hash is invalid";
            return false;
        }
        outManifest.dataJsonSha256 = hash;
    }

    if (const auto* value = FindManifestValue(parsed, {"sprites_count", "sprite_count", "count"}); value) {
        if (value->is_number_unsigned()) {
            outManifest.spritesCount = value->get<std::size_t>();
        } else if (value->is_number_integer()) {
            const auto signedCount = value->get<long long>();
            if (signedCount < 0) {
                err = "Manifest sprites_count cannot be negative";
                return false;
            }
            outManifest.spritesCount = static_cast<std::size_t>(signedCount);
        } else if (value->is_string()) {
            try {
                outManifest.spritesCount = static_cast<std::size_t>(std::stoull(value->get<std::string>()));
            } catch (...) {
                err = "Manifest sprites_count is not numeric";
                return false;
            }
        }
    }

    return true;
}

std::string ComputeSha256Hex(const std::vector<u8>& bytes) {
    return BytesToHexLower(pksm::crypto::sha256(bytes));
}

std::string NormalizeZipEntryName(std::string entryName) {
    std::replace(entryName.begin(), entryName.end(), '\\', '/');
    while (StartsWith(entryName, "./")) {
        entryName = entryName.substr(2);
    }
    while (!entryName.empty() && entryName.front() == '/') {
        entryName.erase(entryName.begin());
    }

    const std::string lowered = ToLower(entryName);
    if (StartsWith(lowered, "assets/")) {
        entryName = entryName.substr(std::strlen("assets/"));
    }
    return entryName;
}

bool IsSafeZipRelativePath(const std::string& relativePath) {
    if (relativePath.empty()) {
        return false;
    }

    std::size_t segmentStart = 0;
    while (segmentStart < relativePath.size()) {
        const auto slash = relativePath.find('/', segmentStart);
        const std::size_t segmentEnd = (slash == std::string::npos) ? relativePath.size() : slash;
        const std::string segment = relativePath.substr(segmentStart, segmentEnd - segmentStart);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        if (slash == std::string::npos) {
            break;
        }
        segmentStart = slash + 1;
    }

    return true;
}

bool ParseZipCentralDirectory(const std::vector<u8>& zipBytes, std::vector<ZipEntryRecord>& outEntries, std::string& err) {
    constexpr std::size_t kEocdMinSize = 22;
    constexpr u32 kEocdSignature = 0x06054B50;
    constexpr u32 kCentralFileHeaderSignature = 0x02014B50;

    outEntries.clear();

    if (zipBytes.size() < kEocdMinSize) {
        err = "ZIP payload is too small";
        return false;
    }

    const std::size_t searchStart = (zipBytes.size() > (0xFFFF + kEocdMinSize))
        ? (zipBytes.size() - (0xFFFF + kEocdMinSize))
        : 0;

    std::optional<std::size_t> eocdOffset;
    for (std::size_t pos = zipBytes.size() - kEocdMinSize;; --pos) {
        if (ReadLe32(zipBytes.data() + pos) == kEocdSignature) {
            eocdOffset = pos;
            break;
        }
        if (pos == searchStart) {
            break;
        }
    }

    if (!eocdOffset.has_value()) {
        err = "ZIP end-of-central-directory record not found";
        return false;
    }

    const u8* eocd = zipBytes.data() + *eocdOffset;
    const u16 diskNumber = ReadLe16(eocd + 4);
    const u16 centralDisk = ReadLe16(eocd + 6);
    const u16 entriesOnDisk = ReadLe16(eocd + 8);
    const u16 entriesTotal = ReadLe16(eocd + 10);
    const u32 centralSize = ReadLe32(eocd + 12);
    const u32 centralOffset = ReadLe32(eocd + 16);
    const u16 commentLength = ReadLe16(eocd + 20);

    if (*eocdOffset + kEocdMinSize + commentLength > zipBytes.size()) {
        err = "ZIP EOCD comment length is invalid";
        return false;
    }

    if (diskNumber != 0 || centralDisk != 0 || entriesOnDisk != entriesTotal) {
        err = "ZIP multi-disk archives are not supported";
        return false;
    }

    if (entriesTotal == 0xFFFF || centralSize == 0xFFFFFFFFU || centralOffset == 0xFFFFFFFFU) {
        err = "ZIP64 archives are not supported";
        return false;
    }

    const std::size_t centralStart = static_cast<std::size_t>(centralOffset);
    const std::size_t centralEnd = centralStart + static_cast<std::size_t>(centralSize);
    if (centralStart >= zipBytes.size() || centralEnd > zipBytes.size()) {
        err = "ZIP central directory range is invalid";
        return false;
    }

    std::size_t cursor = centralStart;
    outEntries.reserve(entriesTotal);
    for (u16 i = 0; i < entriesTotal; ++i) {
        if (cursor + 46 > zipBytes.size()) {
            err = "ZIP central directory entry is truncated";
            return false;
        }

        const u8* header = zipBytes.data() + cursor;
        if (ReadLe32(header) != kCentralFileHeaderSignature) {
            err = "ZIP central directory signature mismatch";
            return false;
        }

        const u16 fileNameLen = ReadLe16(header + 28);
        const u16 extraLen = ReadLe16(header + 30);
        const u16 commentLen = ReadLe16(header + 32);

        const std::size_t nameOffset = cursor + 46;
        const std::size_t nextEntry = nameOffset + static_cast<std::size_t>(fileNameLen)
            + static_cast<std::size_t>(extraLen)
            + static_cast<std::size_t>(commentLen);
        if (nextEntry > zipBytes.size()) {
            err = "ZIP central directory entry overflows payload";
            return false;
        }

        ZipEntryRecord record;
        record.flags = ReadLe16(header + 8);
        record.method = ReadLe16(header + 10);
        record.crc32 = ReadLe32(header + 16);
        record.compressedSize = ReadLe32(header + 20);
        record.uncompressedSize = ReadLe32(header + 24);
        record.localHeaderOffset = ReadLe32(header + 42);
        record.entryName.assign(
            reinterpret_cast<const char*>(zipBytes.data() + nameOffset),
            static_cast<std::size_t>(fileNameLen)
        );

        outEntries.push_back(std::move(record));
        cursor = nextEntry;
    }

    if (cursor > centralEnd) {
        err = "ZIP central directory cursor overflow";
        return false;
    }

    return true;
}

bool BuildTargetZipEntries(const std::vector<ZipEntryRecord>& records, std::vector<TargetZipEntry>& outTargets, std::string& err) {
    outTargets.clear();

    bool hasDataJson = false;
    std::size_t spriteEntries = 0;
    std::unordered_set<std::string> seenOutputs;

    for (const auto& record : records) {
        std::string entryName = NormalizeZipEntryName(record.entryName);
        if (entryName.empty() || EndsWith(entryName, "/")) {
            continue;
        }

        if (!IsSafeZipRelativePath(entryName)) {
            continue;
        }

        const std::string loweredEntry = ToLower(entryName);
        TargetZipEntry target;
        bool isTarget = false;

        if (loweredEntry == "data.json") {
            target.relativeOutput = std::filesystem::path("data.json");
            target.isDataJson = true;
            isTarget = true;
        } else if (StartsWith(loweredEntry, "sprites/")) {
            const std::string spriteName = entryName.substr(std::strlen("sprites/"));
            if (spriteName.empty() || spriteName.find('/') != std::string::npos) {
                continue;
            }
            if (!EndsWith(ToLower(spriteName), ".png")) {
                continue;
            }

            target.relativeOutput = std::filesystem::path("sprites") / spriteName;
            target.isDataJson = false;
            isTarget = true;
        }

        if (!isTarget) {
            continue;
        }

        if ((record.flags & 0x0001U) != 0U) {
            err = "ZIP encryption is not supported";
            return false;
        }

        if (record.method != 0 && record.method != 8) {
            err = "ZIP compression method is not supported";
            return false;
        }

        const std::string dedupeKey = ToLower(target.relativeOutput.generic_string());
        if (!seenOutputs.insert(dedupeKey).second) {
            continue;
        }

        target.record = record;
        outTargets.push_back(std::move(target));

        if (outTargets.back().isDataJson) {
            hasDataJson = true;
        } else {
            ++spriteEntries;
        }
    }

    if (!hasDataJson) {
        err = "ZIP pack does not contain data.json";
        return false;
    }
    if (spriteEntries == 0) {
        err = "ZIP pack does not contain any sprite files";
        return false;
    }

    return true;
}

bool InflateRawDeflate(
    const u8* compressedData,
    std::size_t compressedSize,
    std::size_t expectedSize,
    std::vector<u8>& out,
    std::string& err
) {
    if (compressedSize > std::numeric_limits<uInt>::max()) {
        err = "Compressed ZIP entry is too large";
        return false;
    }

    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressedData));
    stream.avail_in = static_cast<uInt>(compressedSize);

    const int initRc = inflateInit2(&stream, -MAX_WBITS);
    if (initRc != Z_OK) {
        err = "Failed to initialize zlib inflater";
        return false;
    }

    std::array<u8, 16 * 1024> chunk{};
    out.clear();
    if (expectedSize > 0) {
        out.reserve(expectedSize);
    }

    int rc = Z_OK;
    while (rc != Z_STREAM_END) {
        stream.next_out = chunk.data();
        stream.avail_out = static_cast<uInt>(chunk.size());

        rc = inflate(&stream, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&stream);
            err = "zlib inflate failed with code " + std::to_string(rc);
            return false;
        }

        const std::size_t produced = chunk.size() - static_cast<std::size_t>(stream.avail_out);
        out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(produced));

        if (expectedSize > 0 && out.size() > expectedSize) {
            inflateEnd(&stream);
            err = "Inflated ZIP entry exceeds expected size";
            return false;
        }

        if (stream.avail_in == 0 && produced == 0 && rc != Z_STREAM_END) {
            inflateEnd(&stream);
            err = "Inflated ZIP entry ended unexpectedly";
            return false;
        }
    }

    inflateEnd(&stream);

    if (expectedSize > 0 && out.size() != expectedSize) {
        err = "Inflated ZIP entry size mismatch";
        return false;
    }

    return true;
}

bool ExtractZipEntryPayload(const std::vector<u8>& zipBytes, const ZipEntryRecord& record, std::vector<u8>& out, std::string& err) {
    constexpr u32 kLocalHeaderSignature = 0x04034B50;

    const std::size_t localHeader = static_cast<std::size_t>(record.localHeaderOffset);
    if (localHeader + 30 > zipBytes.size()) {
        err = "ZIP local header is out of range";
        return false;
    }

    const u8* header = zipBytes.data() + localHeader;
    if (ReadLe32(header) != kLocalHeaderSignature) {
        err = "ZIP local header signature mismatch";
        return false;
    }

    const std::size_t fileNameLen = static_cast<std::size_t>(ReadLe16(header + 26));
    const std::size_t extraLen = static_cast<std::size_t>(ReadLe16(header + 28));
    const std::size_t payloadOffset = localHeader + 30 + fileNameLen + extraLen;
    const std::size_t compressedSize = static_cast<std::size_t>(record.compressedSize);

    if (payloadOffset > zipBytes.size() || compressedSize > (zipBytes.size() - payloadOffset)) {
        err = "ZIP entry payload range is invalid";
        return false;
    }

    const u8* compressed = zipBytes.data() + payloadOffset;
    const std::size_t expectedSize = static_cast<std::size_t>(record.uncompressedSize);

    if (record.method == 0) {
        out.assign(compressed, compressed + compressedSize);
        if (out.size() != expectedSize) {
            err = "Stored ZIP entry size mismatch";
            return false;
        }
    } else if (record.method == 8) {
        if (!InflateRawDeflate(compressed, compressedSize, expectedSize, out, err)) {
            return false;
        }
    } else {
        err = "Unsupported ZIP compression method";
        return false;
    }

    if (out.size() > std::numeric_limits<uInt>::max()) {
        err = "ZIP entry is too large for CRC verification";
        return false;
    }

    uLong computedCrc = crc32(0L, Z_NULL, 0);
    if (!out.empty()) {
        computedCrc = crc32(computedCrc, out.data(), static_cast<uInt>(out.size()));
    }

    if (static_cast<u32>(computedCrc) != record.crc32) {
        err = "ZIP entry CRC mismatch";
        return false;
    }

    return true;
}

bool ExtractZipPackToStaging(
    const std::vector<u8>& zipBytes,
    const std::function<void(const SpriteAssetDownloader::ProgressInfo&)>& onProgress,
    ZipExtractionSummary& outSummary,
    std::string& err
) {
    std::error_code fsErr;
    std::filesystem::remove_all(SD_ASSETS_STAGING_DIR, fsErr);
    fsErr.clear();

    std::filesystem::create_directories(SD_ASSETS_STAGING_DIR, fsErr);
    if (fsErr) {
        err = "Failed to create staging directory: " + fsErr.message();
        return false;
    }

    std::vector<ZipEntryRecord> records;
    if (!ParseZipCentralDirectory(zipBytes, records, err)) {
        return false;
    }

    std::vector<TargetZipEntry> targets;
    if (!BuildTargetZipEntries(records, targets, err)) {
        return false;
    }

    if (onProgress) {
        SpriteAssetDownloader::ProgressInfo info;
        info.phase = SpriteAssetDownloader::ProgressInfo::Phase::ZipExtract;
        info.usedZip = true;
        info.stageCurrent = 0;
        info.stageTotal = targets.size();
        onProgress(info);
    }

    outSummary = {};

    const auto stagingRoot = std::filesystem::path(SD_ASSETS_STAGING_DIR);
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const auto& target = targets[i];

        std::vector<u8> payload;
        if (!ExtractZipEntryPayload(zipBytes, target.record, payload, err)) {
            return false;
        }

        std::string writeErr;
        if (!WriteBinaryFile((stagingRoot / target.relativeOutput).string(), payload, writeErr)) {
            err = writeErr;
            return false;
        }

        if (target.isDataJson) {
            outSummary.dataJsonBytes = std::move(payload);
        } else {
            ++outSummary.extractedSprites;
        }

        if (onProgress) {
            SpriteAssetDownloader::ProgressInfo info;
            info.phase = SpriteAssetDownloader::ProgressInfo::Phase::ZipExtract;
            info.usedZip = true;
            info.stageCurrent = i + 1;
            info.stageTotal = targets.size();
            onProgress(info);
        }
    }

    if (outSummary.dataJsonBytes.empty()) {
        err = "ZIP pack extracted an empty data.json";
        return false;
    }

    if (outSummary.extractedSprites == 0) {
        err = "ZIP pack extracted zero sprites";
        return false;
    }

    std::vector<std::string> ignoredMissing;
    std::size_t ignoredPresent = 0;
    std::string parseErr;
    if (!BuildMissingSpriteListFromData(outSummary.dataJsonBytes, ignoredMissing, ignoredPresent, parseErr)) {
        err = "Extracted data.json is invalid: " + parseErr;
        return false;
    }

    return true;
}

bool CommitStagedAssets(std::string& err) {
    const auto stagingRoot = std::filesystem::path(SD_ASSETS_STAGING_DIR);
    const auto stagingData = stagingRoot / "data.json";
    const auto stagingSprites = stagingRoot / "sprites";

    std::error_code fsErr;
    if (!std::filesystem::exists(stagingData, fsErr) || fsErr) {
        err = "Staging data.json is missing";
        return false;
    }
    if (!std::filesystem::exists(stagingSprites, fsErr) || fsErr) {
        err = "Staging sprites directory is missing";
        return false;
    }

    const auto assetsRoot = std::filesystem::path(SD_ASSETS_DIR);
    const auto liveData = assetsRoot / "data.json";
    const auto liveSprites = assetsRoot / "sprites";

    const auto backupRoot = std::filesystem::path(SD_ASSETS_BACKUP_DIR);
    const auto backupData = backupRoot / "data.json";
    const auto backupSprites = backupRoot / "sprites";

    std::filesystem::remove_all(backupRoot, fsErr);
    if (fsErr) {
        err = "Failed clearing backup directory: " + fsErr.message();
        return false;
    }

    std::filesystem::create_directories(backupRoot, fsErr);
    if (fsErr) {
        err = "Failed creating backup directory: " + fsErr.message();
        return false;
    }

    std::filesystem::create_directories(assetsRoot, fsErr);
    if (fsErr) {
        err = "Failed creating assets directory: " + fsErr.message();
        return false;
    }

    bool movedOldData = false;
    bool movedOldSprites = false;
    bool installedNewData = false;
    bool installedNewSprites = false;

    auto rollback = [&]() {
        std::error_code rollbackErr;

        if (installedNewData) {
            std::filesystem::remove(liveData, rollbackErr);
            rollbackErr.clear();
        }
        if (installedNewSprites) {
            std::filesystem::remove_all(liveSprites, rollbackErr);
            rollbackErr.clear();
        }

        if (movedOldData && std::filesystem::exists(backupData, rollbackErr) && !rollbackErr) {
            std::filesystem::rename(backupData, liveData, rollbackErr);
            rollbackErr.clear();
        }
        if (movedOldSprites && std::filesystem::exists(backupSprites, rollbackErr) && !rollbackErr) {
            std::filesystem::rename(backupSprites, liveSprites, rollbackErr);
            rollbackErr.clear();
        }
    };

    if (std::filesystem::exists(liveData, fsErr) && !fsErr) {
        std::filesystem::rename(liveData, backupData, fsErr);
        if (fsErr) {
            err = "Failed backing up current data.json: " + fsErr.message();
            return false;
        }
        movedOldData = true;
    }

    fsErr.clear();
    if (std::filesystem::exists(liveSprites, fsErr) && !fsErr) {
        std::filesystem::rename(liveSprites, backupSprites, fsErr);
        if (fsErr) {
            rollback();
            err = "Failed backing up current sprites directory: " + fsErr.message();
            return false;
        }
        movedOldSprites = true;
    }

    fsErr.clear();
    std::filesystem::rename(stagingData, liveData, fsErr);
    if (fsErr) {
        rollback();
        err = "Failed installing staged data.json: " + fsErr.message();
        return false;
    }
    installedNewData = true;

    fsErr.clear();
    std::filesystem::rename(stagingSprites, liveSprites, fsErr);
    if (fsErr) {
        rollback();
        err = "Failed installing staged sprites directory: " + fsErr.message();
        return false;
    }
    installedNewSprites = true;

    std::filesystem::remove_all(stagingRoot, fsErr);
    fsErr.clear();
    std::filesystem::remove_all(backupRoot, fsErr);
    fsErr.clear();
    std::filesystem::remove(SD_SPRITES_PACK_TEMP, fsErr);

    return true;
}

ZipPackApplyResult TryApplyZipPack(
    const std::string& normalizedBase,
    const std::vector<std::string>& missingCandidates,
    const std::function<void(const SpriteAssetDownloader::ProgressInfo&)>& onProgress
) {
    ZipPackApplyResult result;

    auto cleanupTemporary = []() {
        std::error_code fsErr;
        std::filesystem::remove_all(SD_ASSETS_STAGING_DIR, fsErr);
        fsErr.clear();
        std::filesystem::remove_all(SD_ASSETS_BACKUP_DIR, fsErr);
        fsErr.clear();
        std::filesystem::remove(SD_SPRITES_PACK_TEMP, fsErr);
    };

    cleanupTemporary();

    ZipPackManifest manifest;
    std::vector<u8> manifestBytes;
    bool manifestParsed = false;

    {
        HttpResponse manifestResp;
        std::string manifestErr;
        const std::string manifestUrl = ZIP_MANIFEST_URL;
        if (HttpGet(manifestUrl, manifestResp, manifestErr) && manifestResp.statusCode == 200 && !manifestResp.body.empty()) {
            manifestBytes = manifestResp.body;
            std::string parseErr;
            if (!ParseZipPackManifest(manifestBytes, manifest, parseErr)) {
                LOG_WARNING("SpriteAssetDownloader: zip manifest ignored: " + parseErr);
            } else {
                manifestParsed = true;
            }
        }
    }

    HttpResponse zipResp;
    std::string zipErr;
    const std::string zipUrl = ZIP_PACK_URL;
    result.attempted = true;

    auto publishDownload = [&](std::size_t received, std::size_t total) {
        if (!onProgress) {
            return;
        }
        SpriteAssetDownloader::ProgressInfo info;
        info.phase = SpriteAssetDownloader::ProgressInfo::Phase::ZipDownload;
        info.usedZip = true;
        info.stageCurrent = received;
        info.stageTotal = total;
        onProgress(info);
    };

    const bool zipOk = HttpGet(zipUrl, zipResp, zipErr, 0, publishDownload);
    if (!zipOk || zipResp.statusCode != 200 || zipResp.body.empty()) {
        if (zipErr.empty()) {
            result.error = "ZIP download failed (status " + std::to_string(zipResp.statusCode) + ")";
        } else {
            result.error = zipErr;
        }
        cleanupTemporary();
        return result;
    }

    std::string writeErr;
    if (!WriteBinaryFile(SD_SPRITES_PACK_TEMP, zipResp.body, writeErr)) {
        result.error = writeErr;
        cleanupTemporary();
        return result;
    }

    if (manifestParsed && manifest.zipSha256.has_value()) {
        const std::string actualHash = ComputeSha256Hex(zipResp.body);
        if (actualHash != *manifest.zipSha256) {
            result.error = "ZIP hash mismatch";
            cleanupTemporary();
            return result;
        }
    }

    ZipExtractionSummary summary;
    std::string extractErr;
    if (!ExtractZipPackToStaging(zipResp.body, onProgress, summary, extractErr)) {
        result.error = extractErr;
        cleanupTemporary();
        return result;
    }

    if (manifestParsed && manifest.dataJsonSha256.has_value()) {
        const std::string actualHash = ComputeSha256Hex(summary.dataJsonBytes);
        if (actualHash != *manifest.dataJsonSha256) {
            result.error = "Extracted data.json hash mismatch";
            cleanupTemporary();
            return result;
        }
    }

    if (manifestParsed && manifest.spritesCount.has_value() && *manifest.spritesCount != summary.extractedSprites) {
        result.error = "Extracted sprites count does not match manifest";
        cleanupTemporary();
        return result;
    }

    std::string commitErr;
    if (!CommitStagedAssets(commitErr)) {
        result.error = commitErr;
        cleanupTemporary();
        return result;
    }

    if (!manifestBytes.empty()) {
        std::string manifestWriteErr;
        if (!WriteBinaryFile(SD_SPRITES_PACK_MANIFEST, manifestBytes, manifestWriteErr)) {
            LOG_WARNING("SpriteAssetDownloader: failed to persist zip manifest: " + manifestWriteErr);
        }
    }

    const auto spritesRoot = std::filesystem::path(SD_SPRITES_DIR);
    std::size_t resolved = 0;
    for (const auto& filename : missingCandidates) {
        if (FileExistsAndNotEmpty(spritesRoot / filename)) {
            ++resolved;
        }
    }

    result.succeeded = true;
    result.verified = true;
    result.resolvedSprites = resolved;
    return result;
}

bool BuildMissingSpriteListFromData(
    const std::vector<u8>& dataJsonBytes,
    std::vector<std::string>& outMissing,
    std::size_t& outPresent,
    std::string& err
) {
    std::string jsonText(reinterpret_cast<const char*>(dataJsonBytes.data()), dataJsonBytes.size());
    nlohmann::json parsed = nlohmann::json::parse(jsonText, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("pokemon") || !parsed["pokemon"].is_array()) {
        err = "Invalid pokemon data.json format";
        return false;
    }

    const auto spritesRoot = std::filesystem::path(SD_SPRITES_DIR);
    try {
        std::filesystem::create_directories(spritesRoot);
    } catch (...) {
        err = "Failed to create sprites directory";
        return false;
    }

    outMissing.clear();
    outPresent = 0;

    for (const auto& entry : parsed["pokemon"]) {
        if (!entry.is_object() || !entry.contains("file_path") || !entry["file_path"].is_string()) {
            continue;
        }

        const std::string fullPath = entry["file_path"].get<std::string>();
        const auto slash = fullPath.find_last_of('/');
        const std::string filename = (slash == std::string::npos) ? fullPath : fullPath.substr(slash + 1);
        if (filename.empty()) {
            continue;
        }

        const auto localPath = spritesRoot / filename;
        if (FileExistsAndNotEmpty(localPath)) {
            ++outPresent;
        } else {
            outMissing.push_back(filename);
        }
    }

    return true;
}

}  // namespace

std::size_t SpriteAssetDownloader::CountMissingSprites(std::string* error) {
    std::vector<u8> dataJsonBytes;
    if (!ReadBinaryFile(SD_DATA_JSON, dataJsonBytes) || dataJsonBytes.empty()) {
        if (!ReadBinaryFile(ROMFS_DATA_JSON, dataJsonBytes) || dataJsonBytes.empty()) {
            if (error != nullptr) {
                *error = "No local data.json found on SD or romfs";
            }
            return 0;
        }
    }

    std::vector<std::string> missing;
    std::size_t present = 0;
    std::string parseErr;
    if (!BuildMissingSpriteListFromData(dataJsonBytes, missing, present, parseErr)) {
        if (error != nullptr) {
            *error = parseErr;
        }
        return 0;
    }

    const auto knownMissing = LoadKnownMissingSprites();
    if (knownMissing.empty()) {
        return missing.size();
    }

    std::size_t pending = 0;
    for (const auto& filename : missing) {
        if (!knownMissing.contains(filename)) {
            ++pending;
        }
    }

    return pending;
}

SpriteAssetDownloader::SyncResult SpriteAssetDownloader::SyncFromCdn(
    const std::string& cdnBase,
    std::size_t maxDownloadsPerRun,
    std::uint32_t maxMilliseconds,
    const std::function<void(const ProgressInfo&)>& onProgress
) {
    SyncResult result;
    result.attemptedNetwork = true;

    std::string networkErr;
    NetworkSession session;
    if (!session.Start(networkErr)) {
        result.error = networkErr;
        return result;
    }

    const std::string normalizedBase = NormalizeCdnBase(cdnBase);
    std::vector<u8> dataJsonBytes;

    static std::mutex dataJsonCacheMutex;
    static std::vector<u8> cachedDataJsonBytes;

    {
        std::lock_guard<std::mutex> lock(dataJsonCacheMutex);
        if (!cachedDataJsonBytes.empty()) {
            dataJsonBytes = cachedDataJsonBytes;
        }
    }

    if (dataJsonBytes.empty()) {
        const std::string dataUrl = normalizedBase + "/assets/data.json";
        HttpResponse dataResp;
        std::string dataErr;

        if (HttpGet(dataUrl, dataResp, dataErr) && dataResp.statusCode == 200) {
            dataJsonBytes = std::move(dataResp.body);
            if (dataJsonBytes.empty()) {
                result.error = "Downloaded data.json is empty";
                return result;
            }

            std::string writeErr;
            if (!WriteBinaryFile(SD_DATA_JSON, dataJsonBytes, writeErr)) {
                result.error = writeErr;
                return result;
            }
            result.downloadedDataJson = true;
        } else {
            if (!ReadBinaryFile(SD_DATA_JSON, dataJsonBytes) || dataJsonBytes.empty()) {
                if (!ReadBinaryFile(ROMFS_DATA_JSON, dataJsonBytes) || dataJsonBytes.empty()) {
                    if (dataErr.empty()) {
                        std::ostringstream ss;
                        ss << "Failed to download data.json (status " << dataResp.statusCode << ") and no local fallback";
                        result.error = ss.str();
                    } else {
                        result.error = dataErr;
                    }
                    return result;
                }

                LOG_WARNING("SpriteAssetDownloader: using romfs data.json fallback");
                std::string writeErr;
                if (!WriteBinaryFile(SD_DATA_JSON, dataJsonBytes, writeErr)) {
                    LOG_WARNING("SpriteAssetDownloader: failed to persist romfs data.json to SD: " + writeErr);
                }
            } else {
                LOG_WARNING("SpriteAssetDownloader: using local SD data.json fallback");
            }

            if (dataJsonBytes.empty()) {
                if (dataErr.empty()) {
                    std::ostringstream ss;
                    ss << "Failed to download data.json (status " << dataResp.statusCode << ") and no local fallback";
                    result.error = ss.str();
                } else {
                    result.error = dataErr;
                }
                return result;
            }
        }

        std::lock_guard<std::mutex> lock(dataJsonCacheMutex);
        cachedDataJsonBytes = dataJsonBytes;
    }

    std::vector<std::string> missingSprites;
    std::size_t locallyPresent = 0;
    std::string parseErr;
    if (!BuildMissingSpriteListFromData(dataJsonBytes, missingSprites, locallyPresent, parseErr)) {
        result.error = parseErr;
        return result;
    }

    auto knownMissing = LoadKnownMissingSprites();
    std::vector<std::string> retryableMissingSprites;
    retryableMissingSprites.reserve(missingSprites.size());

    std::size_t knownMissingSkipped = 0;
    for (const auto& filename : missingSprites) {
        if (!knownMissing.empty() && knownMissing.contains(filename)) {
            ++knownMissingSkipped;
            continue;
        }
        retryableMissingSprites.push_back(filename);
    }

    result.skippedSprites = locallyPresent + knownMissingSkipped;
    result.totalMissingSprites = retryableMissingSprites.size();
    result.remainingSprites = result.totalMissingSprites;

    if (retryableMissingSprites.empty()) {
        LOG_INFO("SpriteAssetDownloader: sync finished (downloaded=0, skipped=" + std::to_string(result.skippedSprites) + ", failed=0, remaining=0)");
        return result;
    }

    std::size_t zipResolvedSprites = 0;
    const auto zipAttempt = TryApplyZipPack(normalizedBase, retryableMissingSprites, onProgress);
    if (zipAttempt.attempted) {
        if (zipAttempt.succeeded) {
            result.usedZipPack = true;
            result.zipPackVerified = zipAttempt.verified;

            zipResolvedSprites = std::min(result.totalMissingSprites, zipAttempt.resolvedSprites);
            result.downloadedSprites += zipResolvedSprites;

            std::error_code fsErr;
            std::filesystem::remove(SD_KNOWN_MISSING_SPRITES, fsErr);
            knownMissing.clear();

            std::vector<std::string> remainingAfterZip;
            remainingAfterZip.reserve(retryableMissingSprites.size());

            const auto spritesRootCheck = std::filesystem::path(SD_SPRITES_DIR);
            for (const auto& filename : retryableMissingSprites) {
                if (!FileExistsAndNotEmpty(spritesRootCheck / filename)) {
                    remainingAfterZip.push_back(filename);
                }
            }
            retryableMissingSprites = std::move(remainingAfterZip);

            result.remainingSprites = retryableMissingSprites.size();
            result.downloadedDataJson = true;

            std::vector<u8> refreshedDataJson;
            if (ReadBinaryFile(SD_DATA_JSON, refreshedDataJson) && !refreshedDataJson.empty()) {
                dataJsonBytes = refreshedDataJson;
                std::lock_guard<std::mutex> lock(dataJsonCacheMutex);
                cachedDataJsonBytes = dataJsonBytes;
            }

            if (retryableMissingSprites.empty()) {
                LOG_INFO("SpriteAssetDownloader: zip sync finished (downloaded=" + std::to_string(result.downloadedSprites) +
                         ", skipped=" + std::to_string(result.skippedSprites) +
                         ", failed=0, remaining=0)");
                return result;
            }

            result.zipFallbackTriggered = true;
            LOG_WARNING("SpriteAssetDownloader: zip sync left " + std::to_string(retryableMissingSprites.size()) + " sprites unresolved, falling back to incremental sync");
        } else {
            result.zipFallbackTriggered = true;
            LOG_WARNING("SpriteAssetDownloader: zip sync failed, falling back to incremental sync: " + zipAttempt.error);
        }
    }

    const auto spritesRoot = std::filesystem::path(SD_SPRITES_DIR);

    auto emitProgress = [&](std::size_t processedIncremental, std::size_t downloadedIncremental, std::size_t failedIncremental) {
        if (!onProgress) {
            return;
        }

        const std::size_t baseProcessed = zipResolvedSprites;

        ProgressInfo info;
        info.phase = ProgressInfo::Phase::IncrementalDownload;
        info.usedZip = result.usedZipPack;
        info.usingFallback = result.zipFallbackTriggered;
        info.totalMissing = result.totalMissingSprites;
        info.processed = std::min(info.totalMissing, baseProcessed + processedIncremental);
        info.downloaded = std::min(info.totalMissing, zipResolvedSprites + downloadedIncremental);
        info.failed = failedIncremental;
        info.remaining = (info.totalMissing > info.processed) ? (info.totalMissing - info.processed) : 0;
        onProgress(info);
    };
    emitProgress(0, 0, 0);

    const auto syncStart = std::chrono::steady_clock::now();
    std::size_t attemptedDownloads = 0;
    std::size_t processedIncremental = 0;
    std::size_t downloadedIncremental = 0;
    std::size_t failedIncremental = 0;
    std::size_t newlyKnownMissing = 0;

    for (const auto& filename : retryableMissingSprites) {
        if (maxDownloadsPerRun > 0 && attemptedDownloads >= maxDownloadsPerRun) {
            result.budgetReached = true;
            break;
        }

        if (maxMilliseconds > 0) {
            const auto elapsedMs = static_cast<std::uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - syncStart).count()
            );
            if (elapsedMs >= maxMilliseconds) {
                result.budgetReached = true;
                break;
            }
        }

        ++attemptedDownloads;

        const auto localPath = spritesRoot / filename;

        // Primary: jsDelivr CDN (pokesprite-images npm package)
        const auto primaryUrl = BuildPokespriteFallbackUrl(filename);
        HttpResponse spriteResp;
        std::string spriteErr;
        int primaryStatus = 0;
        int fallbackStatus = 0;
        bool fallbackAttempted = false;

        bool downloaded = false;
        if (primaryUrl.has_value() && HttpGet(*primaryUrl, spriteResp, spriteErr) && spriteResp.statusCode == 200 && !spriteResp.body.empty()) {
            downloaded = true;
            primaryStatus = spriteResp.statusCode;
        } else {
            if (primaryUrl.has_value()) {
                primaryStatus = spriteResp.statusCode;
            }
            // Fallback: raw.githubusercontent.com
            const auto fallbackUrl = BuildPokespriteRawFallbackUrl(filename);
            if (fallbackUrl.has_value()) {
                fallbackAttempted = true;
                HttpResponse fallbackResp;
                std::string fallbackErr;
                const bool fallbackOk = HttpGet(*fallbackUrl, fallbackResp, fallbackErr);
                fallbackStatus = fallbackResp.statusCode;
                if (fallbackOk && fallbackResp.statusCode == 200 && !fallbackResp.body.empty()) {
                    spriteResp = std::move(fallbackResp);
                    downloaded = true;
                } else if (spriteErr.empty()) {
                    if (fallbackErr.empty()) {
                        std::ostringstream ss;
                        ss << "fallback status " << fallbackResp.statusCode;
                        spriteErr = ss.str();
                    } else {
                        spriteErr = fallbackErr;
                    }
                }
            }
        }

        if (!downloaded) {
            const bool primaryClientError = (primaryStatus >= 400) && (primaryStatus < 500);
            const bool permanentlyMissing =
                (primaryStatus == 404 && !fallbackAttempted)
                || (fallbackAttempted && fallbackStatus == 404 && primaryClientError);
            if (permanentlyMissing && knownMissing.insert(filename).second) {
                ++newlyKnownMissing;
            }

            ++result.failedSprites;
            ++failedIncremental;
            ++processedIncremental;
            emitProgress(processedIncremental, downloadedIncremental, failedIncremental);
            if (result.error.empty()) {
                if (spriteErr.empty()) {
                    std::ostringstream ss;
                    ss << "Failed downloading sprite " << filename << " (status " << spriteResp.statusCode << ")";
                    result.error = ss.str();
                } else {
                    result.error = "Failed downloading sprite " + filename + ": " + spriteErr;
                }
            }
            continue;
        }

        std::string writeErr;
        if (!WriteBinaryFile(localPath.string(), spriteResp.body, writeErr)) {
            ++result.failedSprites;
            ++failedIncremental;
            ++processedIncremental;
            emitProgress(processedIncremental, downloadedIncremental, failedIncremental);
            if (result.error.empty()) {
                result.error = writeErr;
            }
            continue;
        }

        ++result.downloadedSprites;
        ++downloadedIncremental;
        ++processedIncremental;
        emitProgress(processedIncremental, downloadedIncremental, failedIncremental);
    }

    if (newlyKnownMissing > 0) {
        std::string cacheErr;
        if (!SaveKnownMissingSprites(knownMissing, cacheErr)) {
            LOG_WARNING("SpriteAssetDownloader: failed to persist known-missing cache: " + cacheErr);
        } else {
            LOG_INFO("SpriteAssetDownloader: cached " + std::to_string(newlyKnownMissing) + " known-missing sprites (404)");
        }
    }

    result.remainingSprites = (result.totalMissingSprites > result.downloadedSprites)
        ? (result.totalMissingSprites - result.downloadedSprites)
        : 0;

    LOG_INFO("SpriteAssetDownloader: sync finished (downloaded=" + std::to_string(result.downloadedSprites) +
             ", skipped=" + std::to_string(result.skippedSprites) +
             ", failed=" + std::to_string(result.failedSprites) +
             ", remaining=" + std::to_string(result.remainingSprites) +
             ", zip=" + std::string(result.usedZipPack ? "true" : "false") +
             ", fallback=" + std::string(result.zipFallbackTriggered ? "true" : "false") +
             ", budgetReached=" + std::string(result.budgetReached ? "true" : "false") + ")");

    return result;
}

}  // namespace pksm::utils