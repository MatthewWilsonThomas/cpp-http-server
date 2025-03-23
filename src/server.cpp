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
#include <fstream>
#include <zlib.h>
#include <pthread.h>


#define DEBUG_ENABLED 0  // Set to 0 to disable debug output
#define DEBUG(x) if (DEBUG_ENABLED) std::cout << "[DEBUG] " << x << std::endl

std::string gzip_encode(const std::string& content) {
  // Initialize zlib stream
  z_stream zs;
  memset(&zs, 0, sizeof(zs));
  
  if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 
                  15 + 16, // 15 window bits + 16 for gzip header
                  8, Z_DEFAULT_STRATEGY) != Z_OK) {
    return content;  // Return unmodified on error
  }

  // Set up input
  zs.next_in = (Bytef*)content.data();
  zs.avail_in = content.size();

  // Prepare output buffer (compressed data is usually smaller, but allocate conservatively)
  int buffer_size = content.size() * 1.1 + 12;  // Add some extra space
  char* output_buffer = new char[buffer_size];
  zs.next_out = (Bytef*)output_buffer;
  zs.avail_out = buffer_size;

  // Compress
  deflate(&zs, Z_FINISH);
  
  // Create result string from buffer
  std::string compressed_data(output_buffer, zs.total_out);
  
  // Clean up
  deflateEnd(&zs);
  delete[] output_buffer;
  
  return compressed_data;
}

std::string removeSpaces(std::string str) {
    str.erase(std::remove(str.begin(), str.end(), ' '), str.end());
    return str;
}

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

class HTTPResponse {
  public:
    std::string status_code;
    std::string content;
    std::string content_type;
    std::vector<std::string> encoding; // "gzip" or ""

    std::vector<std::string> acceptable_encodings = {"gzip", ""};  

    std::string to_string() {
      std::string output;
      std::string encoding_to_use;
      if (this->encoding.size() > 0) {
        if (std::find(this->encoding.begin(), this->encoding.end(), "gzip") != this->encoding.end()) {
          output = gzip_encode(this->content);
          encoding_to_use = "gzip";
        }
      } else {
        output = this->content;
      }

      std::string response = "HTTP/1.1 " + this->status_code + "\r\n";
      if (this->content_type != "") {response += "Content-Type: " + this->content_type + "\r\n";}
      if (encoding_to_use != "") {response += "Content-Encoding: " + encoding_to_use + "\r\n";}
      if (output != "") {response += "Content-Length: " + std::to_string(output.length()) + "\r\n";}
      response += "\r\n";
      if (output != "") {response += output;}
      return response;
    }

    std::string list_encodings() {
      std::string output;
      for (std::string encoding : this->encoding) {
        output += encoding + ", ";
      }
      return output;
    }
};

int send_response(int client_socket, HTTPResponse response) {
  send(client_socket, response.to_string().c_str(), response.to_string().size(), 0);
  DEBUG("Response sent: " << response.to_string());
  return 0;
}

std::vector<std::string> split_string(const std::string& str, const std::string& delimiter) {
    std::vector<std::string> tokens;
    size_t pos = 0;
    size_t prev = 0;
    
    while ((pos = str.find(delimiter, prev)) != std::string::npos) {
        tokens.push_back(str.substr(prev, pos - prev));
        prev = pos + delimiter.length();
    }
    
    // Add the last token
    if (prev < str.length()) {
        tokens.push_back(str.substr(prev));
    }
    
    return tokens;
}

class RequestParser {
  public:
    std::string method;
    std::string url;
    std::unordered_map<std::string, std::string> content_map;
    std::string body;
    
    RequestParser(const std::string& request) {
      std::string header, body;
      std::vector<std::string> parts = split_string(request, "\r\n\r\n");
      if (parts.size() >= 2) {
        header = parts[0];
        body = parts[1];
      } else if (parts.size() == 1) {
        header = parts[0];
        // No body
      }

      std::vector<std::string> h_lines = split_string(header, "\r\n");
      if (h_lines.empty()) return;
      
      std::vector<std::string> request_parts = split_string(h_lines[0], " ");
      if (request_parts.size() >= 2) {
        this->method = request_parts[0];
        this->url = request_parts[1];
      }
      
      for (size_t i = 1; i < h_lines.size(); i++) {
        std::string line = h_lines[i];
        if (line.empty()) continue;
        
        std::vector<std::string> parts = split_string(line, ": ");
        if (parts.size() >= 2) {
          this->content_map[parts[0]] = parts[1];
        }
      }

      if (!body.empty()) {
        this->body = body;
      }

      DEBUG("Method: " << method);
      DEBUG("URL: " << url);
      for (auto const& pair : this->content_map) {
        DEBUG("Header: " << pair.first << " = " << pair.second);
      }
      DEBUG("Body: " << body);
    }
};

std::string directory = "";

class API {
public:
    RequestParser request_parser;
    HTTPResponse response;

    API(const RequestParser& request_parser) : request_parser(request_parser) {
    }

