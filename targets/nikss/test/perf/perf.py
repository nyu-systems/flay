import json
import sys
import time
from pathlib import Path
from typing import Optional

import ptf.testutils as ptfutils  # type: ignore
from google.protobuf import text_format  # type: ignore
from p4.config.v1 import p4info_pb2 as p4info  # type: ignore
from p4.v1 import p4runtime_pb2 as p4rt  # type: ignore

from backends.p4tools.modules.flay.targets.nikss.test.ebpf_ptf_test import (
    TEST_PIPELINE_ID,
    P4EbpfTest,
)
from backends.p4tools.modules.flay.targets.nikss.test.p4rt_to_nikss import (
    P4rtToNikss,
    parse_write_request,
)
from tools import testutils

FILE_DIR: Path = Path(__file__).resolve().parent
ROOT_DIR_OPT: Optional[str] = ptfutils.test_param_get("root_dir")
assert ROOT_DIR_OPT
ROOT_DIR: Path = Path(ROOT_DIR_OPT)

NIKSS_PATH: Path = ROOT_DIR.joinpath("backends/p4tools/modules/flay/targets/nikss/")
TREX_PATH = ROOT_DIR.joinpath("build/_deps/trex-src")
TREX_PY_PATH = TREX_PATH.joinpath("automation/trex_control_plane/interactive")
BPFTOOL_PATH = ROOT_DIR.joinpath("build/_deps/bpftool-src/bpftool")
sys.path.insert(0, str(TREX_PY_PATH))
sys.path.insert(0, str(TREX_PATH))
import automation.trex_control_plane.interactive.trex.stl.api as trex


def rx_example(tx_port: int, rx_port: int, burst_size: int, pps: int) -> bool:

    testutils.log.info(
        "Going to inject %s packets on port %s -" " checking RX stats on port %s",
        burst_size,
        tx_port,
        rx_port,
    )

    # create client
    c = trex.STLClient()
    passed = True

    try:
        pkt = trex.STLPktBuilder(
            pkt=trex.Ether(src="00:11:22:33:44:55", dst="00:77:66:55:44:33")
            / trex.IP(src="16.0.0.1", dst="48.0.0.1")
            / trex.UDP(dport=12, sport=1025)
            / 'at_least_16_bytes_payload_needed'
        )
        total_pkts = burst_size
        s1 = trex.STLStream(
            name='rx',
            packet=pkt,
            flow_stats=trex.STLFlowLatencyStats(pg_id=5),
            mode=trex.STLTXSingleBurst(total_pkts=total_pkts, pps=pps),
        )

        # connect to server
        c.connect()

        # prepare our ports
        c.reset(ports=[tx_port, rx_port])

        # add both streams to ports
        c.add_streams([s1], ports=[tx_port])

        testutils.log.info(
            "Injecting %s packets with size %s on port %s", total_pkts, pkt.get_pkt_len(), tx_port
        )

        rc = rx_iteration(c, tx_port, rx_port, total_pkts, pkt.get_pkt_len(), pps)
        if not rc:
            passed = False

    except trex.STLError as e:
        passed = False
        testutils.log.error(e)

    finally:
        c.disconnect()

    if passed:
        testutils.log.info("Test passed :-)")
    else:
        testutils.log.error("Test failed :-(")
    return passed


