#include <iostream>
#include <cctype>
#include <sys/types.h>
#include <netdb.h>
#include <memory>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>




void fail(const char* filename, const int line) {
  std::cerr<<filename<<":"<<line<<" "<< strerror(errno);
  exit(1);
}

class Client
{
public:
  int socket_desc_;
  addrinfo *serverinfo_;
  Client(const std::string& ip, const std::string& port){
    addrinfo hint;
    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_STREAM;

    auto status = getaddrinfo(ip.c_str(), port.c_str(), &hint, &serverinfo_);
    if(status) {
      std::cerr<<__FILE__<<":"<<__LINE__<<" "<< gai_strerror(status);
      exit(1);
    }

    socket_desc_ = socket(serverinfo_->ai_family, serverinfo_->ai_socktype, serverinfo_->ai_protocol);
    if(socket_desc_ == -1) {
      fail(__FILE__, __LINE__);
    }

    auto error = connect(socket_desc_, serverinfo_->ai_addr, serverinfo_->ai_addrlen);
    if(error == -1){
      fail(__FILE__, __LINE__);
    }
  }
  ~Client() {
    close(socket_desc_);
    freeaddrinfo(serverinfo_);
  }
};


std::string connected_addres(const Client& c) {
  char connected_to[INET_ADDRSTRLEN];
  inet_ntop(c.serverinfo_->ai_family,
            c.serverinfo_->ai_addr,
            connected_to, sizeof(connected_to));
  return std::string(connected_to);
}

void send_string(int socket, std::string text) {
  auto error = send(socket, text.c_str(), text.size(), 0);
  if(error == -1) {
    fail(__FILE__, __LINE__);
  }
  std::cout<<"[Client]: sent message:" <<text<<"\n";
}

std::string recv_string(int socket) {
  char buffer[256];
  auto error = recv(socket, buffer, sizeof(buffer)-1, 0);
  if(error == -1) {
    fail(__FILE__, __LINE__);
  }
  buffer[error]='\0';
  std::cout<<"[Client]: got message:" <<buffer<<"\n";
  return std::string(buffer);
}

int main(int argc, char *argv[]) {
  if(argc != 3) {
    std::cout<<"Usage: "<<argv[0]<<" <server ip address> <port>";
    std::cout<<"Default server port is 1024 and proxy port 1025\n";
    return 0;
  }
  else {
    auto client = Client(argv[1], argv[2]);
    std::cout<<"[Client]: connected to: " <<connected_addres(client)<<"\n";

    send_string(client.socket_desc_, "Ping");
    recv_string(client.socket_desc_);
    return 0;
  }
}
