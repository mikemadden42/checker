#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace std;

// How long to wait for each connection attempt before giving up
const int connectTimeoutMs = 5000;

// Owns a getaddrinfo() result and frees it automatically
using AddrInfoPtr = unique_ptr<struct addrinfo, decltype(&freeaddrinfo)>;

// Resolve domain for TCP connections to port. Returns an empty pointer and
// sets error if the lookup fails.
AddrInfoPtr resolve(const string &domain, int port, string &error) {
    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;      // IPv4 or IPv6, we don't care
    hints.ai_socktype = SOCK_STREAM;  // Use TCP stream sockets
    hints.ai_flags = AI_NUMERICSERV;  // The port is always a number

    struct addrinfo *res = nullptr;
    int status =
        getaddrinfo(domain.c_str(), to_string(port).c_str(), &hints, &res);
    if (status != 0) {
        error = gai_strerror(status);
        return AddrInfoPtr(nullptr, freeaddrinfo);
    }
    return AddrInfoPtr(res, freeaddrinfo);
}

// Format each IPv4 and IPv6 address in res as a string
vector<string> formatAddresses(const string &domain,
                               const struct addrinfo *res) {
    vector<string> addresses;
    for (const struct addrinfo *p = res; p != nullptr; p = p->ai_next) {
        const void *addr;
        if (p->ai_family == AF_INET) {
            addr = &((const struct sockaddr_in *)p->ai_addr)->sin_addr;
        } else if (p->ai_family == AF_INET6) {
            addr = &((const struct sockaddr_in6 *)p->ai_addr)->sin6_addr;
        } else {
            continue;  // Not an IP address
        }

        char ipstr[INET6_ADDRSTRLEN];
        if (inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr) == nullptr) {
            cerr << "Could not format an address for " << domain << ": "
                 << strerror(errno) << endl;
            continue;
        }
        addresses.push_back(ipstr);
    }
    return addresses;
}

// Join the first two addresses, noting how many more there are
string summarizeAddresses(const vector<string> &addresses) {
    const size_t shown = 2;
    if (addresses.empty()) {
        return "(no addresses)";
    }

    string summary;
    for (size_t i = 0; i < addresses.size() && i < shown; i++) {
        if (i > 0) {
            summary += ", ";
        }
        summary += addresses[i];
    }
    if (addresses.size() > shown) {
        summary += " (+" + to_string(addresses.size() - shown) + " more)";
    }
    return summary;
}

// Connect sockfd to addr, waiting at most connectTimeoutMs.
// Returns 0 on success, or an errno value describing the failure.
int connectWithTimeout(int sockfd, const struct sockaddr *addr,
                       socklen_t addrlen) {
    // Use a non-blocking socket so connect() returns immediately
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1 || fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return errno;
    }

    if (connect(sockfd, addr, addrlen) == 0) {
        return 0;
    }
    if (errno != EINPROGRESS) {
        return errno;
    }

    // Wait for the socket to become writable, which means connect() finished
    struct pollfd pfd {};
    pfd.fd = sockfd;
    pfd.events = POLLOUT;
    int ready = poll(&pfd, 1, connectTimeoutMs);
    if (ready == -1) {
        return errno;
    }
    if (ready == 0) {
        return ETIMEDOUT;
    }

    // connect() finished; SO_ERROR says whether it succeeded
    int error = 0;
    socklen_t len = sizeof error;
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == -1) {
        return errno;
    }
    return error;
}

// Try each resolved address until one connects. Returns 0 on success, or the
// errno value from the last failed attempt.
int connectToPort(const struct addrinfo *res) {
    int lastError = 0;
    for (const struct addrinfo *p = res; p != nullptr; p = p->ai_next) {
        int sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            lastError = errno;
            continue;
        }

        lastError = connectWithTimeout(sockfd, p->ai_addr, p->ai_addrlen);
        close(sockfd);

        if (lastError == 0) {
            // Connection successful
            return 0;
        }
    }
    return lastError;
}

void printUsage(const char *program) {
    cerr << "Usage: " << program << " [-v] [-p PORT] [FILE]\n"
         << "  FILE     domains to check, one per line (default: domains.txt)\n"
         << "  -p PORT  TCP port to test, 1-65535 (default: 443)\n"
         << "  -v       show every address and each step" << endl;
}

