#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sstream>
#include <vector>
#include <unordered_map>

// Debug tag for std::out statements with toggle functionality
#define DEBUG_ENABLED 0  // Set to 0 to disable debug output
#define DEBUG(x) if (DEBUG_ENABLED) std::cout << "[DEBUG] " << x << std::endl

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

std::vector<std::string> split(const std::string &message, const std::string& delim) {
  std::vector<std::string> tokens;
  std::stringstream ss = std::stringstream{message};
  std::string line;
  while (getline(ss, line, *delim.begin())) {
    tokens.push_back(line);
    ss.ignore(delim.length() - 1);
  }
  return tokens;
}

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

class RequestParser {
  public:
    std::string url;
    std::string method;
    std::unordered_map<std::string, std::string> content_map;
    
    RequestParser(const std::string& request) {
      std::vector<std::string> lines = split(request, "\r\n");
      if (lines.empty()) return;
      
      std::vector<std::string> request_parts = split(lines[0], " ");
      if (request_parts.size() >= 2) {
        this->method = request_parts[0];
        this->url = request_parts[1];
      }
      
      for (size_t i = 1; i < lines.size(); i++) {
        std::string line = lines[i];
        if (line.empty()) continue;
        
        std::vector<std::string> parts = split(line, ": ");
        if (parts.size() >= 2) {
          this->content_map[parts[0]] = parts[1];
        }
      }

      DEBUG("Method: " << method);
      DEBUG("URL: " << url);
      for (auto const& pair : this->content_map) {
        DEBUG("Line: " << pair.first << " " << pair.second);
      }
    }
};

class API {
public:
    RequestParser request_parser;

    API(const RequestParser& request_parser) : request_parser(request_parser) {
    }

    /**
     * Handles API calls based on a URL string
     * @param url The URL string to process
     * @return Content string if applicable, empty string if no response needed
     */
    std::string getResponse() {
        DEBUG("Processing URL: " + request_parser.url);     
        
        if (request_parser.url.find("/echo/") == 0) {
            return echo();
        } 
        if (request_parser.url.find("/user-agent") == 0) {
            return user_agent();
        }
        if (request_parser.url == "/") {
            return "";
        }
        throw APINotFoundException(request_parser.url);
    }
    std::string echo() {
      return request_parser.content_map["URL"].substr(6);
    }
    std::string user_agent() {
        return request_parser.content_map["User-Agent"];
    }
};

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

  std::string response;
  response = make_response("400 Bad Request", ""); // Default response for malformed requests

  DEBUG("Waiting for a client to connect...");
  
  int client = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);

  // Buffer to store the incoming HTTP request
  char buffer[1024] = {0};
  int bytes_read = read(client, buffer, sizeof(buffer));
  if (bytes_read < 0) {
    std::cerr << "Failed to read from client\n";
    send_response(client, server_fd, response);
    return 1;
  }

  buffer[bytes_read] = '\0';
  DEBUG("Received HTTP request:\n" << buffer);
  RequestParser request_parser(buffer);

  API api(request_parser);

  
  // Handle the URL
  try {
    std::string content = api.getResponse();
    response = make_response("200 OK", content);
  } catch (const APINotFoundException& e) {
    DEBUG("API not found: " << e.what());
    response = make_response("404 Not Found", "");
  }

  send_response(client, server_fd, response);
  return 0;
}
