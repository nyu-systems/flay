#!/usr/bin/env python3

import argparse
import logging
import os
import random
import subprocess
import sys
import tempfile
import time
import uuid
from datetime import datetime
from pathlib import Path
from typing import Any, List, Optional

import pyroute2

PARSER = argparse.ArgumentParser()
PARSER.add_argument(
    "rootdir",
    help="The root directory of the compiler source tree."
    "This is used to import P4C's Python libraries",
)
PARSER.add_argument("p4_file", help="the p4 file to process")
PARSER.add_argument(
    "-tf",
    "--testfile",
    dest="testfile",
    help="The path for the ptf py file for this test.",
)
PARSER.add_argument(
    "-td",
    "--testdir",
    dest="testdir",
    help="The location of the test directory.",
)
PARSER.add_argument(
    "-b",
    "--nocleanup",
    action="store_true",
    dest="nocleanup",
    help="Do not remove temporary results for failing tests.",
)
PARSER.add_argument(
    "-n",
    "--num-ifaces",
    default=6,
    dest="num_ifaces",
    help="How many virtual interfaces to create.",
)
PARSER.add_argument(
    "-ll",
    "--log_level",
    dest="log_level",
    default="WARNING",
    choices=["CRITICAL", "ERROR", "WARNING", "INFO", "DEBUG", "NOTSET"],
    help="The log level to choose.",
)

# Parse options and process argv
ARGS, ARGV = PARSER.parse_known_args()

# Append the root directory to the import path.
FILE_DIR = Path(__file__).resolve().parent
ROOT_DIR = Path(ARGS.rootdir).absolute()
sys.path.append(str(ROOT_DIR))

from backends.ebpf.targets.ebpfenv import (  # pylint: disable=wrong-import-position
    Bridge,
    BridgeConfiguration,
)
from tools import testutils  # pylint: disable=wrong-import-position


class Options:
    """Options for this testing script. Usually correspond to command line inputs."""

    # File that is being compiled.
    p4_file: Path = Path(".")
    # Path to ptf test file that is used.
    testfile: Path = Path(".")
    # Actual location of the test framework.
    testdir: Path = Path(".")
    # The base directory where tests are executed.
    rootdir: Path = Path(".")
    # The number of interfaces to create for this particular test.
    num_ifaces: int = 6
    # Use XDP mode instead of TC mode.
    xdp: bool = False
    # Whether to enable tracing logs.
    trace_logs_enabled: bool = False
    # The XDP2TC mode to use.
    xdp2tc_mode: str = "Meta"
    # Additional arguments for compilation passed from the command line.
    p4c_additional_args: str = ""


def get_dataplane_port_number(namespace: Optional[str], interface: str) -> Any:
    if namespace is None:
        with pyroute2.IPRoute() as ipr:
            return ipr.link_lookup(ifname=interface)[0]
    with pyroute2.NetNS(namespace) as ns:
        testutils.log.info("Using netns %s", namespace)
        return ns.link_lookup(ifname=interface)[0]


