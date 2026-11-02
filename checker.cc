#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

// How long to wait for each connection attempt before giving up
const int connectTimeoutMs = 5000;

bool performDNSLookup(const string &domain) {
    struct addrinfo hints {
    }, *res;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;      // IPv4 or IPv6, we don't care
    hints.ai_socktype = SOCK_STREAM;  // Use TCP stream sockets

    int status = getaddrinfo(domain.c_str(), nullptr, &hints, &res);
    if (status != 0) {
        cerr << "DNS lookup failed for domain: " << domain << " - "
             << gai_strerror(status) << endl;
        return false;
    }

    // Print out IP addresses if DNS lookup is successful
    cout << "IP addresses for domain " << domain << ":" << endl;
    for (struct addrinfo *p = res; p != nullptr; p = p->ai_next) {
        void *addr;
        string ip;

        if (p->ai_family == AF_INET) {  // IPv4
            auto *ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
        } else {  // IPv6
            auto *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
        }

        // Convert the IP to a string and print it
        char ipstr[INET6_ADDRSTRLEN];
        inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
        cout << "Forward DNS for " << domain << ": " << ipstr << endl;
    }

    freeaddrinfo(res);
    return true;
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

bool connectToPort(const string &domain, int port) {
    // Get address info for the domain
    struct addrinfo hints {
    }, *res;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char portStr[6];  // Port number cannot exceed 5 digits
    snprintf(portStr, sizeof(portStr), "%d", port);

    int status = getaddrinfo(domain.c_str(), portStr, &hints, &res);
    if (status != 0) {
        cerr << "Error getting address info for " << domain << ": "
             << gai_strerror(status) << endl;
        return false;
    }

    // Attempt to connect to the specified port
    int lastError = 0;
    for (struct addrinfo *p = res; p != nullptr; p = p->ai_next) {
        int sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            lastError = errno;
            continue;
        }

        lastError = connectWithTimeout(sockfd, p->ai_addr, p->ai_addrlen);
        close(sockfd);

        if (lastError == 0) {
            // Connection successful
            freeaddrinfo(res);
            return true;
        }
    }

    freeaddrinfo(res);
    cerr << "Error connecting to " << domain << " on port " << port << ": "
         << strerror(lastError) << endl;
    return false;
}

int main() {
    const string filename = "domains.txt";
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

    // Perform DNS lookup for each domain
    for (const auto &d : domains) {
        cout << "Checking DNS for domain: " << d << endl;
        if (performDNSLookup(d)) {
            // DNS lookup successful
            cout << "DNS lookup successful." << endl;
        } else {
            // DNS lookup failed
            cout << "DNS lookup failed." << endl;
            allPassed = false;
        }

        // Attempt to connect to port 443 for the domain
        cout << "Checking connection to port 443 for domain: " << d << endl;
        if (connectToPort(d, 443)) {
            // Connection successful
            cout << "Connection to port 443 successful for " << d << endl;
        } else {
            // Connection failed
            cout << "Connection to port 443 failed for " << d << endl;
            allPassed = false;
        }

        cout << endl;  // Separate each domain's results
    }

    return allPassed ? 0 : 1;
}
