// test/simpleBench.cpp
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <cstring>
#include <iomanip>

#define SERVER_PORT 8080

std::atomic<int> messages_sent{0};
std::atomic<int> messages_received{0};
std::atomic<long> bytes_sent{0};
std::atomic<long> bytes_received{0};

// Data recording function
void record_results(const std::string& filename, 
                   int connections, int messages, int msg_size, int duration,
                   double elapsed_time, int total_sent, int total_recv, 
                   long total_bytes, int errors) {
    std::ofstream file(filename, std::ios::app);  // Append mode
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << " for writing" << std::endl;
        return;
    }
    
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    // Write header if file is empty (new file)
    file.seekp(0, std::ios::end);
    if (file.tellp() == 0) {
        file << "timestamp,connections,msg_size,duration,bandwidth_mbps,msg_per_sec,errors\n";
    }
    
    // Calculate metrics
    double bandwidth_mbps = (total_bytes / elapsed_time) / (1024 * 1024);
    double msg_per_sec = total_recv / elapsed_time;
    
    // Write data in CSV format
    file << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << ","
         << connections << ","
         << msg_size << ","
         << duration << ","
         << std::fixed << std::setprecision(2) << bandwidth_mbps << ","
         << std::setprecision(1) << msg_per_sec << ","
         << errors << std::endl;
    
    file.close();
    std::cout << "Results logged to: " << filename << std::endl;
}

void client_worker(int num_messages, int message_size) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(SERVER_PORT);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return;
    }
    
    std::vector<char> buffer(message_size, 'A');
    std::vector<char> recv_buffer(message_size);
    
    for (int i = 0; i < num_messages; i++) {
        // Send
        if (send(sock, buffer.data(), message_size, 0) != message_size) break;
        messages_sent++;
        bytes_sent += message_size;
        
        // Receive
        int total = 0;
        while (total < message_size) {
            int received = recv(sock, recv_buffer.data() + total, message_size - total, 0);
            if (received <= 0) goto done;
            total += received;
        }
        messages_received++;
        bytes_received += total;
    }
    
done:
    close(sock);
}

int main(int argc, char* argv[]) {
    int connections = 5000;
    int messages = 10;
    int msg_size = 1024;
    int duration = 20;
    std::string output_file = "benchmark_results.csv";
    
    // Simple argument parsing
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "-c") == 0) connections = atoi(argv[i+1]);
        else if (strcmp(argv[i], "-n") == 0) messages = atoi(argv[i+1]);
        else if (strcmp(argv[i], "-s") == 0) msg_size = atoi(argv[i+1]);
        else if (strcmp(argv[i], "-t") == 0) duration = atoi(argv[i+1]);
        else if (strcmp(argv[i], "-o") == 0) output_file = argv[i+1];
    }
    
    // Display help if requested
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        std::cout << "Usage: " << argv[0] << " [options]\n"
                  << "Options:\n"
                  << "  -c <num>     Number of connections (default: 5000)\n"
                  << "  -n <num>     Number of messages per connection (default: 10)\n"
                  << "  -s <bytes>   Message size in bytes (default: 1024)\n"
                  << "  -t <seconds> Duration parameter (default: 20)\n"
                  << "  -o <file>    Output file for results (default: benchmark_results.csv)\n"
                  << "  -h, --help   Show this help message\n";
        return 0;
    }
    
    std::cout << "Simple Benchmark: " << connections << " connections, " 
              << messages << " messages, " << msg_size << " bytes each" << std::endl;
    std::cout << "Output file: " << output_file << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Launch client threads
    std::vector<std::thread> clients;
    for (int i = 0; i < connections; i++) {
        clients.emplace_back(client_worker, messages, msg_size);
    }
    
    // Wait for completion
    for (auto& t : clients) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    
    // Results
    int total_sent = messages_sent.load();
    int total_recv = messages_received.load();
    long total_bytes = bytes_received.load();
    int errors = total_sent - total_recv;
    
    std::cout << "\nResults:" << std::endl;
    std::cout << "Time: " << elapsed << " seconds" << std::endl;
    std::cout << "Messages: " << total_recv << " (" << (total_recv/elapsed) << " msg/s)" << std::endl;
    std::cout << "Bandwidth: " << (total_bytes/elapsed/1024/1024) << " MB/s" << std::endl;
    std::cout << "Errors: " << errors << std::endl;
    
    // Record results to file
    record_results(output_file, connections, messages, msg_size, duration,
                  elapsed, total_sent, total_recv, total_bytes, errors);
    
    return 0;
}
