// © 2020 Erik Rigtorp <erik@rigtorp.se>
// SPDX-License-Identifier: MIT

// Measure inter-core one-way data latency
//
// Build:
// g++ -O3 -DNDEBUG c2clat.cpp -o c2clat -pthread
//
// Plot results using gnuplot:
// $ c2clat -p | gnuplot -p

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <thread>
#include <vector>

void pinThread(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  if (sched_setaffinity(0, sizeof(set), &set) == -1) {
    perror("sched_setaffinity");
    exit(1);
  }
}

int main(int argc, char *argv[]) {

  int nsamples = 1000;
  int begin_core = 0;
  int end_core = CPU_SETSIZE;
  bool plot = false;

  int opt;
  while ((opt = getopt(argc, argv, "b:e:ps:")) != -1) {
    switch (opt) {
    case 'p':
      plot = true;
      break;
    case 's':
      nsamples = std::stoi(optarg);
      break;
    case 'b':
      begin_core = std::max(std::stoi(optarg), 0);
      break;
    case 'e':
      end_core = std::min(std::stoi(optarg), CPU_SETSIZE);
      break;
    default:
      goto usage;
    }
  }

  if (optind != argc) {
  usage:
    std::cerr << "c2clat 1.0.1 © 2020 Erik Rigtorp <erik@rigtorp.se>\n"
                 "usage: c2clat [-p] [-s number_of_samples] [-b begin_core] [-e end_core]\n"
                 "\nPlot results using gnuplot:\n"
                 "c2clat -p | gnuplot -p\n";
    exit(1);
  }

  cpu_set_t set;
  CPU_ZERO(&set);
  if (sched_getaffinity(0, sizeof(set), &set) == -1) {
    perror("sched_getaffinity");
    exit(1);
  }

  // enumerate available CPUs
  std::vector<int> cpus;
  for (int i = begin_core; i <= end_core; ++i) {
    if (CPU_ISSET(i, &set)) {
      cpus.push_back(i);
    }
  }

  std::map<std::pair<int, int>, std::chrono::nanoseconds> data;

  for (size_t i = 0; i < cpus.size(); ++i) {
    for (size_t j = i + 1; j < cpus.size(); ++j) {

      alignas(64) std::atomic<int> seq1 = {-1};
      alignas(64) std::atomic<int> seq2 = {-1};

      auto t = std::thread([&] {
        pinThread(cpus[i]);
        for (int m = 0; m < nsamples; ++m) {
          for (int n = 0; n < 100; ++n) {
            while (seq1.load(std::memory_order_acquire) != n)
              ;
            seq2.store(n, std::memory_order_release);
          }
        }
      });

      std::chrono::nanoseconds rtt = std::chrono::nanoseconds::max();

      pinThread(cpus[j]);
      for (int m = 0; m < nsamples; ++m) {
        seq1 = seq2 = -1;
        auto ts1 = std::chrono::steady_clock::now();
        for (int n = 0; n < 100; ++n) {
          seq1.store(n, std::memory_order_release);
          while (seq2.load(std::memory_order_acquire) != n)
            ;
        }
        auto ts2 = std::chrono::steady_clock::now();
        rtt = std::min(rtt, ts2 - ts1);
      }

      t.join();

      data[{i, j}] = rtt / 2 / 100;
      data[{j, i}] = rtt / 2 / 100;
    }
  }

  if (plot) {
    std::cout
        << "set title \"Inter-core one-way data latency between CPU cores\"\n"
        << "set xlabel \"CPU\"\n"
        << "set ylabel \"CPU\"\n"
        << "set cblabel \"Latency (ns)\"\n"
        << "$data << EOD\n";
  }

  std::cout << std::setw(4) << "CPU";
  for (size_t i = 0; i < cpus.size(); ++i) {
    std::cout << " " << std::setw(4) << cpus[i];
  }
  std::cout << std::endl;
  for (size_t i = 0; i < cpus.size(); ++i) {
    std::cout << std::setw(4) << cpus[i];
    for (size_t j = 0; j < cpus.size(); ++j) {
      std::cout << " " << std::setw(4) << data[{i, j}].count();
    }
    std::cout << std::endl;
  }

  if (plot) {
    std::cout << "EOD\n"
              << "plot '$data' matrix rowheaders columnheaders using 2:1:3 "
                 "with image\n";
  }

  return 0;
}