class EbpfPtfTestEnv:
    options: Options = Options()
    switch_proc: Optional[subprocess.Popen] = None
    bridge: Optional[Bridge] = None

    def __init__(self, options: Options) -> None:
        self.options = options
        # Create the virtual environment for the test execution.
        self.bridge = self.create_bridge(options.num_ifaces)
        self.bridge.ns_exec("ip link add name psa_recirc type dummy")
        self.bridge.ns_exec("ip link add name psa_cpu type dummy")
        self.bridge.ns_exec("ip link set dev psa_recirc up")
        self.bridge.ns_exec("ip link set dev psa_cpu up")

    def __del__(self) -> None:
        if self.bridge:
            self.bridge.ns_del()

    def create_bridge(self, num_ifaces: int) -> Bridge:
        """Create a network namespace environment."""
        testutils.log.info(
            "---------------------- Creating a namespace ----------------------",
        )
        random.seed(datetime.now().timestamp())
        bridge = Bridge(str(uuid.uuid4()), BridgeConfiguration(mtu=1500, bpf_kernel_stats=True))
        result = bridge.create_virtual_env(num_ifaces)
        if result != testutils.SUCCESS:
            bridge.ns_del()
            testutils.log.error(
                "---------------------- Namespace creation failed ----------------------",
            )
            raise SystemExit("Unable to create the namespace environment.")
        testutils.log.info(
            "---------------------- Namespace successfully created ----------------------"
        )
        return bridge

    def get_iface_str(self, num_ifaces: int, prefix: str = "") -> str:
        """Produce the PTF interface arguments based on the number of interfaces the PTF test uses."""
        iface_str = ""
        for iface_num in range(num_ifaces):
            iface_str += f"-i {iface_num}@{prefix}{iface_num} "
        return iface_str

    def get_iface_numbers(self, num_ifaces: int) -> str:
        """Produce the PTF interface arguments based on the number of interfaces the PTF test uses."""
        iface_str = ""
        for iface_num in range(num_ifaces):
            if iface_str != "":
                iface_str += ","
            iface_str += f"{iface_num}"
        return iface_str

    def compile_program(
        self,
        info_name: Path,
        ebpf_obj: Path,
        interface_list: str,
    ) -> int:
        if not self.bridge:
            testutils.log.error("Unable to run compile_program without a bridge.")
            return testutils.FAILURE
        """Compile the input P4 program using p4c-ebpf."""
        testutils.log.info("---------------------- Compiling with p4c-ebpf ----------------------")
        p4args = f"--p4runtime-files {info_name} --Wdisable=unused --max-ternary-masks 3"
        next_idx = 0
        for intf in interface_list.split(","):
            if intf != "psa_recirc":
                p4args += f" -DPORT{next_idx}={get_dataplane_port_number(self.bridge.ns_name, str(next_idx))}"
                next_idx += 1
        p4args += f" -DPSA_RECIRC={get_dataplane_port_number(self.bridge.ns_name, 'psa_recirc')}"
        if self.options.trace_logs_enabled:
            p4args += " --trace"
        if self.options.xdp2tc_mode is not None:
            p4args += " --xdp2tc=" + self.options.xdp2tc_mode
        if self.options.xdp:
            p4args += " --xdp"

        if self.options.p4c_additional_args:
            p4args = p4args + " " + self.options.p4c_additional_args

        cargs = (
            f"-DPSA_PORT_RECIRCULATE={get_dataplane_port_number(self.bridge.ns_name, 'psa_recirc')}"
        )
        _, returncode = testutils.exec_process(
            f"make -f {ROOT_DIR}/backends/ebpf/runtime/kernel.mk BPFOBJ={ebpf_obj} P4FILE={self.options.p4_file} "
            f"ARGS='{cargs}' P4C={self.options.rootdir}/build/p4c-ebpf P4ARGS='{p4args}' psa",
            shell=True,
        )
        if returncode != testutils.SUCCESS:
            testutils.log.error("Failed to compile the eBPF program %s.", self.options.p4_file)
        return returncode

    def run_ptf(self, ebpf_object: Path, info_name: Path, interface_list: str) -> int:
        if not self.bridge:
            testutils.log.error("Unable to run run_ptf without a bridge.")
            return testutils.FAILURE
        """Run the PTF test."""
        testutils.log.info("---------------------- Run PTF test ----------------------")
        # Add the tools PTF folder to the python path, it contains the base test.
        pypath = ROOT_DIR.joinpath("tools/ptf")
        ifaces = self.get_iface_str(num_ifaces=self.options.num_ifaces, prefix="br_")
        test_params = (
            f"interfaces='psa_recirc,{interface_list}'"
            f";xdp='{ 'True' if self.options.xdp else 'False'}';"
            f"packet_wait_time='0.1';test_dir='{self.options.testdir}';"
            f"ebpf_object='{ebpf_object}';"
            f"root_dir='{ROOT_DIR}';p4info='{info_name}';"
            f"nikss_cmd='{ROOT_DIR}/build/_deps/nikss_ctl-build/nikss-ctl';"
        )
        run_ptf_cmd = (
            f"ptf --pypath {pypath} --pypath {ROOT_DIR} {ifaces} "
            f"--log-file {self.options.testdir.joinpath('ptf.log')} "
            f"--test-params={test_params} --test-dir {self.options.testdir}"
        )
        returncode = self.bridge.ns_exec(run_ptf_cmd)
        return returncode


