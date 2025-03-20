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

// Custom exception for API not found errors
class APINotFoundException : public std::exception {
private:
    std::string message;

public:
    APINotFoundException(const std::string& endpoint = "Unknown endpoint") : 
        message("API Not Found: " + endpoint) {}

    virtual const char* what() const noexcept override {
        return message.c_str();
    }
};

class API {
public:
    /**
     * Handles API calls based on a URL string
     * @param url The URL string to process
     * @return Content string if applicable, empty string if no response needed
     */
    std::string handleURL(const std::string& url) {
        DEBUG("Processing URL: " + url);     

        if (url.find("/echo/") == 0) {
            std::string echoContent = url.substr(6); // Skip "/echo"
            return echoContent;
        } 
        
        // Default response for unknown URLs
        throw APINotFoundException(url);
    }
};

std::string make_response(std::string status_code, std::string content) {
  if (content.empty()) {
    return "HTTP/1.1 " + status_code + "\r\n\r\n";
  }
  return "HTTP/1.1 " + status_code + "\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(content.length()) + "\r\n\r\n" + content;
}

int send_response(int client_socket, int server_socket, std::string response) {
  send(client_socket, response.c_str(), response.size(), 0);
  DEBUG("Response sent: " << response);

  close(client_socket);
  close(server_socket);
  return 0;
}

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
  }
  API api;
  std::string response;

  buffer[bytes_read] = '\0';
  DEBUG("Received HTTP request:\n" << buffer);
  // Extract the URL (path) from the request
  std::string request(buffer);
  std::string url = "";

  response = make_response("400 Bad Request", ""); // Default response for malformed requests
  
  // Parse the HTTP request line to extract the URL
  size_t end_of_line = request.find("\r\n");
  if (end_of_line == std::string::npos) {
    DEBUG("Malformed request: No line ending found");
    send_response(client, server_fd, response);
  }
  
  std::string first_line = request.substr(0, end_of_line);
  DEBUG("Extracted first line: " << first_line);
  
  // HTTP request format: METHOD URL HTTP_VERSION
  size_t method_end = first_line.find(" ");
  size_t url_end = first_line.find(" ", method_end + 1);
  
  if (method_end == std::string::npos || url_end == std::string::npos) {
    DEBUG("Malformed request: Invalid HTTP format");
    send_response(client, server_fd, response);
  }
  
  // Extract the URL between the two spaces
  url = first_line.substr(method_end + 1, url_end - method_end - 1);
  DEBUG("Extracted URL: " << url);
  
  // Handle the URL
  if (url == "/") {
    response = make_response("200 OK", "");
  } else {
    try {
      std::string content = api.handleURL(url);
      response = make_response("200 OK", content);
    } catch (const APINotFoundException& e) {
      DEBUG("API not found: " << e.what());
      response = make_response("404 Not Found", "");
    }
  }

  send_response(client, server_fd, response);
  return 0;
}
