/*
 * Copyright 2019 Xilinx, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "table_dt.hpp"
#include "utils.hpp"
#include "cfg.hpp"
#include "tpch_read_2.hpp"

#include <sys/time.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <climits>
#include <unordered_map>
const int PU_NM = 8;
#include "gqe_api.hpp"
#include "q7.hpp"
int main(int argc, const char* argv[]) {
    std::cout << "\n------------ TPC-H GQE (1G) -------------\n";

    // cmd arg parser.
    ArgParser parser(argc, argv);

    std::string xclbin_path; // eg. q5kernel_VCU1525_hw.xclbin
    if (!parser.getCmdOption("-xclbin", xclbin_path)) {
        std::cout << "ERROR: xclbin path is not set!\n";
        return 1;
    }

    std::string in_dir;
    if (!parser.getCmdOption("-in", in_dir) || !is_dir(in_dir)) {
        std::cout << "ERROR: input dir is not specified or not valid.\n";
        return 1;
    }

    int board = 1;
    std::string board_s;
    if (parser.getCmdOption("-b", board_s)) {
        try {
            board = std::stoi(board_s);
        } catch (...) {
            board = 1;
        }
    }

    int num_rep = 1;

    std::string num_str;
    if (parser.getCmdOption("-rep", num_str)) {
        try {
            num_rep = std::stoi(num_str);
        } catch (...) {
            num_rep = 1;
        }
    }
    if (num_rep > 20) {
        num_rep = 20;
        std::cout << "WARNING: limited repeat to " << num_rep << " times\n.";
    }

    // ********************************************************** //

    // Get CL devices.
    std::vector<cl::Device> devices = xcl::get_xil_devices();
    cl::Device device = devices[board];

    // Create context and command queue for selected device
    cl::Context context(device);
    cl::CommandQueue q(context, device, CL_QUEUE_PROFILING_ENABLE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE);
    std::string devName = device.getInfo<CL_DEVICE_NAME>();
    std::cout << "Selected Device " << devName << "\n";

    cl::Program::Binaries xclBins = xcl::import_binary_file(xclbin_path);
    std::vector<cl::Device> devices_;
    devices_.push_back(device);
    cl::Program program(context, devices_, xclBins);

    std::cout << "Kernel has been created\n";
    // ********************************************************* //
    /**
     * 1.Table and host cols Created
     */
    // for device table
    const int NumTable = 5;
    const int NumSweep = 5;
    Table tbs[NumTable];
    tbs[0] = Table("nation", 25, 2, in_dir);
    tbs[0].addCol("n_nationkey", 4);
    tbs[0].addCol("n_name", TPCH_READ_NATION_LEN + 1, 0, 0);
    // tbs[0].addCol("n_rowid", 4);

    tbs[1] = Table("supplier", 10000, 2, in_dir);
    tbs[1].addCol("s_nationkey", 4);
    tbs[1].addCol("s_suppkey", 4);

    tbs[2] = Table("lineitem", 6001215, 5, in_dir);
    tbs[2].addCol("l_orderkey", 4);
    tbs[2].addCol("l_suppkey", 4);
    tbs[2].addCol("l_shipdate", 4);
    tbs[2].addCol("l_extendedprice", 4);
    tbs[2].addCol("l_discount", 4);

    tbs[3] = Table("orders", 1500000, 2, in_dir);
    tbs[3].addCol("o_orderkey", 4);
    tbs[3].addCol("o_custkey", 4);

    tbs[4] = Table("customer", 150000, 2, in_dir);
    tbs[4].addCol("c_custkey", 4);
    tbs[4].addCol("c_nationkey", 4);

    std::cout << "DEBUG0" << std::endl;
    // tbx is for the empty bufferB in kernel
    Table tbx(512);
    Table th0("tk0", 180000, 8, "");
    Table tk0("tk0", 180000, 8, "");
    Table tk1("tk1", 180000, 8, "");
    std::cout << "Table Creation done." << std::endl;
    /**
     * 2.allocate CPU
     */
    for (int i = 0; i < NumTable; i++) {
        tbs[i].allocateHost();
    }
    th0.allocateHost();
    tk0.allocateHost();
    tk1.allocateHost();

    std::cout << "Table allocation CPU done." << std::endl;

    /**
     * 3. load kernel config from dat and table from disk
     */
    cfgCmd cfgcmds[NumSweep];
    for (int i = 0; i < NumSweep; i++) {
        cfgcmds[i].allocateHost();
    };
    get_cfg_dat_1(cfgcmds[0].cmd);
    get_cfg_dat_2(cfgcmds[1].cmd);
    get_cfg_dat_3(cfgcmds[2].cmd);
    get_cfg_dat_4(cfgcmds[3].cmd);
    get_cfg_dat_5(cfgcmds[4].cmd);

    for (int i = 0; i < NumTable; i++) {
        tbs[i].loadHost();
    };

    /**
     * 4.allocate device
     */
    for (int i = 0; i < NumTable; i++) {
        tbs[i].allocateDevBuffer(context, 32);
    }
    tk0.allocateDevBuffer(context, 32);
    tk1.allocateDevBuffer(context, 32);
    th0.allocateDevBuffer(context, 32);

    for (int i = 0; i < NumSweep; i++) {
        cfgcmds[i].allocateDevBuffer(context, 32);
    };

    std::cout << "Table allocation device done." << std::endl;

    /**
     * 5.kernels (host and device)
     */

    bufferTmp buftmp(context);
    buftmp.initBuffer(q);
    // kernel Engine
    krnlEngine krnlstep[NumSweep];
    for (int i = 0; i < NumSweep; i++) {
        krnlstep[i] = krnlEngine(program, q, "gqeJoin");
    }

    krnlstep[0].setup(th0, tbs[1], tk0, cfgcmds[0], buftmp);
    krnlstep[1].setup(tk0, tbs[2], tk1, cfgcmds[1], buftmp);
    krnlstep[2].setup(tk1, tbs[3], tk0, cfgcmds[2], buftmp);
    krnlstep[3].setup(tk0, tbs[4], tk1, cfgcmds[3], buftmp);
    krnlstep[4].setup(th0, tk1, tk0, cfgcmds[4], buftmp);

    // transfer Engine
    transEngine transin[NumSweep];
    transEngine transout[NumSweep];
    for (int i = 0; i < NumSweep; i++) {
        transin[i].setq(q);
        transout[i].setq(q);
    }

    transin[0].add(&(tbs[1]));
    transin[0].add(&(tbs[4]));
    for (int i = 0; i < NumSweep; i++) {
        transin[0].add(&(cfgcmds[i]));
    };
    q.finish();
    std::cout << "Kernel/Transfer have been setup\n";

    // events
    std::vector<cl::Event> eventsh2d_write[NumSweep];
    std::vector<cl::Event> eventsd2h_read[NumSweep];
    std::vector<cl::Event> events[NumSweep];
    std::vector<cl::Event> events_grp[NumSweep];
    for (int i = 0; i < NumSweep; i++) {
        events[i].resize(1);
    };
    for (int i = 0; i < NumSweep; i++) {
        eventsh2d_write[i].resize(1);
    };
    for (int i = 0; i < NumSweep; i++) {
        eventsd2h_read[i].resize(1);
    };

