#include "drivers/serial/serial_transport.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>

#if defined(FMCW_TARGET_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace fmcw {
namespace {

#if defined(FMCW_TARGET_WINDOWS)

std::string windowsError(const char* operation) {
  return std::string(operation) + " failed with Win32 error " + std::to_string(GetLastError());
}

std::string windowsSerialOpenError(const std::string& port, DWORD code) {
  if (code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION) {
    return "Serial port " + port +
        " is already in use or access was denied; close other serial/control software and retry";
  }
  if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
    return "Serial port " + port +
        " is no longer available; refresh the detected port list and select a connected device";
  }
  return "Opening serial port " + port + " failed with Win32 error " + std::to_string(code);
}

std::string normalizedPort(std::string port) {
  if (port.rfind("\\\\.\\", 0) == 0) {
    return port;
  }
  if (port.size() >= 3 && (port[0] == 'C' || port[0] == 'c') &&
      (port[1] == 'O' || port[1] == 'o') && (port[2] == 'M' || port[2] == 'm')) {
    return "\\\\.\\" + port;
  }
  return port;
}

#else

speed_t baudConstant(std::uint32_t baud_rate) {
  switch (baud_rate) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 921600: return B921600;
#ifdef B1000000
    case 1000000: return B1000000;
#endif
#ifdef B2000000
    case 2000000: return B2000000;
#endif
    default: return 0;
  }
}

std::string posixError(const char* operation) {
  return std::string(operation) + " failed: " + std::strerror(errno);
}

std::string posixSerialOpenError(const std::string& port, int code) {
  if (code == EACCES || code == EPERM) {
    return "Serial port " + port +
        " permission denied; add the login user to the dialout group, log in again, and retry";
  }
  if (code == EBUSY) {
    return "Serial port " + port +
        " is already in use; close serial-getty or another application using this UART";
  }
  if (code == ENOENT || code == ENODEV) {
    const bool jetson_uart = port.rfind("/dev/ttyTHS", 0) == 0;
    return "Serial port " + port + " is not available; " +
        (jetson_uart ? "enable the Jetson header UART with Jetson-IO and refresh the port list" :
                       "reconnect the device and refresh the port list");
  }
  return "Opening serial port " + port + " failed: " + std::strerror(code);
}

#endif

}  // namespace

struct PlatformSerialTransport::Impl {
#if defined(FMCW_TARGET_WINDOWS)
  HANDLE handle = INVALID_HANDLE_VALUE;
#else
  int fd = -1;
#endif
};

PlatformSerialTransport::PlatformSerialTransport() : impl_(std::make_unique<Impl>()) {}

PlatformSerialTransport::~PlatformSerialTransport() { close(); }

