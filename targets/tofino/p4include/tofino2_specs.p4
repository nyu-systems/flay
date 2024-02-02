/* -*- P4_16 -*- */

/**
 * Copyright (c) Intel Corporation
 * SPDX-License-Identifier: CC-BY-ND-4.0
 */


#ifndef _TOFINO2_P4_SPECS_
#define _TOFINO2_P4_SPECS_

/**
 Version Notes:

 1.0.1:
 - Restructuralize P4 header files (t2na.p4 -> tofino2_specs.p4 + tofino2_base.p4 + tofino2_arch.p4)
   - t2na.p4               : Top-level header file to be included by P4 programs, includes the below
     -> tofino2_specs.p4   : Target-device-specific types, constants and macros
     -> tofino2_arch.p4    : Portable parsers, controls and packages (originally tofino2arch.p4)
        -> tofino2_base.p4 : Portable constants, headers, externs etc. (originally tofino2.p4)

*/

#include <core.p4>

// ----------------------------------------------------------------------------
// COMMON TYPES
// ----------------------------------------------------------------------------
#define PORT_ID_WIDTH                  9
typedef bit<PORT_ID_WIDTH>             PortId_t;            // Port id -- ingress or egress port
#define MULTICAST_GROUP_ID_WIDTH       16
typedef bit<MULTICAST_GROUP_ID_WIDTH>  MulticastGroupId_t;  // Multicast group id
#define QUEUE_ID_WIDTH                 7
typedef bit<QUEUE_ID_WIDTH>            QueueId_t;           // Queue id
#define MIRROR_TYPE_WIDTH              4
typedef bit<MIRROR_TYPE_WIDTH>         MirrorType_t;        // Mirror type
#define MIRROR_ID_WIDTH                8
typedef bit<MIRROR_ID_WIDTH>           MirrorId_t;          // Mirror id
#define RESUBMIT_TYPE_WIDTH            3
typedef bit<RESUBMIT_TYPE_WIDTH>       ResubmitType_t;      // Resubmit type
#define DIGEST_TYPE_WIDTH              3
typedef bit<DIGEST_TYPE_WIDTH>         DigestType_t;        // Digest type
#define REPLICATION_ID_WIDTH           16
typedef bit<REPLICATION_ID_WIDTH>      ReplicationId_t;     // Replication id
#define L1_EXCLUSION_ID_WIDTH          16
typedef bit<L1_EXCLUSION_ID_WIDTH>     L1ExclusionId_t;     // L1 Exclusion id
#define L2_EXCLUSION_ID_WIDTH          9
typedef bit<L2_EXCLUSION_ID_WIDTH>     L2ExclusionId_t;     // L2 Exclusion id

// CloneId_t will be deprecated in 9.4. Adding a typedef for any old references.
typedef MirrorType_t CloneId_t;

typedef error ParserError_t;


const bit<32> PORT_METADATA_SIZE = 32w192;


#define DEVPORT_PIPE_MASK   0x180
#define DEVPORT_PIPE(port)  (port)[8:7]
#define DEVPORT_PORT_MASK   0x7f
#define DEVPORT_PORT(port)  (port)[6:0]

#endif  /* _TOFINO2_P4_SPECS_ */