# RX one iteration
def rx_iteration(
    c: trex.STLClient, tx_port: int, rx_port: int, total_pkts: int, pkt_len: int, pps: int
) -> bool:

    c.clear_stats()

    c.start(ports=[tx_port])
    pgids = c.get_active_pgids()
    testutils.log.info("Currently used pgids: %s", pgids)

    for _ in range(1, int(total_pkts / pps)):
        time.sleep(1)
        stats = c.get_pgid_stats(pgids['latency'])
        flow_stats = stats['flow_stats'].get(5)
        rx_pps = flow_stats['rx_pps'][rx_port]
        tx_pps = flow_stats['tx_pps'][tx_port]
        rx_bps = flow_stats['rx_bps'][rx_port]
        tx_bps = flow_stats['tx_bps'][tx_port]
        rx_bps_l1 = flow_stats['rx_bps_l1'][rx_port]
        tx_bps_l1 = flow_stats['tx_bps_l1'][tx_port]
        testutils.log.info(
            "rx_pps:%s tx_pps:%s, rx_bps:%s/%s tx_bps:%s/%s",
            rx_pps,
            tx_pps,
            rx_bps,
            rx_bps_l1,
            tx_bps,
            tx_bps_l1,
        )
    c.wait_on_traffic(ports=[tx_port])
    stats = c.get_pgid_stats(pgids['latency'])
    flow_stats = stats['flow_stats'].get(5)
    global_lat_stats = stats['latency']
    lat_stats = global_lat_stats.get(5)
    if not flow_stats:
        testutils.log.error("no flow stats available")
        return False
    if not lat_stats:
        testutils.log.error("no latency stats available")
        return False

    tx_pkts = flow_stats['tx_pkts'].get(tx_port, 0)
    tx_bytes = flow_stats['tx_bytes'].get(tx_port, 0)
    rx_pkts = flow_stats['rx_pkts'].get(rx_port, 0)
    drops = lat_stats['err_cntrs']['dropped']
    ooo = lat_stats['err_cntrs']['out_of_order']
    dup = lat_stats['err_cntrs']['dup']
    sth = lat_stats['err_cntrs']['seq_too_high']
    stl = lat_stats['err_cntrs']['seq_too_low']
    old_flow = global_lat_stats['global']['old_flow']
    bad_hdr = global_lat_stats['global']['bad_hdr']
    lat = lat_stats['latency']
    jitter = lat['jitter']
    avg = lat['average']
    tot_max = lat['total_max']
    tot_min = lat['total_min']
    last_max = lat['last_max']
    hist = lat['histogram']

    if c.get_warnings():
        testutils.log.error("\n\n*** test had warnings ****\n\n")
        for w in c.get_warnings():
            testutils.log.error(w)
        return False

    testutils.log.info(
        f'Error counters: dropped:{drops}, ooo:{ooo} dup:{dup} seq too high:{sth} seq too low:{stl}'
    )
    if old_flow:
        testutils.log.error(f'Packets arriving too late after flow stopped: {old_flow}')
    if bad_hdr:
        testutils.log.error(f'Latency packets with corrupted info: {bad_hdr}')
    testutils.log.info('Latency info:')
    testutils.log.info("  Maximum latency(usec): %s", tot_max)
    testutils.log.info("  Minimum latency(usec): %s", tot_min)
    testutils.log.info("  Maximum latency in last sampling period (usec): %s", last_max)
    testutils.log.info("  Average latency(usec): %s", avg)
    testutils.log.info("  Jitter(usec): %s", jitter)
    testutils.log.info("  Latency distribution histogram:")
    latency_list = list(hist.keys())  # need to listify in order to be able to sort them.
    latency_list.sort()
    for sample in latency_list:
        range_start = sample
        if range_start == 0:
            range_end = 10
        else:
            range_end = range_start + pow(10, (len(str(range_start)) - 1))
        val = hist[sample]
        testutils.log.info(
            "    Packets with latency between %s and %s:%s ", range_start, range_end, val
        )

    if tx_pkts != total_pkts:
        testutils.log.error("TX pkts mismatch - got: %s, expected: %s", tx_pkts, total_pkts)
        testutils.log.error(flow_stats)
        return False
    testutils.log.info("TX pkts match   - %s", tx_pkts)

    if tx_bytes != (total_pkts * (pkt_len + 4)):  # +4 for ethernet CRC
        testutils.log.error(
            "TX bytes mismatch - got: %s, expected: %s", tx_bytes, total_pkts * pkt_len
        )
        testutils.log.error(flow_stats)
        return False
    testutils.log.info("TX bytes match  - %s", tx_bytes)

    if rx_pkts != total_pkts:
        testutils.log.error("RX pkts mismatch - got: %s, expected: %s", rx_pkts, total_pkts)
        testutils.log.error(flow_stats)
        return False
    testutils.log.info("RX pkts match   - %s", rx_pkts)
    return True


class PerfTest(P4EbpfTest):
    def setUp(self) -> None:
        super().setUp()
        self.p4info = Path(ptfutils.test_param_get("p4info"))

    def runTest(self) -> None:
        # testutils.exec_process("wireshark & ", shell=True)
        # time.sleep(3)
        p4rt_nikss = P4rtToNikss(self.p4info, TEST_PIPELINE_ID)

        test_proto = parse_write_request(FILE_DIR.joinpath("update_0.txtpb"))
        cmds = p4rt_nikss.translate_write_request(test_proto)
        for cmd in cmds:
            proc_result = testutils.exec_process(cmd, capture_output=False)
            if proc_result.returncode != 0:
                self.fail("Failed to translate write request")
                return

        switch_config = FILE_DIR.joinpath('switch_cfg.yaml')
        # TODO: Figure out output here?
        # trex_log = self.test_dir.joinpath("trex.log")
        trex_proc = testutils.open_process(
            f"./t-rex-64 --software -i --cfg {switch_config} > /dev/null 2>&1",
            cwd=TREX_PATH,
        )
        if trex_proc is None:
            self.fail("Failed to start trex")
            return
        time.sleep(2)
        result = rx_example(tx_port=0, rx_port=1, burst_size=10000000, pps=1000000)
        self.assertTrue(result)
        stats = testutils.exec_process(f"{BPFTOOL_PATH} prog show name xdp_ingress_func -j")
        trex_proc.terminate()
        if stats.output:
            stats_obj = json.loads(stats.output)
            if isinstance(stats_obj, list):
                testutils.log.warning("Stats object is a list, not a dict: %s", stats_obj)
            else:
                per_pkt_ns: float = stats_obj["run_cnt"] / stats_obj["run_time_ns"]
                testutils.log.info("Nanoseconds spent per packet: %s" % {per_pkt_ns})
