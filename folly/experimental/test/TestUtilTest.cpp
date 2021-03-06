/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/experimental/TestUtil.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include <system_error>

#include <boost/algorithm/string.hpp>
#include <folly/Memory.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

using namespace folly;
using namespace folly::test;

TEST(TemporaryFile, Simple) {
  int fd = -1;
  char c = 'x';
  {
    TemporaryFile f;
    EXPECT_FALSE(f.path().empty());
    EXPECT_TRUE(f.path().is_absolute());
    fd = f.fd();
    EXPECT_LE(0, fd);
    ssize_t r = write(fd, &c, 1);
    EXPECT_EQ(1, r);
  }

  // The file must have been closed.  This assumes that no other thread
  // has opened another file in the meanwhile, which is a sane assumption
  // to make in this test.
  ssize_t r = write(fd, &c, 1);
  int savedErrno = errno;
  EXPECT_EQ(-1, r);
  EXPECT_EQ(EBADF, savedErrno);
}

TEST(TemporaryFile, Prefix) {
  TemporaryFile f("Foo");
  EXPECT_TRUE(f.path().is_absolute());
  EXPECT_TRUE(boost::algorithm::starts_with(f.path().filename().native(),
                                            "Foo"));
}

TEST(TemporaryFile, PathPrefix) {
  TemporaryFile f("Foo", ".");
  EXPECT_EQ(fs::path("."), f.path().parent_path());
  EXPECT_TRUE(boost::algorithm::starts_with(f.path().filename().native(),
                                            "Foo"));
}

TEST(TemporaryFile, NoSuchPath) {
  EXPECT_THROW({TemporaryFile f("", "/no/such/path");},
               std::system_error);
}

void testTemporaryDirectory(TemporaryDirectory::Scope scope) {
  fs::path path;
  {
    TemporaryDirectory d("", "", scope);
    path = d.path();
    EXPECT_FALSE(path.empty());
    EXPECT_TRUE(path.is_absolute());
    EXPECT_TRUE(fs::exists(path));
    EXPECT_TRUE(fs::is_directory(path));

    fs::path fp = path / "bar";
    int fd = open(fp.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
    EXPECT_NE(fd, -1);
    close(fd);

    TemporaryFile f("Foo", d.path());
    EXPECT_EQ(d.path(), f.path().parent_path());
  }
  bool exists = (scope == TemporaryDirectory::Scope::PERMANENT);
  EXPECT_EQ(exists, fs::exists(path));
}

TEST(TemporaryDirectory, Permanent) {
  testTemporaryDirectory(TemporaryDirectory::Scope::PERMANENT);
}

TEST(TemporaryDirectory, DeleteOnDestruction) {
  testTemporaryDirectory(TemporaryDirectory::Scope::DELETE_ON_DESTRUCTION);
}

TEST(ChangeToTempDir, ChangeDir) {
  auto pwd1 = fs::current_path();
  {
    ChangeToTempDir d;
    EXPECT_NE(pwd1, fs::current_path());
  }
  EXPECT_EQ(pwd1, fs::current_path());
}

TEST(PCREPatternMatch, Simple) {
  EXPECT_PCRE_MATCH(".*a.c.*", "gabca");
  EXPECT_NO_PCRE_MATCH("a.c", "gabca");
  EXPECT_NO_PCRE_MATCH(".*ac.*", "gabca");
}

TEST(CaptureFD, GlogPatterns) {
  CaptureFD stderr(2);
  LOG(INFO) << "All is well";
  EXPECT_NO_PCRE_MATCH(glogErrOrWarnPattern(), stderr.readIncremental());
  {
    LOG(ERROR) << "Uh-oh";
    auto s = stderr.readIncremental();
    EXPECT_PCRE_MATCH(glogErrorPattern(), s);
    EXPECT_NO_PCRE_MATCH(glogWarningPattern(), s);
    EXPECT_PCRE_MATCH(glogErrOrWarnPattern(), s);
  }
  {
    LOG(WARNING) << "Oops";
    auto s = stderr.readIncremental();
    EXPECT_NO_PCRE_MATCH(glogErrorPattern(), s);
    EXPECT_PCRE_MATCH(glogWarningPattern(), s);
    EXPECT_PCRE_MATCH(glogErrOrWarnPattern(), s);
  }
}

TEST(CaptureFD, ChunkCob) {
  std::vector<std::string> chunks;
  {
    CaptureFD stderr(2, [&](StringPiece p) {
      chunks.emplace_back(p.str());
      switch (chunks.size()) {
        case 1:
          EXPECT_PCRE_MATCH(".*foo.*bar.*", p);
          break;
        case 2:
          EXPECT_PCRE_MATCH("[^\n]*baz.*", p);
          break;
        default:
          FAIL() << "Got too many chunks: " << chunks.size();
      }
    });
    LOG(INFO) << "foo";
    LOG(INFO) << "bar";
    EXPECT_PCRE_MATCH(".*foo.*bar.*", stderr.read());
    auto chunk = stderr.readIncremental();
    EXPECT_EQ(chunks.at(0), chunk);
    LOG(INFO) << "baz";
    EXPECT_PCRE_MATCH(".*foo.*bar.*baz.*", stderr.read());
  }
  EXPECT_EQ(2, chunks.size());
}


class EnvVarSaverTest : public testing::Test {};

TEST_F(EnvVarSaverTest, ExampleNew) {
  auto key = "hahahahaha";
  EXPECT_EQ(nullptr, getenv(key));

  auto saver = make_unique<EnvVarSaver>();
  PCHECK(0 == setenv(key, "blah", true));
  EXPECT_EQ("blah", std::string{getenv(key)});
  saver = nullptr;
  EXPECT_EQ(nullptr, getenv(key));
}

TEST_F(EnvVarSaverTest, ExampleExisting) {
  auto key = "USER";
  EXPECT_NE(nullptr, getenv(key));
  auto value = std::string{getenv(key)};

  auto saver = make_unique<EnvVarSaver>();
  PCHECK(0 == setenv(key, "blah", true));
  EXPECT_EQ("blah", std::string{getenv(key)});
  saver = nullptr;
  EXPECT_TRUE(value == getenv(key));
}

TEST_F(EnvVarSaverTest, ExampleDeleting) {
  auto key = "USER";
  EXPECT_NE(nullptr, getenv(key));
  auto value = std::string{getenv(key)};

  auto saver = make_unique<EnvVarSaver>();
  PCHECK(0 == unsetenv(key));
  EXPECT_EQ(nullptr, getenv(key));
  saver = nullptr;
  EXPECT_TRUE(value == getenv(key));
}

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
