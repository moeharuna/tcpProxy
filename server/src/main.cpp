#include <iostream>
#include <cctype>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <memory>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>

const std::string CONNECTION_IS_ENDED =  "";

void fail(const char* filename, const int line) {
  std::cerr<<filename<<":"<<line<<" "<< strerror(errno);
  exit(1);
}


void send_string(int socket, std::string text) {
  auto error = send(socket, text.c_str(), text.size(), 0);
  if(error == -1) {
    fail(__FILE__, __LINE__);
  }
}

std::string recv_string(int socket) {
  char buffer[256];
  auto error = recv(socket, buffer, sizeof(buffer)-1, 0);
  if(error == -1) {
    fail(__FILE__, __LINE__);
  }
  if(error == 0) {
    return CONNECTION_IS_ENDED;
  }
  buffer[error] = '\0';
  return std::string(buffer);
}

std::string get_ip(sockaddr_in* addr) {
  char ip_buffer[INET_ADDRSTRLEN];
  inet_ntop(addr->sin_family, addr,
            ip_buffer, sizeof(ip_buffer));
  return std::string(ip_buffer);
}

class Server;
class ClientSocket {
  sockaddr_storage addr_;
  int socket_;
public:
  ClientSocket(const int listen_socket) {
    socklen_t size = sizeof(addr_);
    socket_ = accept(listen_socket,
                     (sockaddr*)&addr_, &size);
    if(socket_ == -1) {
      fail(__FILE__, __LINE__);
    }

  }
  ~ClientSocket() {
    close(socket_);
  }

  std::string get_ip() {
    return ::get_ip((sockaddr_in*)&addr_);
  }

  bool handle() {
    auto str = recv_string(socket_);

    if(str==CONNECTION_IS_ENDED)
      return false;
    std::cout<<"[Server]: recived:"<<str<<"\n";

    auto sent_msg = "Pong";
    send_string(socket_, sent_msg);
    std::cout<<"[Server]: sent message: " <<sent_msg << "\n";
    return true;
  }
};
class Server {
public:
  int listen_socket_;
  addrinfo *serverinfo_;

  Server(const std::string& port) {
    addrinfo hint;
    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags = AI_PASSIVE;


    auto status = getaddrinfo(NULL, port.c_str(), &hint, &serverinfo_);
    if(status) {
      std::cerr<<__FILE__<<":"<<__LINE__<<" "<< gai_strerror(status);
      exit(1);
    }

    listen_socket_ = socket(serverinfo_->ai_family, serverinfo_->ai_socktype, serverinfo_->ai_protocol);
    if(listen_socket_ == -1) {
      fail(__FILE__, __LINE__);
    }

    int option = 1;
    auto error = setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR,
                            &option, sizeof(int));
    if(error == -1) {
      fail(__FILE__, __LINE__);
    }

    error = bind(listen_socket_, serverinfo_->ai_addr, serverinfo_->ai_addrlen);
    if(error == -1) {
      fail(__FILE__, __LINE__);
    }

    error = listen(listen_socket_, 0);
    if (error == -1) {
      fail(__FILE__, __LINE__);
    }
  }


  ClientSocket* accept_client() {
    return new ClientSocket(this->listen_socket_);
  }


  ~Server() {
    freeaddrinfo(serverinfo_);
    close(listen_socket_);
  }
};






const std::string SERVER_PORT = "1024";

int main(int argc, char *argv[]) {
  auto server = Server(SERVER_PORT);
  std::cout<<"[Server]: Listening to connections"<<std::endl;
  auto client = server.accept_client();
  std::cout << "[Server]: got connection from: " << client->get_ip() << "\n";
  while(true) {
    if(!client->handle()) {
      delete client;
      client = server.accept_client();
      std::cout << "[Server]: got connection from: " << client->get_ip() << "\n";
    }

  }
  return 0;
}