int main(int argc, char *argv[]) {
    int port = 443;
    bool verbose = false;

    // Parse options; usage errors exit with 2 so scripts can tell them apart
    // from failed checks
    int opt;
    while ((opt = getopt(argc, argv, "hp:v")) != -1) {
        switch (opt) {
            case 'h':
                printUsage(argv[0]);
                return 0;
            case 'p': {
                // Accept digits only; strtol alone would allow " 80" or "+80"
                char *end;
                errno = 0;
                long value = strtol(optarg, &end, 10);
                if (!isdigit(static_cast<unsigned char>(optarg[0])) ||
                    errno != 0 || *end != '\0' || value < 1 ||
                    value > 65535) {
                    cerr << "Invalid port: " << optarg << endl;
                    printUsage(argv[0]);
                    return 2;
                }
                port = static_cast<int>(value);
                break;
            }
            case 'v':
                verbose = true;
                break;
            default:  // getopt has already printed the problem
                printUsage(argv[0]);
                return 2;
        }
    }
    if (argc - optind > 1) {
        cerr << "Too many arguments" << endl;
        printUsage(argv[0]);
        return 2;
    }

    const string filename = optind < argc ? argv[optind] : "domains.txt";
    ifstream infile(filename);

    if (!infile.is_open()) {
        cerr << "Error opening file: " << filename << endl;
        return 1;
    }

    // Vector to store the domain names
    vector<string> domains;
    string domain;

    // Read domain names from the file
    while (getline(infile, domain)) {
        // Drop comments; '#' can't appear in a hostname
        domain = domain.substr(0, domain.find('#'));

        // Trim surrounding whitespace, including '\r' from CRLF line endings
        const char *whitespace = " \t\r\n\f\v";
        size_t start = domain.find_first_not_of(whitespace);
        if (start == string::npos) {
            continue;  // Skip blank lines
        }
        size_t end = domain.find_last_not_of(whitespace);
        domains.push_back(domain.substr(start, end - start + 1));
    }

    // Close the file
    infile.close();

    if (domains.empty()) {
        cerr << "Warning: no domains found in " << filename << endl;
    }

    // Track whether every check passed so the exit code reflects failures
    bool allPassed = true;

    // Pad domains to the same width so the one-line output lines up
    size_t width = 0;
    for (const auto &d : domains) {
        width = max(width, d.size());
    }

    // Perform DNS lookup and a port check for each domain. Output is printed
    // as each step finishes so slow connections show progress.
    for (const auto &d : domains) {
        if (verbose) {
            cout << "Checking DNS for domain: " << d << endl;
        } else {
            cout << left << setw(static_cast<int>(width)) << d << "  "
                 << flush;
        }

        string dnsError;
        AddrInfoPtr res = resolve(d, port, dnsError);
        if (!res) {
            // DNS lookup failed, so there is nothing to connect to
            allPassed = false;
            if (verbose) {
                cerr << "DNS lookup failed for domain: " << d << " - "
                     << dnsError << endl;
                cout << "DNS lookup failed." << endl;
                cout << "Skipping port " << port << " check for " << d
                     << endl;
                cout << endl;
            } else {
                cout << "DNS FAILED (" << dnsError << ")" << endl;
            }
            continue;
        }

        vector<string> addresses = formatAddresses(d, res.get());
        if (verbose) {
            cout << "IP addresses for domain " << d << ":" << endl;
            for (const auto &address : addresses) {
                cout << "Forward DNS for " << d << ": " << address << endl;
            }
            cout << "DNS lookup successful." << endl;
            cout << "Checking connection to port " << port
                 << " for domain: " << d << endl;
        } else {
            cout << summarizeAddresses(addresses) << "  " << flush;
        }

        // Attempt to connect to the port using the addresses already resolved
        int connectError = connectToPort(res.get());
        if (connectError != 0) {
            allPassed = false;
        }

        if (verbose) {
            if (connectError == 0) {
                cout << "Connection to port " << port << " successful for "
                     << d << endl;
            } else {
                cerr << "Error connecting to " << d << " on port " << port
                     << ": " << strerror(connectError) << endl;
                cout << "Connection to port " << port << " failed for " << d
                     << endl;
            }
            cout << endl;  // Separate each domain's results
        } else if (connectError == 0) {
            cout << port << ":OK" << endl;
        } else {
            cout << port << ":FAILED (" << strerror(connectError) << ")"
                 << endl;
        }
    }

    return allPassed ? 0 : 1;
}
