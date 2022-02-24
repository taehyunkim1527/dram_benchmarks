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
#include <stdint.h>

#define TLOG(id, mode, mem_len_int, t_begin, t_end, dur) {\
            t_end = std::chrono::steady_clock::now();\
            std::cerr << "[" << id << "]";\
            if (mode == Random)\
                std::cerr << " [Random] ";\
            else if (mode == Sequential)\
                std::cerr << " [Sequential] ";\
            dur = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_begin).count();\
            std::cerr << "elapsed time: " << dur << "[µs] ";\
            std::cerr << "Throughput: " << (mem_len_int * sizeof(int)) / dur << "[MBps]" << std::endl;}
#define TESTSEC = 100;

enum AccessMode{
    Random, Sequential
};
enum TestMode{
    S_SR, D_SR, OSOR, S_R, S_S
    // S_CPU, D_CPU, R_SINGLE, S_SINGLE
};

int memaccess_runner(AccessMode mode, int* memory, int* target_memory, int64_t chunk_size, int64_t mem_len, int id) 
{
    sleep(1);
    int tmp, dur;
    int64_t idx = 0;
    int64_t randidx = 0;
    std::chrono::steady_clock::time_point t_begin, t_end;
    while (true) {
        t_begin = std::chrono::steady_clock::now();
        idx = 0;
        randidx = 0;
        if (mode == Random) {
            int64_t chunk_size_int = sizeof(int) / sizeof(int); // Random access
            int64_t mem_len_int = mem_len / sizeof(int);
            if (chunk_size_int == 1) {
                while (idx + chunk_size_int < mem_len_int) {
                    randidx = randidx + 128; // Index computation overhead affects the performance a lot
                    if (randidx >= mem_len_int)
                        randidx = 0;
                    // randidx = (randidx + 100) % mem_len_int; // Index computation overhead affects the performance a lot
                    // randidx = rand() % mem_len_int; // Index computation overhead affects the performance a lot
                    *((int*)target_memory + randidx) = *((int*)memory + randidx); // Copy
                    idx++;
                    if ((idx & 0x3ffffff) == 0) {
                        TLOG(id, mode, 0x4000000, t_begin, t_end, dur);
                        t_begin = std::chrono::steady_clock::now();
                    }
                }
            } else {
                while (idx * chunk_size_int + chunk_size_int < mem_len_int) {
                    randidx = ((idx * 10000) % (mem_len_int - chunk_size_int - 1)) / chunk_size_int;
                    // randidx = (rand() % (mem_len_int / chunk_size_int)) * chunk_size_int; // Rand functions is followed by a mem read
                    for (int i = 0; i < chunk_size_int; i++) {
                        // *((int*)memory + (randidx * chunk_size_int) + i) + 1; // Just read
                        *((int*)target_memory + (randidx * chunk_size_int) + i) = *((int*)memory + (randidx * chunk_size_int) + i); // Copy
                    }
                    idx++;
                    if (idx % (mem_len_int / 20) == 0) {
                        TLOG(id, mode, (mem_len_int / 20), t_begin, t_end, dur);
                        t_begin = std::chrono::steady_clock::now();
                    }
                }
            }
        } else if (mode == Sequential) {
            int64_t chunk_size_int = chunk_size / sizeof(int); // Minimum unit is an integer
            int64_t mem_len_int = mem_len / sizeof(int);
            int64_t idx_chunk = idx * chunk_size_int;
            while (idx_chunk + chunk_size_int < mem_len_int) {
                // for (int i = 0; i < chunk_size_int; i++) {
                //     // *((int*)memory + idx_chunk + i) + 1; // Just read
                //     *((int*)target_memory + idx_chunk + i) = *((int*)memory + idx_chunk + i); // Copy
                // }
                memcpy((int*)memory + idx_chunk, (int*)target_memory + idx_chunk, chunk_size);
                idx++;
                idx_chunk = idx * chunk_size_int;
            }
            TLOG(id, mode, mem_len_int, t_begin, t_end, dur);
        }
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
    int64_t chunk_size = atoi(argv[1]); //
    // setting_a: False: random access / sequential access on the same core, True: random access on a core, sequential access on the other core
    TestMode setting_a = (TestMode)atoi(argv[2]); 
    int num_threads = atoi(argv[3]);
    int tot_cores_per_cpu = 24;
    int cidx, tidx;
    int64_t mem_len;
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
    if (setting_a == S_SR) {
        std::cerr << "" << std::endl;
        std::cerr << "Random/Sequential on the same core (One CPU)" << std::endl;
        cidx = 0;
        assert(num_threads % 2 == 0); // S_SR mode should use the even number of num_threads
        for (tidx = 0; tidx < num_threads; tidx++) {
            mem_len = 1000 * 1000 * 1000 * sizeof(int) / sizeof(int);
            int* memory = (int*)numa_alloc_onnode(mem_len, cidx);
            int* target_memory = (int*)numa_alloc_onnode(mem_len, cidx);
            if (memory == NULL | target_memory == NULL) {
                std::cerr << "Memory allocation error " << __LINE__ << std::endl;
                exit(0);
            }
            memset(memory, '0', mem_len);
            memset(target_memory, '0', mem_len);
            AccessMode mode;
            if (tidx < num_threads / 2)
                mode = Random;
            else
                mode = Sequential;
            std::thread tmp_th(&memaccess_runner,
                            mode,
                            std::ref(memory),
                            std::ref(target_memory),
                            chunk_size,
                            mem_len, id++);
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(tidx % (num_threads / 2), &cpuset);
            int rc = pthread_setaffinity_np(tmp_th.native_handle(),
                                            sizeof(cpu_set_t), &cpuset);
            if (rc != 0) {
                fprintf(stderr, "pthread_setaffinity_np failed\n");
                exit(0);
            } else {
                std::cerr << "Run core[" << tidx % (num_threads / 2) << "] for ";
                if (mode == Random) {
                    std::cerr << "random access" << std::endl;
                } else {
                    std::cerr << "sequential access" << std::endl;
                }
            }
            threads_vec.push_back(std::move(tmp_th));
            mem_vec.push_back(std::move(memory));
            mem_vec.push_back(std::move(target_memory));
        }
    } else if (setting_a == D_SR) {
        std::cerr << "" << std::endl;
        std::cerr << "Random/Sequential on the same core (Two CPUs)" << std::endl;
        if(nr_nodes == -1){
            fprintf(stderr, "Configured Nodes does not match available memory nodes\n");
            exit(1);
        } else if (nr_nodes < 2) {
            printf("A minimum of 2 nodes is required for this test.\n");
            exit(77);
        }
        assert(num_threads % 2 == 0); // S_SR mode should use the even number of num_threads
        for (cidx = 0; cidx < nr_nodes; cidx++) {
            for (tidx = 0; tidx < num_threads; tidx++) {
                mem_len = 1000 * 1000 * 1000 * sizeof(int) / sizeof(int);
                int* memory = (int*)numa_alloc_onnode(mem_len, cidx);
                int* target_memory = (int*)numa_alloc_onnode(mem_len, cidx);
                if (memory == NULL | target_memory == NULL) {
                    std::cerr << "Memory allocation error " << __LINE__ << std::endl;
                    exit(0);
                }
                memset(memory, '0', mem_len);
                memset(target_memory, '0', mem_len);
                AccessMode mode;
                if (tidx < num_threads / 2)
                    mode = Random;
                else
                    mode = Sequential;
                std::thread tmp_th(&memaccess_runner,
                                mode,
                                std::ref(memory),
                                std::ref(target_memory),
                                chunk_size,
                                mem_len, id++);
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(cidx * tot_cores_per_cpu + tidx % (num_threads / 2), &cpuset);
                int rc = pthread_setaffinity_np(tmp_th.native_handle(),
                                                sizeof(cpu_set_t), &cpuset);
                if (rc != 0) {
                    fprintf(stderr, "pthread_setaffinity_np failed\n");
                    exit(0);
                } else {
                    std::cerr << "Run core[" << cidx * tot_cores_per_cpu + tidx % (num_threads / 2) << "] for ";
                    if (mode == Random) {
                        std::cerr << "random access" << std::endl;
                    } else {
                        std::cerr << "sequential access" << std::endl;
                    }
                }
                threads_vec.push_back(std::move(tmp_th));
                mem_vec.push_back(std::move(memory));
                mem_vec.push_back(std::move(target_memory));
            }
        }
    } else if (setting_a == OSOR) {
        std::cerr << "" << std::endl;
        std::cerr << "Random/Sequential on different cores" << std::endl;
        if(nr_nodes == -1){
            fprintf(stderr, "Configured Nodes does not match available memory nodes\n");
            exit(1);
        } else if (nr_nodes < 2) {
            printf("A minimum of 2 nodes is required for this test.\n");
            exit(77);
        }
        for (cidx = 0; cidx < nr_nodes; cidx++) {
            int _num_threads;
            if (cidx == 0) {
                _num_threads = 1;
            } else if (cidx == 1) {
                _num_threads = num_threads;
            }
            for (tidx = 0; tidx < _num_threads; tidx++) {
                mem_len = 1000 * 1000 * 1000 * sizeof(int) / sizeof(int);
                int* memory = (int*)numa_alloc_onnode(mem_len, cidx);
                int* target_memory = (int*)numa_alloc_onnode(mem_len, cidx);
                if (memory == NULL | target_memory == NULL) {
                    std::cerr << "Memory allocation error " << __LINE__ << std::endl;
                    exit(0);
                }
                memset(memory, '0', mem_len);
                memset(target_memory, '0', mem_len);
                AccessMode mode;
                if (cidx == 0)
                    mode = Random;
                else if (cidx == 1)
                    mode = Sequential;
                std::thread tmp_th(&memaccess_runner,
                                mode,
                                std::ref(memory),
                                std::ref(target_memory),
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
                mem_vec.push_back(std::move(target_memory));
            }

        }
    } else if (setting_a == S_R) {
        std::cerr << "" << std::endl;
        std::cerr << "Random in a single core" << std::endl;
        cidx = 0;
        for (tidx = 0; tidx < num_threads; tidx++) {
            mem_len = 1000 * 1000 * 1000 * sizeof(int) / sizeof(int);
            int* memory = (int*)numa_alloc_onnode(mem_len, cidx);
            int* target_memory = (int*)numa_alloc_onnode(mem_len, cidx);
            if (memory == NULL | target_memory == NULL) {
                std::cerr << "Memory allocation error " << __LINE__ << std::endl;
                exit(0);
            }
            memset(memory, '0', mem_len);
            memset(target_memory, '0', mem_len);
            AccessMode mode;
            mode = Random;
            std::thread tmp_th(&memaccess_runner,
                            mode,
                            std::ref(memory),
                            std::ref(target_memory),
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
            mem_vec.push_back(std::move(target_memory));
        }
    } else if (setting_a == S_S) {
        std::cerr << "" << std::endl;
        std::cerr << "Sequential in a single core" << std::endl;
        cidx = 0;
        for (tidx = 0; tidx < num_threads; tidx++) {
            mem_len = 1000 * 1000 * 1000 * sizeof(int) / sizeof(int);
            int* memory = (int*)numa_alloc_onnode(mem_len, cidx);
            int* target_memory = (int*)numa_alloc_onnode(mem_len, cidx);
            if (memory == NULL | target_memory == NULL) {
                std::cerr << "Memory allocation error " << __LINE__ << std::endl;
                exit(0);
            }
            memset(memory, '0', mem_len);
            memset(target_memory, '0', mem_len);
            AccessMode mode;
            mode = Sequential;
            std::thread tmp_th(&memaccess_runner,
                            mode,
                            std::ref(memory),
                            std::ref(target_memory),
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
            mem_vec.push_back(std::move(target_memory));
        }
    }
    sleep(1000);
}