// Minimal loopback-only TCP control server. This lets the Python/PySide6
// desktop launcher (see launcher/) query simulation status and change a
// handful of parameters (freeze, haptic mode, potential, anchors) while
// haptic-device is running, without requiring any third-party networking or
// JSON library on the C++ side.
//
// Protocol: newline-delimited plain-text commands, one text response per
// command, also newline-terminated. See handleCommand() below for the exact
// grammar.
#include "ipcServer.h"
#include "globals.h"
#include "inputHandling.h"
#include "potentials.h"

#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET SocketHandle;
  static const SocketHandle kInvalidSocket = INVALID_SOCKET;
  #define CLOSESOCKET closesocket
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  typedef int SocketHandle;
  static const SocketHandle kInvalidSocket = -1;
  #define CLOSESOCKET close
#endif

using namespace std;

namespace {

thread ipcThread;
atomic<bool> ipcRunning(false);
atomic<SocketHandle> listenSocket(kInvalidSocket);

string toLower(string value) {
  for (char &c : value) {
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

string modeToString(HapticMode mode) {
  switch (mode) {
    case HapticMode::Force: return "force";
    case HapticMode::Standby: return "standby";
    case HapticMode::Position: default: return "position";
  }
}

string buildStatus() {
  int anchored = 0;
  size_t atomCount;
  {
    lock_guard<recursive_mutex> lock(sceneMutex);
    atomCount = spheres.size();
    for (Atom *atom : spheres) {
      if (atom->isAnchor()) {
        anchored++;
      }
    }
  }

  string potentialStr = "lj";
  if (energySurface == MORSE) {
    potentialStr = "morse";
  } else if (energySurface == ASE) {
    potentialStr = "ase";
  }

  ostringstream out;
  out << "OK STATUS mode=" << modeToString(hapticMode.load())
      << " freeze=" << (freezeAtoms.load() ? "true" : "false")
      << " potential=" << potentialStr
      << " atoms=" << atomCount
      << " anchored=" << anchored
      << " energy=" << displayedPotentialEnergy.load()
      << " timestep=" << simulationTimeStep.load()
      << " render_atoms=" << (renderAtoms.load() ? "true" : "false")
      << " render_forces=" << (renderForceVectors.load() ? "true" : "false")
      << " render_bonds=" << (renderBonds.load() ? "true" : "false")
      << " settling_err=" << settlingError.load()
      << " k_return=" << kReturn.load()
      << " k_dampen=" << kDampen.load()
      << " return_delay=" << returnDelaySeconds.load()
      << " force_scale=" << hapticForceScale.load();
  return out.str();
}

// Handles a single command line and returns the response line (without the
// trailing newline, which the caller appends before sending).
string handleCommand(const string &line) {
  istringstream stream(line);
  string command;
  stream >> command;
  command = toLower(command);

  if (command.empty()) {
    return "ERR empty command";
  } else if (command == "ping") {
    return "OK PONG";
  } else if (command == "status") {
    return buildStatus();
  } else if (command == "set") {
    string key, value;
    stream >> key >> value;
    key = toLower(key);
    value = toLower(value);
    if (key == "freeze") {
      if (value != "true" && value != "false") {
        return "ERR freeze must be true or false";
      }
      freezeAtoms.store(value == "true");
      return "OK";
    } else if (key == "mode") {
      if (value == "force") {
        hapticMode.store(HapticMode::Force);
      } else if (value == "position") {
        hapticMode.store(HapticMode::Position);
      } else if (value == "standby") {
        hapticMode.store(HapticMode::Standby);
      } else {
        return "ERR mode must be force, position, or standby";
      }
      return "OK";
    } else if (key == "potential") {
      if (setLivePotential(value)) {
        return "OK";
      }
      return "ERR live potential switch only supports lj or morse";
    } else if (key == "timestep") {
      try {
        size_t consumed = 0;
        double seconds = stod(value, &consumed);
        if (consumed != value.size() || !setLiveTimeStep(seconds)) {
          return "ERR timestep must be a number between " +
                 to_string(MIN_SIMULATION_TIME_STEP) + " and " +
                 to_string(MAX_SIMULATION_TIME_STEP);
        }
      } catch (const exception &) {
        return "ERR timestep must be a valid number";
      }
      return "OK";
    } else if (key == "render_atoms") {
      if (value != "true" && value != "false") {
        return "ERR render_atoms must be true or false";
      }
      renderAtoms.store(value == "true");
      return "OK";
    } else if (key == "render_forces") {
      if (value != "true" && value != "false") {
        return "ERR render_forces must be true or false";
      }
      renderForceVectors.store(value == "true");
      return "OK";
    } else if (key == "render_bonds") {
      if (value != "true" && value != "false") {
        return "ERR render_bonds must be true or false";
      }
      renderBonds.store(value == "true");
      return "OK";
    } else if (key == "settling_err") {
      try {
        size_t consumed = 0;
        double parsed = stod(value, &consumed);
        if (consumed != value.size() || !setLiveSettlingError(parsed)) {
          return "ERR settling_err must be a number between " +
                 to_string(MIN_SETTLING_ERROR) + " and " + to_string(MAX_SETTLING_ERROR);
        }
      } catch (const exception &) {
        return "ERR settling_err must be a valid number";
      }
      return "OK";
    } else if (key == "k_return") {
      try {
        size_t consumed = 0;
        double parsed = stod(value, &consumed);
        if (consumed != value.size() || !setLiveKReturn(parsed)) {
          return "ERR k_return must be a number between " +
                 to_string(MIN_K_RETURN) + " and " + to_string(MAX_K_RETURN);
        }
      } catch (const exception &) {
        return "ERR k_return must be a valid number";
      }
      return "OK";
    } else if (key == "k_dampen") {
      try {
        size_t consumed = 0;
        double parsed = stod(value, &consumed);
        if (consumed != value.size() || !setLiveKDampen(parsed)) {
          return "ERR k_dampen must be a number between " +
                 to_string(MIN_K_DAMPEN) + " and " + to_string(MAX_K_DAMPEN);
        }
      } catch (const exception &) {
        return "ERR k_dampen must be a valid number";
      }
      return "OK";
    } else if (key == "return_delay") {
      try {
        size_t consumed = 0;
        double parsed = stod(value, &consumed);
        if (consumed != value.size() || !setLiveReturnDelay(parsed)) {
          return "ERR return_delay must be a number between " +
                 to_string(MIN_RETURN_DELAY_SECONDS) + " and " + to_string(MAX_RETURN_DELAY_SECONDS);
        }
      } catch (const exception &) {
        return "ERR return_delay must be a valid number";
      }
      return "OK";
    } else if (key == "force_scale") {
      try {
        size_t consumed = 0;
        double parsed = stod(value, &consumed);
        if (consumed != value.size() || !setLiveForceScale(parsed)) {
          return "ERR force_scale must be a number between " +
                 to_string(MIN_FORCE_SCALE) + " and " + to_string(MAX_FORCE_SCALE);
        }
      } catch (const exception &) {
        return "ERR force_scale must be a valid number";
      }
      return "OK";
    }
    return "ERR unknown setting '" + key + "'";
  } else if (command == "anchor_all") {
    anchorAllAtoms();
    return "OK";
  } else if (command == "unanchor_all") {
    unanchorAllAtoms();
    return "OK";
  } else if (command == "next_atom") {
    switchCurrentAtom();
    return "OK";
  } else if (command == "next_camera") {
    switchCamera();
    return "OK";
  }
  return "ERR unknown command '" + command + "'";
}

// Applies a short receive timeout to a connected client socket so the
// server loop can periodically re-check ipcRunning and shut down promptly
// even while a client connection is idle.
void setClientReceiveTimeout(SocketHandle client, int milliseconds) {
#ifdef _WIN32
  DWORD timeout = static_cast<DWORD>(milliseconds);
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
             reinterpret_cast<const char *>(&timeout), sizeof(timeout));
#else
  struct timeval timeout{};
  timeout.tv_sec = milliseconds / 1000;
  timeout.tv_usec = (milliseconds % 1000) * 1000;
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
}

bool wasReceiveTimeout() {
#ifdef _WIN32
  return WSAGetLastError() == WSAETIMEDOUT;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

void serviceClient(SocketHandle client) {
  setClientReceiveTimeout(client, 500);
  string buffer;
  char chunk[512];

  while (ipcRunning.load()) {
    int received = recv(client, chunk, sizeof(chunk), 0);
    if (received > 0) {
      buffer.append(chunk, static_cast<size_t>(received));

      size_t newlinePos;
      while ((newlinePos = buffer.find('\n')) != string::npos) {
        string line = buffer.substr(0, newlinePos);
        buffer.erase(0, newlinePos + 1);
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        string response = handleCommand(line) + "\n";
        send(client, response.c_str(), static_cast<int>(response.size()), 0);
      }
      continue;
    }
    if (received == 0) {
      break; // client disconnected
    }
    if (wasReceiveTimeout()) {
      continue; // no data yet; loop back around to re-check ipcRunning
    }
    break; // real socket error
  }

  CLOSESOCKET(client);
}

void serverLoop(int port) {
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    cerr << "IPC server: WSAStartup failed, remote control disabled." << endl;
    return;
  }
#endif

  SocketHandle sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == kInvalidSocket) {
    cerr << "IPC server: failed to create socket, remote control disabled." << endl;
    return;
  }
  listenSocket.store(sock);

  int reuse = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char *>(&reuse), sizeof(reuse));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = inet_addr("127.0.0.1");
  address.sin_port = htons(static_cast<unsigned short>(port));

