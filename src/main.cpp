#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <type_traits>
#include <unistd.h>

// Timestamp field: bytes 5..10 of every ITCH panel message (after the 1-byte
// type, 2-byte stock locate, and 2-byte tracking number). Shared by all types,
// so it lives here rather than inside a per-message namespace.
constexpr size_t kTimestampOffset = 5;
constexpr size_t kTimestampSize = 6;

// 'A' Add Order layout. Offsets are relative to the start of the payload
// (the message-type byte). Reading by offset keeps us off a packed struct
// and avoids forming typed pointers into unaligned mapped memory.
namespace add_order {

// Full 'A' layout is documented here; fields not yet parsed are marked
// [[maybe_unused]] so the table stays complete under -Wunused-const-variable.
[[maybe_unused]] constexpr size_t kStockLocate = 1; // uint16, big-endian
[[maybe_unused]] constexpr size_t kOrderRef = 11;   // uint64, big-endian
[[maybe_unused]] constexpr size_t kSide = 19;       // char ('B' or 'S')
constexpr size_t kShares = 20;                      // uint32, big-endian
[[maybe_unused]] constexpr size_t kStock = 24;      // 8 bytes, space-padded
[[maybe_unused]] constexpr size_t kPrice = 32;      // uint32, big-endian
constexpr size_t kSize = 36;                        // total payload bytes
} // namespace add_order

// Big-endian, alignment-safe scalar read. Accepts a pointer to any type and
// views it as bytes via memcpy, so it is correct on unaligned addresses.
template <typename T, typename U>
[[nodiscard]] inline T read_be(const U *p) noexcept {
  static_assert(std::is_unsigned_v<T>, "T must be an unsigned type");
  T value{};
  std::memcpy(&value, reinterpret_cast<const uint8_t *>(p), sizeof(T));
  if constexpr (std::endian::native == std::endian::little) {
    if constexpr (sizeof(T) == 2) {
      value = __builtin_bswap16(value);
    } else if constexpr (sizeof(T) == 4) {
      value = __builtin_bswap32(value);
    } else if constexpr (sizeof(T) == 8) {
      value = __builtin_bswap64(value);
    }
  }
  return value;
}

// ITCH 48-bit big-endian timestamp (6 bytes), zero-extended to uint64_t.
[[nodiscard]] inline uint64_t read_timestamp(const uint8_t *p) noexcept {
  return (static_cast<uint64_t>(p[0]) << 40) |
         (static_cast<uint64_t>(p[1]) << 32) |
         (static_cast<uint64_t>(p[2]) << 24) |
         (static_cast<uint64_t>(p[3]) << 16) |
         (static_cast<uint64_t>(p[4]) << 8) | static_cast<uint64_t>(p[5]);
}

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
              << "First Timestamp: 0 ns\n"
              << "Last Timestamp:  0 ns\n"
              << "Out-of-order ts: 0\n"
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

  uint64_t first_timestamp = 0;
  uint64_t last_timestamp = 0;
  bool have_timestamp = false;
  size_t out_of_order_timestamps = 0;

  const auto start_time = std::chrono::high_resolution_clock::now();

  while (current_start < end) {
    const auto *current = current_start;

    if (static_cast<size_t>(end - current) < sizeof(uint16_t)) {
      std::cerr << "Truncated message length prefix.\n";
      break;
    }

    const uint16_t msg_length = read_be<uint16_t>(current);

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

    // Shared 48-bit timestamp, extracted once for all message types. The guard
    // prevents malformed short messages from reading past their payload.
    if (msg_length >= kTimestampOffset + kTimestampSize) {
      const uint64_t ts = read_timestamp(current + kTimestampOffset);
      if (!have_timestamp) {
        first_timestamp = ts;
        have_timestamp = true;
      } else if (ts < last_timestamp) {
        ++out_of_order_timestamps;
      }
      last_timestamp = ts;
    }

    switch (msg_type) {
    case 'A': {
      if (msg_length < add_order::kSize) {
        std::cerr << "Invalid AddOrderMessage length: " << msg_length << '\n';
        munmap(mapped, static_cast<size_t>(sb.st_size));
        close(fd);
        return 1;
      }

      ++add_orders;
      total_shares_added += read_be<uint32_t>(current + add_order::kShares);
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
            << "First Timestamp: " << first_timestamp << " ns\n"
            << "Last Timestamp:  " << last_timestamp << " ns\n"
            << "Out-of-order ts: " << out_of_order_timestamps << '\n'
            << "Time Elapsed:   " << elapsed_seconds << " seconds\n"
            << "Throughput:     " << throughput << " million msgs/sec\n";

  munmap(mapped, static_cast<size_t>(sb.st_size));
  close(fd);

  return 0;
}