#ifdef INI
    tk0.initBuffer(q);
    tk1.initBuffer(q);
#endif
    struct timeval tv_r_s, tv_r_0, tv_r_1, tv_r_e;
    gettimeofday(&tv_r_s, 0);
    // step1 :filter of customer
    NationFilter(tbs[0], th0);
    gettimeofday(&tv_r_0, 0);

    transin[0].add(&th0);
    transin[0].host2dev(0, nullptr, &(eventsh2d_write[0][0]));
    krnlstep[0].run(0, &(eventsh2d_write[0]), &(events[0][0]));
    //  q7Join_t1_s(th0,tbs[1],tk0);

    transin[1].add(&(tbs[2]));
    transin[1].host2dev(0, &(eventsh2d_write[0]), &(eventsh2d_write[1][0]));
    events_grp[1].push_back(eventsh2d_write[1][0]);
    events_grp[1].push_back(events[0][0]);
    krnlstep[1].run(0, &(events_grp[1]), &(events[1][0]));
    //  q7Join_t2_l(tk0,tbs[2],tk1);

    transin[2].add(&(tbs[3]));
    transin[2].host2dev(0, &(eventsh2d_write[1]), &(eventsh2d_write[2][0]));
    events_grp[2].push_back(eventsh2d_write[2][0]);
    events_grp[2].push_back(events[1][0]);
    krnlstep[2].run(0, &(events_grp[2]), &(events[2][0]));

    krnlstep[3].run(0, &(events[2]), &(events[3][0]));

    krnlstep[4].run(0, &(events[3]), &(events[4][0]));

    transout[0].add(&tk0);
    transout[0].dev2host(0, &(events[4]), &(eventsd2h_read[0][0]));
    q.finish();

    gettimeofday(&tv_r_1, 0);
    // step6 : group
    q7Group(tk0, tbs[0], tk1);
    q7Sort(tk1, tk0);
    gettimeofday(&tv_r_e, 0);

    print_h_time(tv_r_s, tv_r_s, tv_r_0, "NationFilter..");
    int64_t offset = tvdiff(&tv_r_s, &tv_r_0) / 1000;
    cl_ulong kstart;
    eventsh2d_write[0][0].getProfilingInfo(CL_PROFILING_COMMAND_START, &kstart);
    print_d_time(eventsh2d_write[0][0], eventsh2d_write[0][0], kstart, "data trans 0", offset);
    print_d_time(events[0][0], events[0][0], kstart, "kernel0", offset);
    print_d_time(eventsh2d_write[1][0], eventsh2d_write[1][0], kstart, "data trans 1", offset);
    print_d_time(events[1][0], events[1][0], kstart, "kernel1", offset);
    print_d_time(events[2][0], events[2][0], kstart, "kernel2", offset);
    print_d_time(eventsh2d_write[2][0], eventsh2d_write[2][0], kstart, "data trans 2", offset);
    print_d_time(events[3][0], events[3][0], kstart, "kernel2", offset);
    print_d_time(eventsh2d_write[0][0], eventsd2h_read[0][0], kstart, "all kernels", offset);
    print_h_time(tv_r_s, tv_r_1, tv_r_e, "Group&Sort..");
    std::cout << "All execution time of Host " << std::dec << tvdiff(&tv_r_s, &tv_r_e) / 1000 << " ms" << std::endl;

    return 0;
}