  if (::bind(sock, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
    cerr << "IPC server: failed to bind to 127.0.0.1:" << port
         << ", remote control disabled." << endl;
    CLOSESOCKET(sock);
    listenSocket.store(kInvalidSocket);
    return;
  }

  if (listen(sock, 1) != 0) {
    cerr << "IPC server: failed to listen, remote control disabled." << endl;
    CLOSESOCKET(sock);
    listenSocket.store(kInvalidSocket);
    return;
  }

  cout << "IPC server: listening on 127.0.0.1:" << port << endl;

  while (ipcRunning.load()) {
    SocketHandle client = accept(sock, nullptr, nullptr);
    if (client == kInvalidSocket) {
      break; // listening socket was closed by stopIpcServer() to unblock accept()
    }
    serviceClient(client);
  }

  if (listenSocket.exchange(kInvalidSocket) != kInvalidSocket) {
    CLOSESOCKET(sock);
  }

#ifdef _WIN32
  WSACleanup();
#endif
}

} // namespace

void startIpcServer(int port) {
  if (ipcRunning.load()) {
    return;
  }
  ipcRunning.store(true);
  ipcThread = thread(serverLoop, port);
}

void stopIpcServer() {
  if (!ipcRunning.load()) {
    return;
  }
  ipcRunning.store(false);
  SocketHandle sock = listenSocket.exchange(kInvalidSocket);
  if (sock != kInvalidSocket) {
    // unblocks accept() in serverLoop so it can observe ipcRunning == false
    CLOSESOCKET(sock);
  }
  if (ipcThread.joinable()) {
    ipcThread.join();
  }
}