bool PlatformSerialTransport::open(const SerialSettings& settings, std::string& error) {
  close();
  if (settings.port.empty() || settings.baud_rate == 0 || (settings.stop_bits != 1 && settings.stop_bits != 2)) {
    error = "Serial port, baud rate, and stop bits are invalid";
    return false;
  }

#if defined(FMCW_TARGET_WINDOWS)
  const auto port = normalizedPort(settings.port);
  impl_->handle = CreateFileA(port.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (impl_->handle == INVALID_HANDLE_VALUE) {
    error = windowsSerialOpenError(settings.port, GetLastError());
    return false;
  }

  DCB dcb{};
  dcb.DCBlength = sizeof(dcb);
  if (!GetCommState(impl_->handle, &dcb)) {
    error = windowsError("GetCommState");
    close();
    return false;
  }
  dcb.BaudRate = settings.baud_rate;
  dcb.ByteSize = 8;
  dcb.fBinary = TRUE;
  dcb.fParity = settings.parity == SerialParity::None ? FALSE : TRUE;
  dcb.Parity = settings.parity == SerialParity::Even ? EVENPARITY :
               settings.parity == SerialParity::Odd ? ODDPARITY : NOPARITY;
  dcb.StopBits = settings.stop_bits == 2 ? TWOSTOPBITS : ONESTOPBIT;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDtrControl = DTR_CONTROL_DISABLE;
  dcb.fRtsControl = RTS_CONTROL_DISABLE;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;
  if (!SetCommState(impl_->handle, &dcb)) {
    error = windowsError("SetCommState");
    close();
    return false;
  }

  COMMTIMEOUTS timeouts{};
  timeouts.ReadIntervalTimeout = 10;
  timeouts.ReadTotalTimeoutConstant = 20;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 1000;
  if (!SetCommTimeouts(impl_->handle, &timeouts)) {
    error = windowsError("SetCommTimeouts");
    close();
    return false;
  }
#else
  const auto speed = baudConstant(settings.baud_rate);
  if (speed == 0) {
    error = "Unsupported Linux serial baud rate: " + std::to_string(settings.baud_rate);
    return false;
  }
  impl_->fd = ::open(settings.port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (impl_->fd < 0) {
    error = posixSerialOpenError(settings.port, errno);
    return false;
  }
  termios tty{};
  if (tcgetattr(impl_->fd, &tty) != 0) {
    error = posixError("tcgetattr");
    close();
    return false;
  }
  cfmakeraw(&tty);
  cfsetispeed(&tty, speed);
  cfsetospeed(&tty, speed);
  tty.c_cflag |= CLOCAL | CREAD;
  tty.c_cflag &= ~CSIZE;
  tty.c_cflag |= CS8;
  tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
  if (settings.parity != SerialParity::None) {
    tty.c_cflag |= PARENB;
    if (settings.parity == SerialParity::Odd) {
      tty.c_cflag |= PARODD;
    }
  }
  if (settings.stop_bits == 2) {
    tty.c_cflag |= CSTOPB;
  }
  if (tcsetattr(impl_->fd, TCSANOW, &tty) != 0) {
    error = posixError("tcsetattr");
    close();
    return false;
  }
#endif
  error.clear();
  return true;
}

void PlatformSerialTransport::close() {
#if defined(FMCW_TARGET_WINDOWS)
  if (impl_->handle != INVALID_HANDLE_VALUE) {
    CloseHandle(impl_->handle);
    impl_->handle = INVALID_HANDLE_VALUE;
  }
#else
  if (impl_->fd >= 0) {
    ::close(impl_->fd);
    impl_->fd = -1;
  }
#endif
}

bool PlatformSerialTransport::isOpen() const {
#if defined(FMCW_TARGET_WINDOWS)
  return impl_->handle != INVALID_HANDLE_VALUE;
#else
  return impl_->fd >= 0;
#endif
}

bool PlatformSerialTransport::purge(std::string& error) {
  if (!isOpen()) {
    error = "Serial port is not open";
    return false;
  }
#if defined(FMCW_TARGET_WINDOWS)
  if (!PurgeComm(impl_->handle, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR)) {
    error = windowsError("PurgeComm");
    return false;
  }
#else
  if (tcflush(impl_->fd, TCIOFLUSH) != 0) {
    error = posixError("tcflush");
    return false;
  }
#endif
  error.clear();
  return true;
}

bool PlatformSerialTransport::write(const std::vector<std::uint8_t>& data, std::string& error) {
  if (!isOpen()) {
    error = "Serial port is not open";
    return false;
  }
  std::size_t offset = 0;
  while (offset < data.size()) {
#if defined(FMCW_TARGET_WINDOWS)
    DWORD written = 0;
    const auto remaining = static_cast<DWORD>(std::min<std::size_t>(data.size() - offset, MAXDWORD));
    if (!WriteFile(impl_->handle, data.data() + offset, remaining, &written, nullptr)) {
      error = windowsError("WriteFile(serial)");
      return false;
    }
    offset += written;
#else
    const auto written = ::write(impl_->fd, data.data() + offset, data.size() - offset);
    if (written > 0) {
      offset += static_cast<std::size_t>(written);
    } else if (written < 0 && errno != EINTR && errno != EAGAIN) {
      error = posixError("write(serial)");
      return false;
    } else {
      pollfd descriptor{impl_->fd, POLLOUT, 0};
      if (::poll(&descriptor, 1, 1000) <= 0) {
        error = "Serial write timeout";
        return false;
      }
    }
#endif
  }
#if !defined(FMCW_TARGET_WINDOWS)
  if (tcdrain(impl_->fd) != 0) {
    error = posixError("tcdrain");
    return false;
  }
#endif
  error.clear();
  return true;
}

bool PlatformSerialTransport::readExact(std::size_t byte_count, std::vector<std::uint8_t>& data,
                                        std::chrono::milliseconds timeout, std::string& error) {
  data.clear();
  if (!isOpen()) {
    error = "Serial port is not open";
    return false;
  }
  data.reserve(byte_count);
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (data.size() < byte_count) {
    if (std::chrono::steady_clock::now() >= deadline) {
      error = "Serial read timeout";
      return false;
    }
    std::uint8_t buffer[256]{};
    const auto wanted = std::min<std::size_t>(sizeof(buffer), byte_count - data.size());
#if defined(FMCW_TARGET_WINDOWS)
    DWORD received = 0;
    if (!ReadFile(impl_->handle, buffer, static_cast<DWORD>(wanted), &received, nullptr)) {
      error = windowsError("ReadFile(serial)");
      return false;
    }
    data.insert(data.end(), buffer, buffer + received);
#else
    pollfd descriptor{impl_->fd, POLLIN, 0};
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    const int wait_ms = static_cast<int>(std::clamp<std::int64_t>(remaining, 1, 1000));
    const int ready = ::poll(&descriptor, 1, wait_ms);
    if (ready < 0 && errno != EINTR) {
      error = posixError("poll(serial)");
      return false;
    }
    if (ready > 0) {
      const auto received = ::read(impl_->fd, buffer, wanted);
      if (received > 0) {
        data.insert(data.end(), buffer, buffer + received);
      } else if (received < 0 && errno != EINTR && errno != EAGAIN) {
        error = posixError("read(serial)");
        return false;
      }
    }
#endif
    if (data.size() < byte_count) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  error.clear();
  return true;
}

bool PlatformSerialTransport::readLine(std::string& line, std::chrono::milliseconds timeout, std::string& error) {
  line.clear();
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    std::vector<std::uint8_t> byte;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (!readExact(1, byte, std::max(remaining, std::chrono::milliseconds(1)), error)) {
      return false;
    }
    const char ch = static_cast<char>(byte.front());
    if (ch == '\n') {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      error.clear();
      return true;
    }
    if (ch != '\r') {
      if (line.size() >= 255U) {
        error = "Serial response line exceeds 255 bytes";
        return false;
      }
      line.push_back(ch);
    }
  }
  error = "Serial line read timeout";
  return false;
}

std::vector<std::uint8_t> bytesFromString(const std::string& text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::vector<std::string> availableSerialPorts() {
  std::vector<std::string> ports;
#if defined(FMCW_TARGET_WINDOWS)
  char target[4096]{};
  for (int index = 1; index <= 256; ++index) {
    const auto name = "COM" + std::to_string(index);
    if (QueryDosDeviceA(name.c_str(), target, static_cast<DWORD>(sizeof(target))) != 0U) {
      ports.push_back(name);
    }
  }
#else
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator("/dev", error)) {
    if (error) {
      break;
    }
    const auto name = entry.path().filename().string();
    const bool supported = name.rfind("ttyUSB", 0) == 0 || name.rfind("ttyACM", 0) == 0 ||
        name.rfind("ttyTHS", 0) == 0 || name.rfind("ttyAMA", 0) == 0 ||
        name.rfind("rfcomm", 0) == 0;
    if (supported) {
      ports.push_back(entry.path().string());
    }
  }
#endif
  std::sort(ports.begin(), ports.end());
  ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
  return ports;
}

}  // namespace fmcw