    /**
     * Handles API calls based on a URL string
     * @param url The URL string to process
     * @return Content string if applicable, empty string if no response needed
     */
    HTTPResponse getResponse() {
        DEBUG("Processing URL: " + request_parser.url); 
        if (request_parser.content_map.find("Accept-Encoding") != request_parser.content_map.end()) {
          for (std::string encoding : split_string(removeSpaces(request_parser.content_map["Accept-Encoding"]), ",")) {
            if (std::find(this->response.acceptable_encodings.begin(), this->response.acceptable_encodings.end(), encoding) != this->response.acceptable_encodings.end()) {
              this->response.encoding.push_back(encoding);
            }
          }
        }
        DEBUG("Encodings: " + response.list_encodings());

        if (request_parser.url.find("/echo/") == 0) {
            return echo();
        } 
        if (request_parser.url.find("/files/") == 0) {
            return files();
        }
        if (request_parser.url.find("/user-agent") == 0) {
            return user_agent();
        }
        if (request_parser.url == "/") {
            this->response.status_code = "200 OK";
            return this->response;
        }
        throw APINotFoundException(request_parser.url);
    }
    HTTPResponse echo() {
        DEBUG("Echoing from URL: " + request_parser.url);
        this->response.status_code = "200 OK";
        this->response.content = request_parser.url.substr(6);
        this->response.content_type = "text/plain";
        return this->response;
    }
    HTTPResponse user_agent() {
        DEBUG("User-Agent: " + request_parser.content_map["User-Agent"]);
        this->response.status_code = "200 OK";
        this->response.content = request_parser.content_map["User-Agent"];
        this->response.content_type = "text/plain";
        return this->response;
    }
    HTTPResponse files() {
        DEBUG("Files: " + request_parser.url);
        std::string filename = request_parser.url.substr(7);
        std::string filepath = directory.empty() ? filename : directory + "/" + filename;
        
        if (request_parser.method == "GET") {
            std::ifstream file(filepath);
            if (!file.is_open()) {
                this->response.status_code = "404 Not Found";
            } else {
                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                this->response.content = content;
                this->response.content_type = "application/octet-stream";
                this->response.status_code = "200 OK";
            }
        } else if (request_parser.method == "POST") {
            std::ofstream file(filepath, std::ios::binary);
            if (!file.is_open()) {
                this->response.status_code = "404 Not Found";
            } else {
                file.write(request_parser.body.c_str(), request_parser.body.length());
                file.close();
                this->response.status_code = "201 Created";
            }
        } else {
            this->response.status_code = "405 Method Not Allowed";
        }
        return this->response;
    }
};

struct ConnectionData {
    int client_socket;
    struct sockaddr_in client_addr;
};

void* handle_connection(void* arg) {
  ConnectionData* data = (ConnectionData*)arg;
  int client_socket = data->client_socket;
  struct sockaddr_in client_addr = data->client_addr;
  free(data);

  HTTPResponse response;
  response.status_code = "400 Bad Request";

  // Buffer to store the incoming HTTP request
  char buffer[1024] = {0};
  int bytes_read = read(client_socket, buffer, sizeof(buffer));
  if (bytes_read < 0) {
    std::cerr << "Failed to read from client\n";
    send_response(client_socket, response);
    return NULL;
  }

  buffer[bytes_read] = '\0';
  DEBUG("Received HTTP request:\n" << buffer);
  RequestParser request_parser(buffer);

  API api(request_parser);
  
  // Handle the URL
  try {
    response = api.getResponse();
  } catch (const APINotFoundException& e) {
    DEBUG("API not found: " << e.what());
    response.status_code = "404 Not Found";
  } catch (const std::exception& e) {
    DEBUG("Error: " << e.what());
    response.status_code = "500 Internal Server Error";
  }

  send_response(client_socket, response);
  close(client_socket);
  return NULL;
}

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  
  // Parse command line arguments for --directory flag
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--directory" && i + 1 < argc) {
      directory = argv[i + 1];
      DEBUG("Directory set to: " << directory);
      break;
    }
  }
  
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
  DEBUG("Waiting for clients to connect...");
    
  // Main server loop
  while (true) {
      struct sockaddr_in client_addr;
      int client_addr_len = sizeof(client_addr);
      
      int client_socket = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
      if (client_socket < 0) {
          std::cerr << "Failed to accept connection\n";
          continue;
      }
      
      DEBUG("Client connected");
      
      // Create data structure to pass to thread
      ConnectionData* conn_data = new ConnectionData;
      conn_data->client_socket = client_socket;
      conn_data->client_addr = client_addr;
      
      // Create a new thread to handle this connection
      pthread_t thread_id;
      if (pthread_create(&thread_id, NULL, handle_connection, (void*)conn_data) != 0) {
          std::cerr << "Failed to create thread\n";
          close(client_socket);
          delete conn_data;
          continue;
      }
      
      // Detach the thread so its resources are automatically released when it terminates
      pthread_detach(thread_id);
  }

  close(server_fd);
  return 0;
}
