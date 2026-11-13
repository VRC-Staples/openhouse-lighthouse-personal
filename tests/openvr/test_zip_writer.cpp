#include <gtest/gtest.h>
#include <algorithm>
#include <sstream>
#include "report/zip_writer.hpp"

namespace {
namespace fs = std::filesystem;
using lh::report::ZipWriter;

TEST(ReportTransactions, FailedInputCannotPublishEvenWhenCallerFinalizes) {
  lh::files::StagingDirectory temp(fs::temp_directory_path());
  ZipWriter writer;
  ASSERT_TRUE(writer.Open(temp.Payload()));
  ASSERT_TRUE(writer.AddText("info.txt", "metadata written before input disappears"));
  EXPECT_FALSE(writer.AddFile("missing", temp.Payload().parent_path() / "missing"));
  EXPECT_FALSE(writer.AddText("later", "must not resume"));
  EXPECT_FALSE(writer.Close());
  EXPECT_FALSE(fs::exists(temp.Payload()));
}

struct FailedRead : std::streambuf {
  int reads = 0;
  std::streamsize xsgetn(char* out, std::streamsize n) override {
    if (reads++ != 0) throw std::ios_base::failure("injected read failure");
    std::fill_n(out, size_t(n), 'x');
    return n;
  }
};

TEST(ReportTransactions, MidstreamReadFailureAbortsPublication) {
  lh::files::StagingDirectory temp(fs::temp_directory_path());
  FailedRead buffer;
  std::istream input(&buffer);
  ZipWriter writer;
  ASSERT_TRUE(writer.Open(temp.Payload()));
  EXPECT_FALSE(writer.AddStream("recording", input));
  EXPECT_EQ(buffer.reads, 2);
  EXPECT_FALSE(writer.Close());
  EXPECT_FALSE(fs::exists(temp.Payload()));
}

struct EndlessRead : std::streambuf {
  size_t consumed = 0;
  std::streamsize xsgetn(char* out, std::streamsize n) override {
    consumed += size_t(n);
    std::fill_n(out, size_t(n), 'x');
    return n;
  }
};

TEST(ReportLimits, StopsAnOversizedInputWithoutReadingToTheEnd) {
  lh::files::StagingDirectory temp(fs::temp_directory_path());
  EndlessRead buffer;
  std::istream input(&buffer);
  ZipWriter writer(1024);
  ASSERT_TRUE(writer.Open(temp.Payload()));
  EXPECT_FALSE(writer.AddStream("recording", input));
  EXPECT_LE(buffer.consumed, 1025u);
  EXPECT_FALSE(writer.Close());
  EXPECT_FALSE(fs::exists(temp.Payload()));
}

TEST(ReportLimits, IncludesCentralDirectoryInOutputBudget) {
  lh::files::StagingDirectory temp(fs::temp_directory_path());
  ZipWriter writer(80);
  ASSERT_TRUE(writer.Open(temp.Payload()));
  ASSERT_TRUE(writer.AddText("info", "hello"));
  EXPECT_FALSE(writer.Close());
  EXPECT_FALSE(fs::exists(temp.Payload()));
}

TEST(ReportLimits, RejectsLongNamesAndEntryCountOverflow) {
  lh::files::StagingDirectory temp(fs::temp_directory_path());
  {
    ZipWriter writer;
    ASSERT_TRUE(writer.Open(temp.Payload()));
    EXPECT_FALSE(writer.AddText(std::string(65536, 'x'), ""));
    EXPECT_FALSE(writer.Close());
  }
  ZipWriter writer;
  ASSERT_TRUE(writer.Open(temp.Payload()));
  for (size_t i = 0; i < 65535; ++i) ASSERT_TRUE(writer.AddText("x", "")) << i;
  EXPECT_FALSE(writer.AddText("overflow", ""));
  EXPECT_FALSE(writer.Close());
  EXPECT_FALSE(fs::exists(temp.Payload()));
}

TEST(ReportTransactions, SuccessfulReportContainsTheCompleteCompressedInput) {
  lh::files::StagingDirectory temp(fs::temp_directory_path());
  const std::string content(150000, 'z');
  ZipWriter writer;
  ASSERT_TRUE(writer.Open(temp.Payload()));
  ASSERT_TRUE(writer.AddText("recording", content));
  ASSERT_TRUE(writer.Close());
  const auto bytes = lh::files::Read(temp.Payload());
  const size_t start = 30 + std::string("recording").size();
  std::string decoded(content.size(), '\0');
  z_stream stream{};
  ASSERT_EQ(inflateInit2(&stream, -15), Z_OK);
  stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data() + start));
  stream.avail_in = uInt(bytes.size() - start);
  stream.next_out = reinterpret_cast<Bytef*>(decoded.data());
  stream.avail_out = uInt(decoded.size());
  const int result = inflate(&stream, Z_FINISH);
  inflateEnd(&stream);
  EXPECT_EQ(result, Z_STREAM_END);
  EXPECT_EQ(decoded, content);
}
}  // namespace