def run_test(options: Options) -> int:
    """Define the test environment and compile the P4 target
    Optional: Run the generated model"""
    test_name = Path(options.p4_file.name)
    ebpf_program = options.testdir.joinpath(test_name.with_suffix(".c"))
    ebpf_object = options.testdir.joinpath(test_name.with_suffix(".o"))
    info_name = options.testdir.joinpath(test_name.with_suffix(".p4info.txtpb"))
    # Copy the test file into the test folder so that it can be picked up by PTF.
    testutils.link_file_into_directory([options.p4_file, options.testfile], options.testdir)

    testenv: EbpfPtfTestEnv = EbpfPtfTestEnv(options)
    interface_list = testenv.get_iface_numbers(num_ifaces=options.num_ifaces)

    # Compile the P4 program.
    returncode = testenv.compile_program(info_name, ebpf_object, interface_list)
    if returncode != testutils.SUCCESS:
        return returncode

    # Run the PTF test and retrieve the result.
    result = testenv.run_ptf(ebpf_object, info_name, interface_list)
    # Delete the test environment and trigger a clean up.
    del testenv
    # Print ptf log if the results were not successful.
    if result != testutils.SUCCESS:
        ptf_log = options.testdir.joinpath("ptf.log")
        if ptf_log.exists():
            testutils.log.error("######## PTF log ########\n%s", ptf_log.read_text())
    return result


def create_options(test_args: Any) -> Optional[Options]:
    """Parse the input arguments and create a processed options object."""
    options = Options()
    result = testutils.check_if_file(test_args.p4_file)
    if not result:
        return None
    options.p4_file = result
    testfile = test_args.testfile
    if not testfile:
        testutils.log.info("No test file provided. Checking for file in folder.")
        testfile = options.p4_file.with_suffix(".py")
    result = testutils.check_if_file(testfile)
    if not result:
        return None
    options.testfile = Path(result)
    testdir = test_args.testdir
    if not testdir:
        testutils.log.info("No test directory provided. Generating temporary folder.")
        testdir = tempfile.mkdtemp(dir=Path(".").absolute())
        # Generous permissions because the program is usually edited by sudo.
        os.chmod(testdir, 0o755)
    options.testdir = Path(testdir)
    options.rootdir = Path(test_args.rootdir)
    options.num_ifaces = test_args.num_ifaces

    # Configure logging.
    logging.basicConfig(
        filename=options.testdir.joinpath("test.log"),
        format="%(levelname)s: %(message)s",
        level=getattr(logging, test_args.log_level),
        filemode="w",
    )
    stderr_log = logging.StreamHandler()
    stderr_log.setFormatter(logging.Formatter("%(levelname)s: %(message)s"))
    logging.getLogger().addHandler(stderr_log)
    return options


if __name__ == "__main__":
    test_options = create_options(ARGS)
    if not test_options:
        sys.exit(testutils.FAILURE)

    if not testutils.check_root():
        testutils.log.error("This script requires root privileges; Exiting.")
        sys.exit(1)

    # Run the test with the extracted options
    test_result = run_test(test_options)
    if not (ARGS.nocleanup or test_result != testutils.SUCCESS):
        testutils.log.info("Removing temporary test directory.")
        testutils.del_dir(test_options.testdir)
    sys.exit(test_result)
