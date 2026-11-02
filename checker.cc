#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace std;

// How long to wait for each connection attempt before giving up
const int connectTimeoutMs = 5000;

// Owns a getaddrinfo() result and frees it automatically
using AddrInfoPtr = unique_ptr<struct addrinfo, decltype(&freeaddrinfo)>;

// Resolve domain for TCP connections to port. Prints the error and returns an
// empty pointer if the lookup fails.
AddrInfoPtr resolve(const string &domain, int port) {
    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;      // IPv4 or IPv6, we don't care
    hints.ai_socktype = SOCK_STREAM;  // Use TCP stream sockets
    hints.ai_flags = AI_NUMERICSERV;  // The port is always a number

    struct addrinfo *res = nullptr;
    int status =
        getaddrinfo(domain.c_str(), to_string(port).c_str(), &hints, &res);
    if (status != 0) {
        cerr << "DNS lookup failed for domain: " << domain << " - "
             << gai_strerror(status) << endl;
        return AddrInfoPtr(nullptr, freeaddrinfo);
    }
    return AddrInfoPtr(res, freeaddrinfo);
}

void printAddresses(const string &domain, const struct addrinfo *res) {
    cout << "IP addresses for domain " << domain << ":" << endl;
    for (const struct addrinfo *p = res; p != nullptr; p = p->ai_next) {
        const void *addr;
        if (p->ai_family == AF_INET) {
            addr = &((const struct sockaddr_in *)p->ai_addr)->sin_addr;
        } else if (p->ai_family == AF_INET6) {
            addr = &((const struct sockaddr_in6 *)p->ai_addr)->sin6_addr;
        } else {
            continue;  // Not an IP address
        }

        // Convert the IP to a string and print it
        char ipstr[INET6_ADDRSTRLEN];
        if (inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr) == nullptr) {
            cerr << "Could not format an address for " << domain << ": "
                 << strerror(errno) << endl;
            continue;
        }
        cout << "Forward DNS for " << domain << ": " << ipstr << endl;
    }
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

bool connectToPort(const string &domain, int port,
                   const struct addrinfo *res) {
    // Attempt to connect to the specified port
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
            return true;
        }
    }

    cerr << "Error connecting to " << domain << " on port " << port << ": "
         << strerror(lastError) << endl;
    return false;
}

void printUsage(const char *program) {
    cerr << "Usage: " << program << " [-p PORT] [FILE]\n"
         << "  FILE     domains to check, one per line (default: domains.txt)\n"
         << "  -p PORT  TCP port to test, 1-65535 (default: 443)" << endl;
}

int main(int argc, char *argv[]) {
    int port = 443;

    // Parse options; usage errors exit with 2 so scripts can tell them apart
    // from failed checks
    int opt;
    while ((opt = getopt(argc, argv, "hp:")) != -1) {
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

    // Perform DNS lookup and a port check for each domain
    for (const auto &d : domains) {
        cout << "Checking DNS for domain: " << d << endl;
        AddrInfoPtr res = resolve(d, port);
        if (!res) {
            // DNS lookup failed, so there is nothing to connect to
            cout << "DNS lookup failed." << endl;
            cout << "Skipping port " << port << " check for " << d << endl;
            cout << endl;
            allPassed = false;
            continue;
        }
        printAddresses(d, res.get());
        cout << "DNS lookup successful." << endl;

        // Attempt to connect to the port using the addresses already resolved
        cout << "Checking connection to port " << port
             << " for domain: " << d << endl;
        if (connectToPort(d, port, res.get())) {
            // Connection successful
            cout << "Connection to port " << port << " successful for " << d
                 << endl;
        } else {
            // Connection failed
            cout << "Connection to port " << port << " failed for " << d
                 << endl;
            allPassed = false;
        }

        cout << endl;  // Separate each domain's results
    }

    return allPassed ? 0 : 1;
}
