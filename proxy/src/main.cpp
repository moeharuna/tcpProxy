#include <fstream>
#include <iostream>
#include <vector>
#include <cctype>
#include <sys/types.h>
#include <netdb.h>
#include <memory>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <exception>
#include <fcntl.h>
#include <cstring>
#include <cassert>
#include <errno.h>
#include <thread>
#include <poll.h>
#include <ctime>

const size_t BUFFER_SIZE   = 512;
const std::string SERVER_PORT  = "1024";
const std::string PROXY_PORT  = "1025";
const std::string CONNECTION_IS_ENDED =  "";
enum CExceptionType {
  STDLIB,
  GAI,
};
//This class turns c error codes into C++ exceptions.
//It also takes additional context that caller can pass
//Probably not smartest way to represent errors. But i need to
//make big constructor for RAII with c sockets and i can't return
//error codes from constuctors so i came up with this solution.
class CException : std::exception {
  char  * context;
  const static size_t context_size = 128;
  const static size_t error_size = 1024;
  int error_code;
  CExceptionType type;

  public:
  CException(int code, CExceptionType t, const std::string &context="") :error_code(code), type(t) {
    this->context = (char*) malloc(error_size*sizeof(char));
    strncpy(this->context, context.c_str(), context_size);
  }
  const char *what() const noexcept override {
    switch(type) {
      case STDLIB:
        std::strncat(context, strerror(error_code), error_size);
        break;
      case GAI:
        std::strncat(context, gai_strerror(error_code), error_size);
        break;
      default:
        assert(false);
        break;//unreachable code
    }
    return context;
  }
};

//accept socket for server. I made this into separate class for RAII
class SlaveClient {
  int slave_socket_;
  sockaddr_storage clients_addr_;
  public:
  SlaveClient(const int listen_socket) {
    socklen_t size = sizeof(clients_addr_);
    slave_socket_ = accept(listen_socket,
                             (sockaddr*)&clients_addr_, &size);
    if(slave_socket_ == -1) { throw CException(errno, STDLIB, "accept "); }
  }
  int get_socket() const {
    return slave_socket_;
  }
  ~SlaveClient() {
    close(slave_socket_);
  }
};
//TCP Server that accepts connections from client
//A lot of work for this and other tcp classes done in constructors
//to make sure that socket and addrinfo is always valid
class Server {
  int listen_socket_;
  addrinfo *server_info_;
public:
  Server(const std::string& port) {
    addrinfo hint;
    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags = AI_PASSIVE;


    auto status = getaddrinfo(NULL, port.c_str(), &hint, &server_info_);
    if(status) {throw CException(status, GAI, "getaddrinfo ");}

    listen_socket_ = socket(server_info_->ai_family,
                            server_info_->ai_socktype,
                            server_info_->ai_protocol);
    if(listen_socket_ == -1) { throw CException(errno, STDLIB, "socket ");}

    int option = 1;
    auto error = setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR,
                            &option, sizeof(int));
    if(error == -1) {throw CException(errno, STDLIB, "setsockopt ");}

    error = bind(listen_socket_,
                 server_info_->ai_addr,
                 server_info_->ai_addrlen);
    if(error == -1) {throw CException(errno, STDLIB, "bind ");}

    error = listen(listen_socket_, 0);
    if (error == -1) {throw CException(errno, STDLIB, "listen ");}
  }

  SlaveClient* wait_for_connection() const {
    return new SlaveClient(this->listen_socket_);
  }

  ~Server() {
    freeaddrinfo(server_info_);
    close(listen_socket_);
  }
};
//TCP client that connects to "real" server
class Client {
  int socket_;
  addrinfo *server_info_;
public:
  Client(const std::string& ip, const std::string& port) {
    addrinfo hint;
    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_STREAM;

    auto status = getaddrinfo(ip.c_str(), port.c_str(),
                              &hint,
                              &server_info_);
    if(status) { throw CException(status, GAI, "getaddrinfo "); }

    socket_= socket(server_info_->ai_family,
                    server_info_->ai_socktype,
                    server_info_->ai_protocol);
    if(socket_ == -1) { throw CException(errno, STDLIB, "socket "); }

    auto error = connect(socket_,
                         server_info_->ai_addr,
                         server_info_->ai_addrlen);
    if(error == -1) { throw CException(errno, STDLIB, "connect " ); }
  }
  ~Client() {
    close(socket_);
    freeaddrinfo(server_info_);
  }
  int get_socket() const {
    return socket_;
  }
};


