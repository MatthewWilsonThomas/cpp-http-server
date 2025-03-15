#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

// Debug tag for std::out statements with toggle functionality
#define DEBUG_ENABLED 0  // Set to 0 to disable debug output
#define DEBUG(x) if (DEBUG_ENABLED) std::cout << "[DEBUG] " << x << std::endl

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  
  // You can use print statements as follows for debugging, they'll be visible when running tests.
  DEBUG("Logs from your program will appear here!");

  // Uncomment this block to pass the first stage
  
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   std::cerr << "Failed to create server socket\n";
   return 1;
  }
  
  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }
  
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(4221);
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 4221\n";
    return 1;
  }
  
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }
  
  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);
  
  DEBUG("Waiting for a client to connect...");
  
  int client = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);

  // Buffer to store the incoming HTTP request
  char buffer[1024] = {0};
  int bytes_read = read(client, buffer, sizeof(buffer));
  if (bytes_read < 0) {
    std::cerr << "Failed to read from client\n";
    return 1;
  }  else {
    buffer[bytes_read] = '\0';
    DEBUG("Received HTTP request:\n" << buffer);

    // Extract the URL (path) from the request
    std::string request(buffer);
    std::string url;
    
    // Find the first line of the request
    size_t end_of_line = request.find("\r\n");
    if (end_of_line != std::string::npos) {
      std::string first_line = request.substr(0, end_of_line);
      
      // Find the first space (after the HTTP method)
      size_t method_end = first_line.find(" ");
      if (method_end != std::string::npos) {
        // Find the second space (after the URL)
        size_t url_end = first_line.find(" ", method_end + 1);
        DEBUG("Extracted first line: " << first_line);
        if (url_end != std::string::npos) {
          // Extract the URL between the two spaces
          url = first_line.substr(method_end + 1, url_end - method_end - 1);
          DEBUG("Extracted URL: " << url);
        }
      }
    }

    std::string response;
    if (url == "/") {
      response = "HTTP/1.1 200 OK\r\n\r\n";
    } else {
      response = "HTTP/1.1 404 Not Found\r\n\r\n";
    }
    send(client, response.c_str(), response.size(), 0);
    DEBUG("Response sent");
    close(client);
  }
  
  close(server_fd);
  return 0;
}
