#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

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
    for (struct addrinfo *p = res; p != nullptr; p = p->ai_next) {
        int sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) != -1) {
            // Connection successful
            close(sockfd);
            freeaddrinfo(res);
            return true;
        }

        close(sockfd);
    }

    freeaddrinfo(res);
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
        domains.push_back(domain);
    }

    // Close the file
    infile.close();

    // Perform DNS lookup for each domain
    for (const auto &d : domains) {
        cout << "Checking DNS for domain: " << d << endl;
        if (performDNSLookup(d)) {
            // DNS lookup successful
            cout << "DNS lookup successful." << endl;
        } else {
            // DNS lookup failed
            cout << "DNS lookup failed." << endl;
        }

        // Attempt to connect to port 443 for the domain
        cout << "Checking connection to port 443 for domain: " << d << endl;
        if (connectToPort(d, 443)) {
            // Connection successful
            cout << "Connection to port 443 successful for " << d << endl;
        } else {
            // Connection failed
            cout << "Connection to port 443 failed for " << d << endl;
        }

        cout << endl;  // Separate each domain's results
    }

    return 0;
}
