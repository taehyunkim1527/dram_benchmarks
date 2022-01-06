#include <iostream>
#include <utility>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <thread>
#include <vector>
#include <cassert>
#include <numa.h>
#include <time.h>
#include <chrono>

#define TESTSEC = 100;

enum AccessMode{
    Random, Sequential
};
enum TestMode{
    S_CPU, D_CPU, R_SINGLE, S_SINGLE
};

int memaccess_runner(AccessMode mode, int* memory, size_t chunk_size, size_t mem_len, int id) 
{
    sleep(1);
    int tmp;
    int idx = 0;
    int randidx = 0;
    while (true) {
        std::chrono::steady_clock::time_point t_begin = std::chrono::steady_clock::now();
        idx = 0;
        if (mode == Random) {
            while (idx * chunk_size + chunk_size < mem_len) {
                randidx = (idx * 10000) % mem_len;
                // randidx = (rand() % (mem_len / chunk_size)) * chunk_size;
                for (int i = 0; i < chunk_size; i++) {
                    *((int*)memory + (randidx * chunk_size) + i) + 1;
                }
                idx++;
            }
        } else if (mode == Sequential) {
            while (idx * chunk_size + chunk_size < mem_len) {
                for (int i = 0; i < chunk_size; i++) {
                    *((int*)memory + idx * chunk_size + i) + 1;
                }
                idx++;
            }
        }
        std::chrono::steady_clock::time_point t_end = std::chrono::steady_clock::now();
        std::cerr << "[" << id << "]";
        if (mode == Random)
            std::cerr << " [Random] ";
        else if (mode == Sequential)
            std::cerr << " [Sequential] ";
        int dur = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_begin).count();
        std::cerr << "elapsed time: " << dur << "[µs] ";
        std::cerr << "Throughput: " << (mem_len * sizeof(int)) / dur << "[MBps]" << std::endl;
    }
}

int get_node_list(void)
{
    int a, got_nodes = 0;
    long free_node_sizes;

    int numnodes = numa_num_configured_nodes();
    int* node_to_use = (int *)malloc(numnodes * sizeof(int));
    int max_node = numa_max_node();
    for (a = 0; a <= max_node; a++) {
        if (numa_node_size(a, &free_node_sizes) > 0)
            node_to_use[got_nodes++] = a;
    }
    std::cerr << "numnodes: " << numnodes << std::endl;
    if(got_nodes != numnodes)
            return -1;
	return got_nodes;
}

