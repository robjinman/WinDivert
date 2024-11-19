#include <windows.h>
#include "windivert.h"
#undef max
#include <chrono>
#include <thread>
#include <iostream>
#include <algorithm>
#include <vector>

using std::chrono::high_resolution_clock;
using std::chrono::duration_cast;
using std::chrono::duration;
using std::chrono::milliseconds;

namespace {

void busyWait(long ms)
{
  auto start = high_resolution_clock::now();
  while (duration_cast<milliseconds>(high_resolution_clock::now() - start).count() < ms) {}
}

void sleep(long ms)
{
  std::this_thread::sleep_for(milliseconds(ms));
}

struct Config
{
  HANDLE handle;
  int batch;
  int delay;
};

DWORD passthru(LPVOID arg)
{
  Config* config = reinterpret_cast<Config*>(arg);
  HANDLE handle = config->handle;
  int batch = config->batch;
  int delay = config->delay;

  const int mtu = 1500;
  UINT packetLen = std::max(batch * mtu, WINDIVERT_MTU_MAX);
  std::vector<UINT8> packet(packetLen);
  std::vector<WINDIVERT_ADDRESS> addr(batch);

  while (true) {
    UINT addrLen = batch * sizeof(WINDIVERT_ADDRESS);
    UINT recvLen = 0;
    if (!WinDivertRecvEx(handle, packet.data(), packetLen, &recvLen, 0, addr.data(), &addrLen, NULL)) {
      std::cerr << "warning: failed to read packet (" << GetLastError() << ")\n";
      continue;
    }

    busyWait(delay);
    //sleep(delay);

    if (!WinDivertSendEx(handle, packet.data(), recvLen, NULL, 0, addr.data(), addrLen, NULL)) {
      std::cerr << "warning: failed to reinject packet (" << GetLastError() << ")\n";
    }
  }
}

}

int main(int argc, char** argv)
{
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " filter milliseconds\n";
    return EXIT_FAILURE;
  }

  const int threads = 32;
  std::string filter = argv[1];
  int delay = std::atoi(argv[2]);

  HANDLE handle = WinDivertOpen(filter.c_str(), WINDIVERT_LAYER_NETWORK, 0, 0);
  if (handle == INVALID_HANDLE_VALUE) {
    std::cerr << "error: failed to open the WinDivert device (" << GetLastError() << ")\n";
    return EXIT_FAILURE;
  }

  Config config;
  config.handle = handle;
  config.batch = 1;
  config.delay = delay;

  for (int i = 0; i < threads; ++i) {
    HANDLE thread = CreateThread(NULL, 1, passthru, &config, 0, NULL);
    if (thread == NULL) {
      std::cerr << "error: failed to start passthru thread (" << GetLastError() << ")\n";
      return EXIT_FAILURE;
    }
  }

  passthru(&config);

  return 0;
}

