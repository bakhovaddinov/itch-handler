#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#pragma pack(push, 1)
struct AddOrderMessage {
  char message_type;
  uint16_t stock_locate;
  uint16_t tracking_number;
  uint8_t timestamp[6];
  uint64_t order_ref_number;
  char buy_sell_indicator;
  uint32_t shares;
  char stock[8];
  uint32_t price;
};
#pragma pack(pop)

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <file>\n";
    return 1;
  }

  const int fd = open(argv[1], O_RDONLY);
  if (fd < 0) {
    std::cerr << "Failed to open file.\n";
    return 1;
  }

  struct stat sb{};
  if (fstat(fd, &sb) < 0) {
    std::cerr << "Failed to get file size.\n";
    close(fd);
    return 1;
  }

  if (sb.st_size == 0) {
    std::cout << "--- Parsing Complete ---\n"
              << "Total Messages: 0\n"
              << "Add Orders (A): 0\n"
              << "Total Shares:   0\n"
              << "Time Elapsed:   0 seconds\n"
              << "Throughput:     0 million msgs/sec\n";

    close(fd);
    return 0;
  }

  void *mapped = mmap(nullptr, static_cast<size_t>(sb.st_size), PROT_READ,
                      MAP_PRIVATE, fd, 0);

  if (mapped == MAP_FAILED) {
    std::cerr << "Memory mapping failed.\n";
    close(fd);
    return 1;
  }

  const auto *file_data = static_cast<const uint8_t *>(mapped);
  const auto *current_start = file_data;
  const auto *end = file_data + sb.st_size;

  size_t total_messages = 0;
  size_t add_orders = 0;
  uint64_t total_shares_added = 0;

  const auto start_time = std::chrono::high_resolution_clock::now();

  while (current_start < end) {
    const auto *current = current_start;

    if (static_cast<size_t>(end - current) < sizeof(uint16_t)) {
      std::cerr << "Truncated message length prefix.\n";
      break;
    }

    const uint16_t msg_length =
        __builtin_bswap16(*reinterpret_cast<const uint16_t *>(current));

    current += sizeof(uint16_t);

    if (static_cast<size_t>(end - current) < msg_length) {
      std::cerr << "Truncated message payload.\n";
      break;
    }

    if (msg_length == 0) {
      std::cerr << "Invalid zero-length message.\n";
      break;
    }

    const char msg_type = static_cast<char>(current[0]);

    switch (msg_type) {
    case 'A': {
      if (msg_length < sizeof(AddOrderMessage)) {
        std::cerr << "Invalid AddOrderMessage length: " << msg_length << '\n';
        munmap(mapped, static_cast<size_t>(sb.st_size));
        close(fd);
        return 1;
      }

      const auto *msg = reinterpret_cast<const AddOrderMessage *>(current);

      ++add_orders;
      total_shares_added += __builtin_bswap32(msg->shares);
      break;
    }

    case 'E': {
      break;
    }

    default: {
      break;
    }
    }

    ++total_messages;
    current_start = current + msg_length;
  }

  const auto end_time = std::chrono::high_resolution_clock::now();

  const std::chrono::duration<double> elapsed = end_time - start_time;

  const double elapsed_seconds = elapsed.count();
  const double throughput =
      elapsed_seconds > 0.0
          ? static_cast<double>(total_messages) / elapsed_seconds / 1'000'000.0
          : 0.0;

  std::cout << "--- Parsing Complete ---\n"
            << "Total Messages: " << total_messages << '\n'
            << "Add Orders (A): " << add_orders << '\n'
            << "Total Shares:   " << total_shares_added << '\n'
            << "Time Elapsed:   " << elapsed_seconds << " seconds\n"
            << "Throughput:     " << throughput << " million msgs/sec\n";

  munmap(mapped, static_cast<size_t>(sb.st_size));
  close(fd);

  return 0;
}