class Logger {
  public:
  virtual void log(const std::string& msg) = 0;
};
//send_string is just send from posix but with c++ std::string
void send_string(int socket, const std::string& text) {
  auto error = send(socket, text.c_str(), text.size(), 0);
  if(error == -1) {throw CException(errno, STDLIB, "socket");}
}
//recv_string is just recv from posix but with c++ std::string
std::string recv_string(int socket) {
  std::string result{};
  char buffer[BUFFER_SIZE];
  auto recv_count =0;
  do {
    recv_count = recv(socket, buffer, sizeof(buffer)-1, 0);
    if(recv_count == -1) {throw CException(errno, STDLIB, "recv");}
    if(recv_count == 0) return CONNECTION_IS_ENDED;
    buffer[recv_count]='\0';
  } while(recv_count==sizeof(buffer)-1);
  result +=buffer;
  return result;
}






//Proxy itself
//Its implimentation is quite naive. Proxy is blocking and can server only one client
//Proxy consists of server that takes connection from "real" client
//and client that connects to "real" server
class Proxy {
private:
  Client connectionToServer_;
  Server connectionToClient_;
  std::shared_ptr<Logger> logger_;
public:
  Proxy(const std::string& server_ip,
        const std::string& port,
        std::shared_ptr<Logger>&& l ) :connectionToServer_(server_ip, SERVER_PORT),
                                       connectionToClient_(port),
                                       logger_(move(l)) {};

//In this function proxy polls for input events server and client
//in infinte loop.
//When server or client sends some data, proxy logs it and
//sends it to destination.
  void proxy_loop() {
    int server_socket = connectionToServer_.get_socket();
    SlaveClient* slave = connectionToClient_.wait_for_connection();
    auto slave_socket = slave->get_socket();

    pollfd poll_client, poll_server;
    poll_client.fd = slave_socket;
    poll_client.events = POLLIN;

    poll_server.fd = server_socket;
    poll_server.events = POLLIN;

    while(true) {

      int error = poll(&poll_client, 1, 50);
      if(error==-1) throw CException(errno, STDLIB, "poll_client");
      //got message form client
      if((poll_client.revents & POLLIN) != 0) {
        std::string client_recv = "";
        client_recv = recv_string(slave_socket);

        if(client_recv!=CONNECTION_IS_ENDED) {
          logger_->log("Got message " + client_recv + " from client");
          send_string(server_socket, client_recv);
          logger_->log("Send message " + client_recv + " to server");
        } else {
          //connection to client ended
          delete slave;
          slave = connectionToClient_.wait_for_connection();
          slave_socket = slave->get_socket();
          poll_client.fd = slave_socket;
        }
      }
      poll_client.revents = 0;

      error = poll(&poll_server, 1, 50);
      if(error==-1) throw CException(errno, STDLIB, "poll_server");
      //got message from server
      if((poll_server.revents & POLLIN) != 0) {
        std::string server_recv = "";
        server_recv = recv_string(server_socket);

        if(server_recv!=CONNECTION_IS_ENDED) {
          logger_->log("Got message " + server_recv + " from server");
          send_string(slave_socket, server_recv);
          logger_->log("Send message " + server_recv + " to client");
        } else {
          //connection to server ended
          logger_->log("Connection to server is ended!");
          throw "Connection to server is ended";
        }
      }
      poll_server.revents = 0;
    }
  }
};

class StdinLogger : public Logger {
  public:
  StdinLogger() {}
  void log(const std::string& msg) {
    std::cout<<"[Proxy]:"<<msg<<"\n";
  }
};

class FileLogger : public Logger {
  private:
  std::ofstream out;
  public:
  FileLogger(const std::string& file_name) :out(file_name) {}
  void log(const std::string &msg ) {
    std::time_t now = std::time(nullptr);
    char time_str[100];
    std::strftime(time_str, sizeof(time_str),
                  "%d-%b-%Y %X", std::localtime(&now));
    out<<"["<<time_str<<"]" <<msg<<"\n";
    out.flush(); // I flush here because deconsturctor probably
                 // wont be called because only way to end progarm is
                 // to send some sort of a kill signal
  }
  ~FileLogger() {
    out.flush();
  }
};

int main(int argc, char *argv[]) {
  if(argc != 2) {
    std::cout<<"Usage: "<<argv[0]<<" <server ip address>";
    return 0;
  }

  try {
    const std::string port = PROXY_PORT;
    const std::string server_ip = argv[1];
    std::shared_ptr<Logger> logger_ptr = std::make_shared<FileLogger>("log.txt");
    Proxy proxy(server_ip, port, std::move(logger_ptr));
    proxy.proxy_loop();
  } catch(const CException &e) {
    std::cout<<e.what()<<"\n";
  }
  catch(const std::exception &e) {
    std::cout<<e.what()<<"\n";
  }
  catch(const std::string &s) {
    std::cout<<s<<"\n";
  }

  return 0;

}
