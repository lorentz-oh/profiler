#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <sys/time.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

using std::chrono::steady_clock;
using std::chrono::time_point;

const int UTIME_I = 13;
const int STIME_I = 14;
const int RSS = 23;

pid_t execute(char *path) {
  pid_t pid = fork();
  if (pid == 0) {
    execl(path, path, nullptr);
  }
  return pid;
}

std::string get_word(std::string &str, int n) {
  std::stringstream stream(str);
  std::string word;
  for (int i = 0; i < n; i++) {
    std::getline(stream, word, ' ');
  }
  std::getline(stream, word, ' ');
  return word;
}

struct Stat {
  float percent_cpu = 0;
  long bytes_mem = 0;
  int open_fds = 0;
};

class system_config {
  static system_config conf;

public:
  int concurrency = 0;
  int tps = 0;
  int page_size = 0;

  static void init() {
    conf.concurrency = std::thread::hardware_concurrency();
    conf.tps = sysconf(_SC_CLK_TCK);
    conf.page_size = sysconf(_SC_PAGE_SIZE);
  }

  static system_config get_conf() { return conf; }
};
system_config system_config::conf;

class tracked_process {
  std::string prev_stat;
  std::ifstream stat_file;
  time_point<steady_clock> last_read_time;
  int pid;

public:
  tracked_process(pid_t pid) {
    stat_file = std::ifstream("/proc/" + std::to_string(pid) + "/stat");
    last_read_time = steady_clock::now();
    std::getline(stat_file, prev_stat);
    this->pid = pid;
  }

  Stat get_stat() {
    auto now = steady_clock::now();
    std::string stat;
    float t_sec = std::chrono::duration<float>(now - last_read_time).count();
    stat_file = std::ifstream("/proc/" + std::to_string(pid) + "/stat");
    std::getline(stat_file, stat);
    Stat ret = calculate_stat(prev_stat, stat, t_sec);
    prev_stat = stat;
    last_read_time = now;
    return ret;
  }

  Stat calculate_stat(std::string pre, std::string post, float time_sec) {
    Stat stat;
    try {
      system_config conf = system_config::get_conf();
      float pre_time =
          std::stof(get_word(pre, UTIME_I)) + std::stof(get_word(pre, STIME_I));
      float post_time = std::stof(get_word(post, UTIME_I)) +
                        std::stof(get_word(post, STIME_I));
      float percent_cpu =
          (post_time - pre_time) / (time_sec * conf.tps) * 100.0f;
      long bytes = std::stol(get_word(post, RSS)) * conf.page_size;

      for (auto const &dir_entry : std::filesystem::directory_iterator(
               "/proc/" + std::to_string(pid) + "/fd")) {
        stat.open_fds++;
      }
      stat.percent_cpu = percent_cpu;
      stat.bytes_mem = bytes;
      return stat;
    } catch (...) {
      std::cerr << "Exception in calculate_stat()" << std::endl;
    }
    return stat;
  }
};

int main(int argc, char **argv) {
  if (argc < 3) {
    std::cout << "usage: " << argv[0]
              << " [path_to_executable] [time_interval_in_seconds]"
              << std::endl;
    return 0;
  }
  system_config::init();
  std::chrono::seconds delta;
  try {
    delta = std::chrono::seconds(std::stoi(argv[2]));
  } catch (...) {
    std::cerr << "Incorrect time" << std::endl;
    return 1;
  }

  pid_t pid = execute(argv[1]);

  if (pid == -1) {
    std::cerr << "Failed to execute the specified file" << std::endl;
    return 1;
  }
  std::ofstream log;
  log.open("log.txt", std::ios::app);
  tracked_process proc(pid);

  auto sleep_till = steady_clock::now();

  Stat stat;
  while (true) {
    using std::chrono::system_clock;

    sleep_till += delta;
    std::this_thread::sleep_until(sleep_till);

    stat = proc.get_stat();

    std::time_t time = system_clock::to_time_t(system_clock::now());
    log << '[' << std::put_time(std::localtime(&time), "%c") << ']'
        << " cpu (%): " << stat.percent_cpu
        << ", memory (bytes): " << stat.bytes_mem << ", fds: " << stat.open_fds
        << std::endl;
  }
}
