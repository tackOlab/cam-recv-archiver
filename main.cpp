#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/json.hpp>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mongocxx/client.hpp>
#include <mongocxx/exception/exception.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#include <vector>

namespace fs = std::filesystem;

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_document;

#include "simple_tcp.hpp"

// JPH header for YCbCr colorspace (1920x1080, 4:2:0, sYCC).
std::vector<uint8_t> jph_header = {
    0x00, 0x00, 0x00, 0x0C, 0x6A, 0x50, 0x20, 0x20, 0x0D, 0x0A, 0x87,
    0x0A, 0x00, 0x00, 0x00, 0x14, 0x66, 0x74, 0x79, 0x70, 0x6A, 0x70,
    0x68, 0x20, 0x00, 0x00, 0x00, 0x00, 0x6A, 0x70, 0x68, 0x20, 0x00,
    0x00, 0x00, 0x2D, 0x6A, 0x70, 0x32, 0x68, 0x00, 0x00, 0x00, 0x16,
    0x69, 0x68, 0x64, 0x72, 0x00, 0x00, 0x04, 0x38, 0x00, 0x00, 0x07,
    0x80, 0x00, 0x03, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F,
    0x63, 0x6F, 0x6C, 0x72, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12,
    0x00, 0x08, 0x9C, 0x2C, 0x6A, 0x70, 0x32, 0x63};

void create_filename_based_on_time(char *fname, char *iso_date) {
  char tbuf[32], tmbuf[64];
  auto now = std::chrono::high_resolution_clock::now();
  std::time_t now_c = std::chrono::system_clock::to_time_t(now);
  std::tm now_tm = *std::localtime(&now_c);
  strftime(tbuf, 32, "%Y-%m-%d-%H-%M-%S", &now_tm);
  strftime(iso_date, 32, "%FT%TZ", gmtime(&now_c));
  struct timespec ts;
  std::timespec_get(&ts, TIME_UTC);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::nanoseconds{ts.tv_nsec});
  const int msec = ms.count();
  snprintf(tmbuf, 64, "%s.%03d", tbuf, msec);
  snprintf(fname, 256, "%s.jph", tmbuf);
}

int main(int argc, char **argv) {
  fs::path archive_dir = (argc > 1) ? fs::path(argv[1]) : fs::path(".");
  std::error_code ec;
  fs::create_directories(archive_dir, ec);
  if (ec) {
    fprintf(stderr, "Cannot create archive dir '%s': %s\n",
            archive_dir.c_str(), ec.message().c_str());
    return EXIT_FAILURE;
  }
  archive_dir = fs::absolute(archive_dir);
  printf("Archive dir: %s\n", archive_dir.c_str());

  std::vector<uint8_t> data;
  data.reserve(5000000);
  char fname[256];
  char iso_date[32];
  printf("Waiting for client...\n");
  try {
    mongocxx::instance instance;
    mongocxx::uri uri("mongodb://localhost:27017/");
    mongocxx::client client(uri);
    auto database = client["test"];
    auto collection = database["item1"];

    simple_tcp tcp0("0.0.0.0", 4001);
    if (tcp0.bind_listen() != 0) {
      perror("bind/listen");
      return EXIT_FAILURE;
    }

    while (true) {
      printf("Awaiting connection on :4001 ...\n");
      if (tcp0.accept_one() != 0) {
        perror("accept");
        continue;
      }
      printf("Client connected.\n");

      while (true) {
        uint32_t rsize = 0;
        if (!tcp0.RecvFramed(data, rsize)) {
          printf("Client disconnected or framing error; awaiting next connection.\n");
          tcp0.disconnect_client();
          break;
        }

        create_filename_based_on_time(fname, iso_date);
        auto result = collection.insert_one(make_document(
            kvp("fname", fname),
            kvp("timestamp", bsoncxx::types::b_date(
                                 std::chrono::system_clock::now()))));
        printf("%s = %u bytes\n", fname, rsize);

        fs::path fpath = archive_dir / fname;
        FILE *fp = fopen(fpath.c_str(), "wb");
        if (!fp) {
          perror("fopen");
          continue;
        }
        auto jph_buf =
            std::make_unique<uint8_t[]>(rsize + jph_header.size());
        uint8_t *buf = jph_buf.get();
        memcpy(buf, jph_header.data(), jph_header.size());
        memcpy(buf + jph_header.size(), data.data(), rsize);
        // jp2c box length = codestream bytes + 8 (its own header). Field at offset 77.
        uint32_t jp2c_len = htonl(static_cast<uint32_t>(rsize) + 8u);
        memcpy(buf + 77, &jp2c_len, 4);
        fwrite(buf, sizeof(uint8_t), rsize + jph_header.size(), fp);
        fclose(fp);
      }
    }
  } catch (const mongocxx::exception &e) {
    std::cout << "An exception occurred: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return 0;
}