int main(int argc, char* argv[]) 
{

    assert(argc == 4);
    int chunk_size = atoi(argv[1]); //
    // setting_a: False: random access / sequential access on the same core, True: random access on a core, sequential access on the other core
    TestMode setting_a = (TestMode)atoi(argv[2]); 
    int num_threads = atoi(argv[3]);
    int tot_cores_per_cpu = 8;
    int cidx, tidx;
    int mem_len;
    int id = 0;
    std::vector<std::thread> threads_vec; // vector of vectors
    std::vector<int*> mem_vec; // vector of vectors
    int numa_node_size = numa_node_size;

    std::cerr << "numa_available: " << numa_available() << std::endl;
    assert(numa_available() == 0);
    std::cerr << "numa_num_configured_cpus: " << numa_num_configured_cpus() << std::endl;
    tot_cores_per_cpu = numa_num_configured_cpus() / 4; // For a hyperthreaded system
    // for (int i = 0; i < 4; i++) {
    //     std::cerr << "numa_node_of_cpu(" << i * tot_cores_per_cpu << "): ";
    //     std::cerr << numa_node_of_cpu(i * tot_cores_per_cpu) << std::endl;
    // }

	int nr_nodes = get_node_list();
	if(nr_nodes == -1){
		fprintf(stderr, "Configured Nodes does not match available memory nodes\n");
		exit(1);
	} else if (nr_nodes < 2) {
		printf("A minimum of 2 nodes is required for this test.\n");
		exit(77);
	}
    if (setting_a == S_CPU) {
        std::cerr << "" << std::endl;
        std::cerr << "Random/Sequential on the same core" << std::endl;
        for (cidx = 0; cidx < nr_nodes; cidx++) {
            for (tidx = 0; tidx < num_threads * 2; tidx++) {
                mem_len = 1000 * 1000 * 1000;
                int* memory = (int*)numa_alloc_onnode(sizeof(int) * mem_len, cidx);
                memset(memory, '0', mem_len * sizeof(int));
                AccessMode mode;
                if (tidx < num_threads)
                    mode = Random;
                else
                    mode = Sequential;
                std::thread tmp_th(&memaccess_runner,
                                mode,
                                std::ref(memory),
                                chunk_size,
                                mem_len, id++);
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(cidx * tot_cores_per_cpu + tidx, &cpuset);
                int rc = pthread_setaffinity_np(tmp_th.native_handle(),
                                                sizeof(cpu_set_t), &cpuset);
                if (rc != 0) {
                    fprintf(stderr, "pthread_setaffinity_np failed\n");
                    exit(0);
                } else {
                    std::cerr << "Run core[" << cidx * tot_cores_per_cpu + tidx << "] for ";
                    if (mode == Random) {
                        std::cerr << "random access" << std::endl;
                    } else {
                        std::cerr << "sequential access" << std::endl;
                    }
                }
                threads_vec.push_back(std::move(tmp_th));
                mem_vec.push_back(std::move(memory));
            }
        }
    } else if (setting_a == D_CPU) {
        std::cerr << "" << std::endl;
        std::cerr << "Random/Sequential on different cores" << std::endl;
        for (cidx = 0; cidx < nr_nodes; cidx++) {
            for (tidx = 0; tidx < num_threads; tidx++) {
                mem_len = 1000 * 1000 * 1000;
                int* memory = (int*)numa_alloc_onnode(sizeof(int) * mem_len, cidx);
                memset(memory, '0', mem_len * sizeof(int));
                AccessMode mode;
                if (cidx == 0)
                    mode = Random;
                else if (cidx == 1)
                    mode = Sequential;
                std::thread tmp_th(&memaccess_runner,
                                mode,
                                std::ref(memory),
                                chunk_size,
                                mem_len, id++);
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(cidx * tot_cores_per_cpu + tidx, &cpuset);
                int rc = pthread_setaffinity_np(tmp_th.native_handle(),
                                                sizeof(cpu_set_t), &cpuset);
                if (rc != 0) {
                    fprintf(stderr, "pthread_setaffinity_np failed\n");
                    exit(0);
                } else {
                    std::cerr << "Run core[" << cidx * tot_cores_per_cpu + tidx << "] for ";
                    if (mode == Random) {
                        std::cerr << "random access" << std::endl;
                    } else {
                        std::cerr << "sequential access" << std::endl;
                    }
                }
                threads_vec.push_back(std::move(tmp_th));
                mem_vec.push_back(std::move(memory));
            }

        }
    } else if (setting_a == R_SINGLE) {
        std::cerr << "" << std::endl;
        std::cerr << "Random a single core" << std::endl;
        for (tidx = 0; tidx < num_threads; tidx++) {
            mem_len = 1000 * 1000 * 1000;
            int* memory = (int*)numa_alloc_onnode(sizeof(int) * mem_len, 0);
            memset(memory, '0', mem_len * sizeof(int));
            AccessMode mode;
            mode = Random;
            std::thread tmp_th(&memaccess_runner,
                            mode,
                            std::ref(memory),
                            chunk_size,
                            mem_len, id++);
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(tidx, &cpuset);
            int rc = pthread_setaffinity_np(tmp_th.native_handle(),
                                            sizeof(cpu_set_t), &cpuset);
            if (rc != 0) {
                fprintf(stderr, "pthread_setaffinity_np failed\n");
                exit(0);
            } else {
                std::cerr << "Run core[" << tidx << "] for ";
                if (mode == Random) {
                    std::cerr << "random access" << std::endl;
                } else {
                    std::cerr << "sequential access" << std::endl;
                }
            }
            threads_vec.push_back(std::move(tmp_th));
            mem_vec.push_back(std::move(memory));
        }
    } else if (setting_a == S_SINGLE) {
        std::cerr << "" << std::endl;
        std::cerr << "Random a single core" << std::endl;
        for (tidx = 0; tidx < num_threads; tidx++) {
            mem_len = 1000 * 1000 * 1000;
            int* memory = (int*)numa_alloc_onnode(sizeof(int) * mem_len, 0);
            memset(memory, '0', mem_len * sizeof(int));
            AccessMode mode;
            mode = Sequential;
            std::thread tmp_th(&memaccess_runner,
                            mode,
                            std::ref(memory),
                            chunk_size,
                            mem_len, id++);
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(tidx, &cpuset);
            int rc = pthread_setaffinity_np(tmp_th.native_handle(),
                                            sizeof(cpu_set_t), &cpuset);
            if (rc != 0) {
                fprintf(stderr, "pthread_setaffinity_np failed\n");
                exit(0);
            } else {
                std::cerr << "Run core[" << tidx << "] for ";
                if (mode == Random) {
                    std::cerr << "random access" << std::endl;
                } else {
                    std::cerr << "sequential access" << std::endl;
                }
            }
            threads_vec.push_back(std::move(tmp_th));
            mem_vec.push_back(std::move(memory));
        }
    }
    sleep(100);
}