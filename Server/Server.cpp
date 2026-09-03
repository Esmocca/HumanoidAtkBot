// Server code (ported from Arduino/Pico W to Linux socket server)
// Logika komunikasi antar client (mainServer) TIDAK diubah,
// hanya bagian koneksi (WiFi -> TCP socket) dan tampilan (LCD -> stdout) yang disesuaikan.

#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

int port = 5000; //socket listening in port

int server_fd = -1;

// ClientData
struct ClientData {
  int sock;
  std::string name;
  // int lifePoints = 3;
};

// DoublyLinkedList for storing a list of clients
template <class T>
class DNode {
public:
  T data;
  DNode<T>* next;
  DNode<T>* prev;
  DNode(const T& d, DNode<T>* p = nullptr, DNode<T>* n = nullptr)
    : data(d), prev(p), next(n) {}
};

template <class T>
class DoublyLinkedList {
private:
  DNode<T>* head;
  DNode<T>* tail;
public:
  DoublyLinkedList() : head(nullptr), tail(nullptr) {}
  ~DoublyLinkedList() {
    while (head) {
      DNode<T>* tmp = head;
      head = head->next;
      delete tmp;
    }
  }
  void insertBack(const T& data) {
    DNode<T>* newNode = new DNode<T>(data, tail, nullptr);
    if (tail) {
      tail->next = newNode;
    } else {
      head = newNode;
    }
    tail = newNode;
  }
  void removeNode(DNode<T>* node) {
    if (!node) return;
    if (node->prev)
      node->prev->next = node->next;
    else
      head = node->next;
    if (node->next)
      node->next->prev = node->prev;
    else
      tail = node->prev;
    delete node;
  }
  DNode<T>* getHead() {
    return head;
  }
  int count() {
    int c = 0;
    DNode<T>* temp = head;
    while (temp) {
      c++;
      temp = temp->next;
    }
    return c;
  }
};

DoublyLinkedList<ClientData> clientList;
bool gameStarted = false;
bool serverReady = false;

//  Setup socket
void setNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// cari IPv4 non-loopback pertama
std::string getLocalIP() {
  struct ifaddrs* ifaddr;
  std::string result = "unknown";

  if (getifaddrs(&ifaddr) == -1) {
    return result;
  }

  for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr) continue;
    if (ifa->ifa_addr->sa_family != AF_INET) continue;
    if (ifa->ifa_flags & IFF_LOOPBACK) continue;

    std::string name(ifa->ifa_name);
    if (name.rfind("docker", 0) == 0 || name.rfind("br-", 0) == 0 || name.rfind("veth", 0) == 0) {
      continue;
    }

    char buf[INET_ADDRSTRLEN];
    void* addr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
    inet_ntop(AF_INET, addr, buf, sizeof(buf));
    result = buf;
    break;
  }

  freeifaddrs(ifaddr);
  return result;
}

void setupServer() {
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "Gagal membuat socket" << std::endl;
    exit(1);
  }

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
    std::cerr << "Gagal bind socket ke port " << port << std::endl;
    exit(1);
  }

  if (listen(server_fd, 4) < 0) {
    std::cerr << "Gagal listen" << std::endl;
    exit(1);
  }

  setNonBlocking(server_fd);

  std::string ip = getLocalIP();
  std::cout << "Server berjalan di IP: " << ip << " Port: " << port << std::endl;
}

// displayServerReady
void displayServerReady() {
  std::cout << "Server Ready!" << std::endl;
  serverReady = true;
}

// mainServer for handling robot communication
void mainServer() {
  // Get new client
  sockaddr_in client_addr{};
  socklen_t addrlen = sizeof(client_addr);
  int newSock = accept(server_fd, (sockaddr*)&client_addr, &addrlen);

  if (newSock >= 0) {
    setNonBlocking(newSock);
    std::string clientName = "Player_" + std::to_string(rand() % 99 + 1);
    ClientData cd;
    cd.sock = newSock;
    cd.name = clientName;
    clientList.insertBack(cd);
    std::cout << "Client terhubung: " << clientName << std::endl;
  }

  if (clientList.count() == 2 && !gameStarted) {
    displayServerReady();
  }

  DNode<ClientData>* current = clientList.getHead();
  while (current != nullptr) {
    int sock = current->data.sock;
    bool disconnected = false;

    char buffer[512];
    ssize_t n = recv(sock, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);

    if (n > 0) {
      buffer[n] = '\0';
      std::string data(buffer);
      // data.trim()
      while (!data.empty() && (data.back() == '\n' || data.back() == '\r')) {
        data.pop_back();
      }
      std::cout << "Received: " << data << std::endl;

      DNode<ClientData>* receiver = clientList.getHead();
      while (receiver != nullptr) {
        if (receiver != current) {
          std::string msg = data + "\n";
          send(receiver->data.sock, msg.c_str(), msg.size(), 0);
        }
        receiver = receiver->next;
      }
    } else if (n == 0) {
      // client menutup koneksi
      disconnected = true;
    } else {
      // n < 0
      if (errno != EWOULDBLOCK && errno != EAGAIN) {
        disconnected = true;
      }
    }

    if (disconnected) {
      close(sock);
      DNode<ClientData>* toDelete = current;
      current = current->next;
      clientList.removeNode(toDelete);
    } else {
      current = current->next;
    }
  }
}


int main() {
  srand(static_cast<unsigned int>(time(nullptr)));

  setupServer();

  while (true) {
    mainServer();
  }

  return 0;
}
